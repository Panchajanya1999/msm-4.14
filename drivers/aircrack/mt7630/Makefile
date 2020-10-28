EXTRA_CFLAGS += $(USER_EXTRA_CFLAGS)
EXTRA_CFLAGS += -O3

EXTRA_CFLAGS += -Wno-unused-variable
EXTRA_CFLAGS += -Wno-unused-value
EXTRA_CFLAGS += -Wno-unused-label
EXTRA_CFLAGS += -Wno-unused-parameter
EXTRA_CFLAGS += -Wno-parentheses-equality
EXTRA_CFLAGS += -Wno-constant-conversion
EXTRA_CFLAGS += -Wno-enum-conversion
EXTRA_CFLAGS += -Wno-unused-function
EXTRA_CFLAGS += -Wno-unused

EXTRA_CFLAGS += -Wno-uninitialized

EXTRA_CFLAGS += -I$(src)/include

CONFIG_PLATFORM_ANDROID_ARM64 = y

ifeq ($(CONFIG_PLATFORM_ANDROID_ARM64), y)
# For this to work, change the "modules:" section is also needed, in order to build with CLANG.
# "$(MAKE) ARCH=$(ARCH) SUBARCH=$(ARCH) REAL_CC=${CC_DIR}/clang CLANG_TRIPLE=aarch64-linux-gnu- CROSS_COMPILE=$(CROSS_COMPILE) -C $(KSRC) M=$(shell pwd) O="$(KBUILD_OUTPUT)" modules"
EXTRA_CFLAGS += -DCONFIG_LITTLE_ENDIAN -fno-pic
EXTRA_CFLAGS += -DRTW_ENABLE_WIFI_CONTROL_FUNC -DCONFIG_RADIO_WORK
#Enable this to have two interfaces:
#EXTRA_CFLAGS += -DCONFIG_CONCURRENT_MODE
EXTRA_CFLAGS += -DCONFIG_IOCTL_CFG80211
EXTRA_CFLAGS += -DRTW_USE_CFG80211_STA_EVENT
EXTRA_CFLAGS += -DCONFIG_P2P_IPS
endif

SUBARCH := $(shell uname -m | sed -e s/i.86/i386/ | sed -e s/ppc/powerpc/)
ARCH ?= $(SUBARCH)
CROSS_COMPILE ?=
KVER  := $(shell uname -r)
KSRC := /lib/modules/$(KVER)/build
MODDESTDIR := /lib/modules/$(KVER)/kernel/drivers/net/wireless/
INSTALL_PREFIX :=

ifneq ($(KERNELRELEASE),)
rt2x00lib-y				+= rt2x00dev.o
rt2x00lib-y				+= rt2x00mac.o
rt2x00lib-y				+= rt2x00config.o
rt2x00lib-y				+= rt2x00queue.o
rt2x00lib-y				+= rt2x00link.o
#rt2x00lib-y				+= rt2x00debug.o
rt2x00lib-y				+= rt2x00crypto.o
rt2x00lib-y				+= rt2x00firmware.o
rt2x00lib-y				+= rt2x00leds.o

obj-m					+= rt2x00lib.o
obj-m					+= rt2x00pci.o
obj-m					+= rt2800lib.o
obj-m					+= rt2800pci.o

else

all: modules

modules:
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KSRC) M=$(shell pwd)  modules

install:
	install -p -m 644 rt2800lib.ko  $(MODDESTDIR)
	install -p -m 644 rt2800pci.ko  $(MODDESTDIR)
	install -p -m 644 rt2x00lib.ko  $(MODDESTDIR)
	install -p -m 644 rt2x00pci.ko  $(MODDESTDIR)
	cp -p mt7630.bin /lib/modules/$(KVER)/.
	/sbin/depmod -a ${KVER}

uninstall:
	rm -f $(MODDESTDIR)/rt2800lib.ko
	rm -f $(MODDESTDIR)/rt2800pci.ko
	rm -f $(MODDESTDIR)/rt2x00lib.ko
	rm -f $(MODDESTDIR)/rt2x00pci.ko
	/sbin/depmod -a ${KVER}

.PHONY: modules clean

clean: $(clean_more)
	rm -fr *.mod.c *.mod *.o .*.cmd *.ko *~
	rm -fr .tmp_versions
	rm -fr Module.symvers ; rm -fr Module.markers ; rm -fr modules.order
endif

