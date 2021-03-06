#
# Makefile for the linux kernel.
#

# Common support
obj-y := common.o sram-fn.o clock.o devices.o dma.o mux.o gpio.o \
	 usb.o fb.o
obj-m :=
obj-n :=
obj-  :=

ifeq ($(CONFIG_ARCH_OMAP34XX),y)
obj-y += sram_ti.o
else
obj-y += sram.o
endif

# OCPI interconnect support for 1710, 1610 and 5912
obj-$(CONFIG_ARCH_OMAP16XX) += ocpi.o

obj-$(CONFIG_OMAP_MCBSP) += mcbsp.o

# STI support
obj-$(CONFIG_OMAP_STI) += sti/

obj-$(CONFIG_ARCH_OMAP3) += display.o

obj-$(CONFIG_CPU_FREQ) += cpu-omap.o
obj-$(CONFIG_OMAP_DM_TIMER) += dmtimer.o
obj-$(CONFIG_OMAP_BOOT_REASON) += bootreason.o
obj-$(CONFIG_OMAP_COMPONENT_VERSION) += component-version.o
obj-$(CONFIG_OMAP_GPIO_SWITCH) += gpio-switch.o
obj-$(CONFIG_OMAP_DEBUG_DEVICES) += debug-devices.o
obj-$(CONFIG_OMAP_DEBUG_LEDS) += debug-leds.o
obj-$(CONFIG_I2C_OMAP) += i2c.o
obj-$(CONFIG_HS_SERIAL_SUPPORT) += omap24xx-uart.o

# OMAP MMU framework
obj-$(CONFIG_OMAP_MMU_FWK) += mmu.o

# OMAP mailbox framework
obj-$(CONFIG_OMAP_MBOX_FWK) += mailbox.o

