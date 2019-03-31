// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Sultan Alsawaf <sultan@kerneltoast.com>.
 */

#define pr_fmt(fmt) "cpu_input_boost: " fmt

#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/kthread.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>

static unsigned int input_boost_freq_lp = CONFIG_INPUT_BOOST_FREQ_LP;
static unsigned int input_boost_freq_hp = CONFIG_INPUT_BOOST_FREQ_PERF;
static unsigned short input_boost_duration = CONFIG_INPUT_BOOST_DURATION_MS;

module_param(input_boost_freq_lp, uint, 0644);
module_param(input_boost_freq_hp, uint, 0644);
module_param(input_boost_duration, short, 0644);

/* Available bits for boost state */
#define SCREEN_OFF		BIT(0)
#define INPUT_BOOST		BIT(1)
#define WAKE_BOOST		BIT(2)
#define MAX_BOOST		BIT(3)

struct boost_drv {
	struct delayed_work input_unboost;
	struct delayed_work max_unboost;
	struct notifier_block cpu_notif;
	struct notifier_block fb_notif;
	atomic64_t max_boost_expires;
	wait_queue_head_t boost_waitq;
	unsigned long input_boost_jifs;
	unsigned long wake_boost_jifs;
	atomic_t state;
};

static struct boost_drv *boost_drv_g __read_mostly;

static u32 get_boost_freq(struct boost_drv *b, u32 cpu)
{
	if (cpumask_test_cpu(cpu, cpu_lp_mask))
		return input_boost_freq_lp;

	return input_boost_freq_hp;
}

static u32 get_boost_state(struct boost_drv *b)
{
	return atomic_read(&b->state);
}

static void set_boost_bit(struct boost_drv *b, u32 state)
{
	atomic_or(state, &b->state);
}

static void clear_boost_bit(struct boost_drv *b, u32 state)
{
	atomic_andnot(state, &b->state);
}

static void update_online_cpu_policy(void)
{
	u32 cpu;

	/* Only one CPU from each cluster needs to be updated */
	get_online_cpus();
	cpu = cpumask_first_and(cpu_lp_mask, cpu_online_mask);
	cpufreq_update_policy(cpu);
	cpu = cpumask_first_and(cpu_perf_mask, cpu_online_mask);
	cpufreq_update_policy(cpu);
	put_online_cpus();
}

static void __cpu_input_boost_kick(struct boost_drv *b)
{
	if (get_boost_state(b) & SCREEN_OFF)
		return;

	set_boost_bit(b, INPUT_BOOST);
	wake_up(&b->boost_waitq);
	mod_delayed_work(system_unbound_wq, &b->input_unboost,
			 b->input_boost_jifs);
}

void cpu_input_boost_kick(void)
{
	struct boost_drv *b = boost_drv_g;

	if (!b)
		return;

	__cpu_input_boost_kick(b);
}

static void __cpu_input_boost_kick_max(struct boost_drv *b,
				       unsigned int duration_ms)
{
	unsigned long boost_jifs = msecs_to_jiffies(duration_ms);
	unsigned long curr_expires, new_expires;

	if (get_boost_state(b) & SCREEN_OFF)
		return;

	do {
		curr_expires = atomic64_read(&b->max_boost_expires);
		new_expires = jiffies + boost_jifs;

		/* Skip this boost if there's a longer boost in effect */
		if (time_after(curr_expires, new_expires))
			return;
	} while (atomic64_cmpxchg(&b->max_boost_expires, curr_expires,
				  new_expires) != curr_expires);

	set_boost_bit(b, MAX_BOOST);
	wake_up(&b->boost_waitq);
	mod_delayed_work(system_unbound_wq, &b->max_unboost, boost_jifs);
}

void cpu_input_boost_kick_max(unsigned int duration_ms)
{
	struct boost_drv *b = boost_drv_g;

	if (!b)
		return;

	__cpu_input_boost_kick_max(b, duration_ms);
}

static void input_unboost_worker(struct work_struct *work)
{
	struct boost_drv *b = container_of(to_delayed_work(work),
					   typeof(*b), input_unboost);

	clear_boost_bit(b, INPUT_BOOST);
	wake_up(&b->boost_waitq);
}

static void max_unboost_worker(struct work_struct *work)
{
	struct boost_drv *b = container_of(to_delayed_work(work),
					   typeof(*b), max_unboost);

	clear_boost_bit(b, WAKE_BOOST | MAX_BOOST);
	wake_up(&b->boost_waitq);
}

static int cpu_boost_thread(void *data)
{
	static const struct sched_param sched_max_rt_prio = {
		.sched_priority = MAX_RT_PRIO - 1
	};
	struct boost_drv *b = data;
	u32 old_state = 0;

	sched_setscheduler_nocheck(current, SCHED_FIFO, &sched_max_rt_prio);

	while (!kthread_should_stop()) {
		u32 curr_state;

		wait_event_interruptible(b->boost_waitq,
			(curr_state = get_boost_state(b)) != old_state ||
			kthread_should_stop());

		old_state = curr_state;
		update_online_cpu_policy();
	}

	return 0;
}

static int cpu_notifier_cb(struct notifier_block *nb,
			   unsigned long action, void *data)
{
	struct boost_drv *b = container_of(nb, typeof(*b), cpu_notif);
	struct cpufreq_policy *policy = data;
	u32 boost_freq, state;

	if (action != CPUFREQ_ADJUST)
		return NOTIFY_OK;

	state = get_boost_state(b);

	/* Unboost when the screen is off */
	if (state & SCREEN_OFF) {
		policy->min = policy->cpuinfo.min_freq;
		return NOTIFY_OK;
	}

	/* Boost CPU to max frequency for max boost */
	if (state & MAX_BOOST) {
		policy->min = policy->max;
		return NOTIFY_OK;
	}

	/*
	 * Boost to policy->max if the boost frequency is higher. When
	 * unboosting, set policy->min to the absolute min freq for the CPU.
	 */
	if (state & INPUT_BOOST) {
		boost_freq = get_boost_freq(b, policy->cpu);
		policy->min = min(policy->max, boost_freq);
	} else {
		policy->min = policy->cpuinfo.min_freq;
	}

	return NOTIFY_OK;
}

static int fb_notifier_cb(struct notifier_block *nb,
			  unsigned long action, void *data)
{
	struct boost_drv *b = container_of(nb, typeof(*b), fb_notif);
	struct fb_event *evdata = data;
	int *blank = evdata->data;

	/* Parse framebuffer blank events as soon as they occur */
	if (action != FB_EARLY_EVENT_BLANK)
		return NOTIFY_OK;

	/* Boost when the screen turns on and unboost when it turns off */
	if (*blank == FB_BLANK_UNBLANK) {
		clear_boost_bit(b, SCREEN_OFF);
		__cpu_input_boost_kick_max(b, CONFIG_WAKE_BOOST_DURATION_MS);
	} else {
		set_boost_bit(b, SCREEN_OFF);
		wake_up(&b->boost_waitq);
	}

	return NOTIFY_OK;
}

static void cpu_input_boost_input_event(struct input_handle *handle,
					unsigned int type, unsigned int code,
					int value)
{
	struct boost_drv *b = handle->handler->private;

	__cpu_input_boost_kick(b);
}

static int cpu_input_boost_input_connect(struct input_handler *handler,
					 struct input_dev *dev,
					 const struct input_device_id *id)
{
	struct input_handle *handle;
	int ret;

	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "cpu_input_boost_handle";

	ret = input_register_handle(handle);
	if (ret)
		goto free_handle;

	ret = input_open_device(handle);
	if (ret)
		goto unregister_handle;

	return 0;

unregister_handle:
	input_unregister_handle(handle);
free_handle:
	kfree(handle);
	return ret;
}

static void cpu_input_boost_input_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id cpu_input_boost_ids[] = {
	/* Multi-touch touchscreen */
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT |
			INPUT_DEVICE_ID_MATCH_ABSBIT,
		.evbit = { BIT_MASK(EV_ABS) },
		.absbit = { [BIT_WORD(ABS_MT_POSITION_X)] =
			BIT_MASK(ABS_MT_POSITION_X) |
			BIT_MASK(ABS_MT_POSITION_Y) }
	},
	/* Touchpad */
	{
		.flags = INPUT_DEVICE_ID_MATCH_KEYBIT |
			INPUT_DEVICE_ID_MATCH_ABSBIT,
		.keybit = { [BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH) },
		.absbit = { [BIT_WORD(ABS_X)] =
			BIT_MASK(ABS_X) | BIT_MASK(ABS_Y) }
	},
	/* Keypad */
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT_MASK(EV_KEY) }
	},
	{ }
};

static struct input_handler cpu_input_boost_input_handler = {
	.event		= cpu_input_boost_input_event,
	.connect	= cpu_input_boost_input_connect,
	.disconnect	= cpu_input_boost_input_disconnect,
	.name		= "cpu_input_boost_handler",
	.id_table	= cpu_input_boost_ids
};

static int __init cpu_input_boost_init(void)
{
	struct task_struct *boost_thread;
	struct boost_drv *b;
	int ret;

	b = kzalloc(sizeof(*b), GFP_KERNEL);
	if (!b)
		return -ENOMEM;

	INIT_DELAYED_WORK(&b->input_unboost, input_unboost_worker);
	INIT_DELAYED_WORK(&b->max_unboost, max_unboost_worker);
	init_waitqueue_head(&b->boost_waitq);
	b->input_boost_jifs = msecs_to_jiffies(input_boost_duration);
	b->wake_boost_jifs = msecs_to_jiffies(CONFIG_WAKE_BOOST_DURATION_MS);
	atomic64_set(&b->max_boost_expires, 0);
	atomic_set(&b->state, 0);

	b->cpu_notif.notifier_call = cpu_notifier_cb;
	ret = cpufreq_register_notifier(&b->cpu_notif, CPUFREQ_POLICY_NOTIFIER);
	if (ret) {
		pr_err("Failed to register cpufreq notifier, err: %d\n", ret);
		goto free_b;
	}

	cpu_input_boost_input_handler.private = b;
	ret = input_register_handler(&cpu_input_boost_input_handler);
	if (ret) {
		pr_err("Failed to register input handler, err: %d\n", ret);
		goto unregister_cpu_notif;
	}

	b->fb_notif.notifier_call = fb_notifier_cb;
	b->fb_notif.priority = INT_MAX;
	ret = fb_register_client(&b->fb_notif);
	if (ret) {
		pr_err("Failed to register fb notifier, err: %d\n", ret);
		goto unregister_handler;
	}

	boost_thread = kthread_run_perf_critical(cpu_boost_thread, b,
						 "cpu_boostd");
	if (IS_ERR(boost_thread)) {
		pr_err("Failed to start CPU boost thread, err: %ld\n",
		       PTR_ERR(boost_thread));
		goto unregister_fb_notif
	;
	}

	boost_drv_g = b;

	return 0;

unregister_fb_notif:
	fb_unregister_client(&b->fb_notif);
unregister_handler:
	input_unregister_handler(&cpu_input_boost_input_handler);
unregister_cpu_notif:
	cpufreq_unregister_notifier(&b->cpu_notif, CPUFREQ_POLICY_NOTIFIER);
free_b:
	kfree(b);
	return ret;
}
late_initcall(cpu_input_boost_init);
