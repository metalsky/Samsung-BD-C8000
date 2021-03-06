#
# Makefile for the linux kernel.
#

# Common support
obj-y := irq.o id.o io.o memory.o control.o mux.o \
		devices.o serial.o gpmc.o timer-gp.o

ifeq ($(CONFIG_MACH_OMAP_2430SDP),y)
obj-y += sram-fn.o prcm.o clock.o

# Power Management
obj-$(CONFIG_PM) += pm.o sleep.o

# Clock framework
obj-$(CONFIG_ARCH_OMAP2)		+= clock24xx.o
obj-$(CONFIG_ARCH_OMAP3)		+= clock34xx.o
endif

# DSP
obj-$(CONFIG_OMAP_MMU_FWK)	+= mmu_mach.o
obj-$(CONFIG_OMAP_MBOX_FWK)	+= mailbox_mach.o
mailbox_mach-objs		:= mailbox.o
mmu_mach-objs			:= mmu.o

# Specific board support
obj-$(CONFIG_ARCH_OMAP34XX) 		+= pm_34xx.o \
					   sleep_34xx.o \
					   prcm-debug.o \
					   sram-fn_34xx.o \
					   clock_34xx.o \
					   resource_34xx.o \
					   prcm_34xx.o \
					   smartreflex.o
obj-$(CONFIG_MACH_OMAP_GENERIC)		+= board-generic.o
obj-$(CONFIG_MACH_OMAP_H4)		+= board-h4.o board-h4-mmc.o
obj-$(CONFIG_MACH_OMAP_2430SDP)		+= board-2430sdp.o \
					   board-2430sdp-flash.o \
					   hsmmc.o \
					   usb-musb.o \
					   usb-ehci.o
obj-$(CONFIG_MACH_OMAP_2430OSK)		+= board-2430osk.o
obj-$(CONFIG_MACH_OMAP_3430SDP)		+= board-3430sdp.o \
					   hsmmc.o \
					   usb-musb.o \
					   usb-ehci.o \
					   board-3430sdp-flash.o
obj-$(CONFIG_MACH_OMAP3EVM)		+= board-omap3evm.o \
					   hsmmc.o \
					   usb-musb.o usb-ehci.o \
					   board-omap3evm-flash.o
obj-$(CONFIG_MACH_OMAP3_BEAGLE)		+= board-omap3beagle.o \
					   board-omap3beagle-flash.o \
					   usb-musb.o usb-ehci.o \
					   hsmmc.o
obj-$(CONFIG_MACH_OMAP_LDP)		+= board-ldp.o \
					   board-ldp-flash.o \
					   board-ldp-usb.o \
					   board-ldp-hsmmc.o
obj-$(CONFIG_MACH_OMAP_APOLLON)		+= board-apollon.o \
					   board-apollon-mmc.o	\
					   board-apollon-keys.o
obj-$(CONFIG_MACH_NOKIA_N800)		+= board-n800.o board-n800-flash.o \
					   board-n800-mmc.o board-n800-bt.o \
					   board-n800-audio.o board-n800-usb.o \
					   board-n800-dsp.o \
					   board-n800-camera.o
obj-$(CONFIG_MACH_NOKIA_N810)		+= board-n810.o

# TUSB 6010 chips
obj-$(CONFIG_MACH_OMAP2_TUSB6010)	+= usb-tusb6010.o

