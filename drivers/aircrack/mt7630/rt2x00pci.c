/*
	Copyright (C) 2004 - 2009 Ivo van Doorn <IvDoorn@gmail.com>
	<http://rt2x00.serialmonkey.com>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the
	Free Software Foundation, Inc.,
	59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
	Module: rt2x00pci
	Abstract: rt2x00 generic pci device routines.
 */

#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>

#include "rt2x00.h"
#include "rt2x00pci.h"

extern void MT76x0_WLAN_ChipOnOff(
	struct rt2x00_dev *rt2x00dev,
	int bOn,
	int bResetWLAN);


extern int rt2x00lib_initialize(struct rt2x00_dev *rt2x00dev);
/*
 * Register access.
 */
int rt2x00pci_regbusy_read(struct rt2x00_dev *rt2x00dev,
			   const unsigned int offset,
			   const struct rt2x00_field32 field,
			   u32 *reg)
{
	unsigned int i;

	if (!test_bit(DEVICE_STATE_PRESENT, &rt2x00dev->flags))
		return 0;

	for (i = 0; i < REGISTER_BUSY_COUNT; i++) {
		rt2x00pci_register_read(rt2x00dev, offset, reg);
		if (!rt2x00_get_field32(*reg, field))
			return 1;
		udelay(REGISTER_BUSY_DELAY);
	}

	ERROR(rt2x00dev, "Indirect register access failed: "
	      "offset=0x%.08x, value=0x%.08x\n", offset, *reg);
	*reg = ~0;

	return 0;
}
EXPORT_SYMBOL_GPL(rt2x00pci_regbusy_read);

bool rt2x00pci_rxdone(struct rt2x00_dev *rt2x00dev)
{
	struct data_queue *queue = rt2x00dev->rx;
	struct queue_entry *entry;
	struct queue_entry_priv_pci *entry_priv;
	struct skb_frame_desc *skbdesc;
	int max_rx = 16;

	while (--max_rx) {
		entry = rt2x00queue_get_entry(queue, Q_INDEX);
		
		entry_priv = entry->priv_data;

		if (rt2x00dev->ops->lib->get_entry_state(entry))
			break;

		//printk("get entry entry_idx=%d entry->skb->len=%d\n",entry->entry_idx,entry->skb->len);
		//printk("get entry entry_priv->desc_dma=0x%x\n",entry_priv->desc_dma);
		/*
		 * Fill in desc fields of the skb descriptor
		 */
		skbdesc = get_skb_frame_desc(entry->skb);
		skbdesc->desc = entry_priv->desc;
		skbdesc->desc_len = entry->queue->desc_size;

		/*
		 * DMA is already done, notify rt2x00lib that
		 * it finished successfully.
		 */
		rt2x00lib_dmastart(entry);
		rt2x00lib_dmadone(entry);

		/*
		 * Send the frame to rt2x00lib for further processing.
		 */
		rt2x00lib_rxdone(entry, GFP_ATOMIC);
	}

	return !max_rx;
}
EXPORT_SYMBOL_GPL(rt2x00pci_rxdone);

void rt2x00pci_flush_queue(struct data_queue *queue, bool drop)
{
	unsigned int i;
	printk("===>%s:MT7630\n", __FUNCTION__);
	for (i = 0; !rt2x00queue_empty(queue) && i < 10; i++)
		msleep(10);
}
EXPORT_SYMBOL_GPL(rt2x00pci_flush_queue);

/*
 * Device initialization handlers.
 */
static int rt2x00pci_alloc_queue_dma(struct rt2x00_dev *rt2x00dev,
				     struct data_queue *queue)
{
	struct queue_entry_priv_pci *entry_priv;
	void *addr;
	dma_addr_t dma;
	unsigned int i;
	u32 word;
	struct pci_dev *pci_dev = to_pci_dev(rt2x00dev->dev);

	/*
	 * Allocate DMA memory for descriptor and buffer.
	 */
#if 1
	addr = dma_alloc_coherent(rt2x00dev->dev,
				  queue->limit * queue->desc_size,
				  &dma, GFP_KERNEL);
#else
	addr = (void*)pci_alloc_consistent(pci_dev,
				  queue->limit * queue->desc_size,
				  &dma);
#endif
	if (!addr)
		return -ENOMEM;

	memset(addr, 0, queue->limit * queue->desc_size);

	/*
	 * Initialize all queue entries to contain valid addresses.
	 */
#if 0
		bool tx_queue =
		(queue->qid == QID_AC_VO) ||
		(queue->qid == QID_AC_VI) ||
		(queue->qid == QID_AC_BE) ||
		(queue->qid == QID_AC_BK);
#endif	
	for (i = 0; i < queue->limit; i++) {
		entry_priv = queue->entries[i].priv_data;
		entry_priv->desc = addr + i * queue->desc_size;
		entry_priv->desc_dma = dma + i * queue->desc_size;
			
		if (queue->qid==QID_RX)
		printk("[%d]entry_priv->desc=0x%x entry_priv->desc_dma=0x%x\n",i,entry_priv->desc,(uint)entry_priv->desc_dma);
	}

	return 0;
}

static void rt2x00pci_free_queue_dma(struct rt2x00_dev *rt2x00dev,
				     struct data_queue *queue)
{
	struct queue_entry_priv_pci *entry_priv =
	    queue->entries[0].priv_data;

	printk("rt2x00pci_free_queue_dma\n");
	if (entry_priv->desc)
		dma_free_coherent(rt2x00dev->dev,
				  queue->limit * queue->desc_size,
				  entry_priv->desc, entry_priv->desc_dma);
	entry_priv->desc = NULL;
}

int rt2x00pci_initialize(struct rt2x00_dev *rt2x00dev)
{
	struct data_queue *queue;
	int status;

	printk("===>%s:MT7630\n", __FUNCTION__);
	/*
	 * Allocate DMA
	 */
	queue_for_each(rt2x00dev, queue) {
		status = rt2x00pci_alloc_queue_dma(rt2x00dev, queue);
		if (status)
			goto exit;
	}

	rt2x00pci_register_write(rt2x00dev, 0x020c, 0xFFFFFFFF);
#if 0
	/*
	 * Register interrupt handler.
	 */
	status = request_irq(rt2x00dev->irq,
			     rt2x00dev->ops->lib->irq_handler,
			     IRQF_SHARED, rt2x00dev->name, rt2x00dev);
	if (status) {
		ERROR(rt2x00dev, "IRQ %d allocation failed (error %d).\n",
		      rt2x00dev->irq, status);
		goto exit;
	}
#endif
	return 0;

exit:
	queue_for_each(rt2x00dev, queue)
		rt2x00pci_free_queue_dma(rt2x00dev, queue);

	return status;
}
EXPORT_SYMBOL_GPL(rt2x00pci_initialize);

void rt2x00pci_uninitialize(struct rt2x00_dev *rt2x00dev)
{
	struct data_queue *queue;
	printk("===>%s:MT7630\n", __FUNCTION__);
	/*
	 * Free irq line.
	 */
	free_irq(rt2x00dev->irq, rt2x00dev);

	/*
	 * Free DMA
	 */
	queue_for_each(rt2x00dev, queue)
		rt2x00pci_free_queue_dma(rt2x00dev, queue);
}
EXPORT_SYMBOL_GPL(rt2x00pci_uninitialize);

/*
 * PCI driver handlers.
 */
static void rt2x00pci_free_reg(struct rt2x00_dev *rt2x00dev)
{
	kfree(rt2x00dev->rf);
	rt2x00dev->rf = NULL;

	kfree(rt2x00dev->eeprom);
	rt2x00dev->eeprom = NULL;

	if (rt2x00dev->csr.base) {
		iounmap(rt2x00dev->csr.base);
		rt2x00dev->csr.base = NULL;
	}
}

static int rt2x00pci_alloc_reg(struct rt2x00_dev *rt2x00dev)
{
	struct pci_dev *pci_dev = to_pci_dev(rt2x00dev->dev);

	printk("pci_resource_len = 0x%x\n",(uint)pci_resource_len(pci_dev, 0));
	rt2x00dev->csr.base = pci_ioremap_bar(pci_dev, 0);
	//rt2x00dev->csr.base = (unsigned long) ioremap(pci_resource_start(pci_dev, 0), pci_resource_len(pci_dev, 0));
#if 0
	printk("%s: at 0x%lx, VA 0x%lx, IRQ %d. \n",  rt2x00dev->name, 
					(unsigned long)pci_resource_start(pci_dev, 0), (unsigned long)rt2x00dev->csr.base, pci_dev->irq);
	//csr_addr = (unsigned long) ioremap(pci_resource_start(pdev, 0), pci_resource_len(pdev, 0));
#endif
	if (!rt2x00dev->csr.base)
		goto exit;

	rt2x00dev->eeprom = kzalloc(rt2x00dev->ops->eeprom_size, GFP_KERNEL);
	if (!rt2x00dev->eeprom)
		goto exit;

	rt2x00dev->rf = kzalloc(rt2x00dev->ops->rf_size, GFP_KERNEL);
	if (!rt2x00dev->rf)
		goto exit;

	return 0;

exit:
	ERROR_PROBE("Failed to allocate registers.\n");

	rt2x00pci_free_reg(rt2x00dev);

	return -ENOMEM;
}

int rt2x00pci_probe(struct pci_dev *pci_dev, const struct rt2x00_ops *ops)
{
	struct ieee80211_hw *hw;
	struct rt2x00_dev *rt2x00dev;
	int retval;
	u16 chip;

	retval = pci_enable_device(pci_dev);
	if (retval) {
		ERROR_PROBE("Enable device failed.\n");
		return retval;
	}

	retval = pci_request_regions(pci_dev, pci_name(pci_dev));
	if (retval) {
		ERROR_PROBE("PCI request regions failed.\n");
		goto exit_disable_device;
	}

	pci_set_master(pci_dev);
	
	if (pci_set_mwi(pci_dev))
		ERROR_PROBE("MWI not available.\n");

	if (dma_set_mask(&pci_dev->dev, DMA_BIT_MASK(32))) {
		ERROR_PROBE("PCI DMA not supported.\n");
		retval = -EIO;
		goto exit_release_regions;
	}
	hw = ieee80211_alloc_hw(sizeof(struct rt2x00_dev), ops->hw);
	if (!hw) {
		ERROR_PROBE("Failed to allocate hardware.\n");
		retval = -ENOMEM;
		goto exit_release_regions;
	}

	pci_set_drvdata(pci_dev, hw);

	rt2x00dev = hw->priv;
	rt2x00dev->dev = &pci_dev->dev;
	rt2x00dev->ops = ops;
	rt2x00dev->hw = hw;
	rt2x00dev->irq = pci_dev->irq;
	rt2x00dev->name = pci_name(pci_dev);

	if (pci_is_pcie(pci_dev))
		rt2x00_set_chip_intf(rt2x00dev, RT2X00_CHIP_INTF_PCIE);
	else
		rt2x00_set_chip_intf(rt2x00dev, RT2X00_CHIP_INTF_PCI);


	
	retval = rt2x00pci_alloc_reg(rt2x00dev);
	
	//printk("rt2x00dev->csr.base =0x%x\n",rt2x00dev->csr.base);
	if (retval)
		goto exit_free_device;

	

	/*
	 * Because rt3290 chip use different efuse offset to read efuse data.
	 * So before read efuse it need to indicate it is the
	 * rt3290 or not.
	 */
	pci_read_config_word(pci_dev, PCI_DEVICE_ID, &chip);


	rt2x00dev->chip.rt=chip;

	if (rt2x00_rt(rt2x00dev, MT7630))
		MT76x0_WLAN_ChipOnOff(rt2x00dev, 1, 1);
	
	retval = rt2x00lib_probe_dev(rt2x00dev);
	if (retval)
		goto exit_free_reg;

#if 0

#endif
	if (rt2x00_rt(rt2x00dev, MT7630))
	{

		u32 MacValue;
		u32 tmp=0;

		rt2x00pci_register_read(rt2x00dev, 0x0000, &MacValue);
		printk("MacIcVersion = 0x%x\n",MacValue);
#if 0		
		pci_read_config_dword((struct pci_dev *)pci_dev, 0x71c,&tmp);
		printk("read pci config 0x%x \n",tmp);
		tmp |= 0x17e00000;
		pci_write_config_dword((struct pci_dev *)pci_dev, 0x71c,tmp);
		printk("write pci config to 0x%x \n",tmp);
#endif
		rt2x00dev->bscan=0;
	}
	
	return 0;

exit_free_reg:
	rt2x00pci_free_reg(rt2x00dev);

exit_free_device:
	ieee80211_free_hw(hw);

exit_release_regions:
	pci_release_regions(pci_dev);

exit_disable_device:
	pci_disable_device(pci_dev);

	pci_set_drvdata(pci_dev, NULL);

	return retval;
}
EXPORT_SYMBOL_GPL(rt2x00pci_probe);

void rt2x00pci_remove(struct pci_dev *pci_dev)
{
	struct ieee80211_hw *hw = pci_get_drvdata(pci_dev);
	struct rt2x00_dev *rt2x00dev = hw->priv;

	/*
	 * Free all allocated data.
	 */
	rt2x00lib_remove_dev(rt2x00dev);
	rt2x00pci_free_reg(rt2x00dev);
	ieee80211_free_hw(hw);

	/*
	 * Free the PCI device data.
	 */
	pci_set_drvdata(pci_dev, NULL);
	pci_disable_device(pci_dev);
	pci_release_regions(pci_dev);
}
EXPORT_SYMBOL_GPL(rt2x00pci_remove);

#ifdef CONFIG_PM
int rt2x00pci_suspend(struct pci_dev *pci_dev, pm_message_t state)
{
	struct ieee80211_hw *hw = pci_get_drvdata(pci_dev);
	struct rt2x00_dev *rt2x00dev = hw->priv;
	int retval;

	retval = rt2x00lib_suspend(rt2x00dev, state);
	if (retval)
		return retval;

	pci_save_state(pci_dev);
	pci_disable_device(pci_dev);
	return pci_set_power_state(pci_dev, pci_choose_state(pci_dev, state));
}
EXPORT_SYMBOL_GPL(rt2x00pci_suspend);

int rt2x00pci_resume(struct pci_dev *pci_dev)
{
	struct ieee80211_hw *hw = pci_get_drvdata(pci_dev);
	struct rt2x00_dev *rt2x00dev = hw->priv;

	if (pci_set_power_state(pci_dev, PCI_D0) ||
	    pci_enable_device(pci_dev)) {
		ERROR(rt2x00dev, "Failed to resume device.\n");
		return -EIO;
	}

	pci_restore_state(pci_dev);
	return rt2x00lib_resume(rt2x00dev);
}
EXPORT_SYMBOL_GPL(rt2x00pci_resume);
#endif /* CONFIG_PM */

/*
 * rt2x00pci module information.
 */
MODULE_AUTHOR(DRV_PROJECT);
MODULE_VERSION(DRV_VERSION);
MODULE_DESCRIPTION("rt2x00 pci library");
MODULE_LICENSE("GPL");
