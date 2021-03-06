/*
 * ehci-omap.h - register definitions for USBHOST in OMAP 34xx
 *
 * Copyright (C) 2007-2008 Texas Instruments, Inc.
 * 	Author: Vikram Pandita <vikram.pandita@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#ifndef __EHCI_OMAP_H
#define __EHCI_OMAP_H

#include <asm/hardware.h>
#include "../../../arch/arm/mach-omap2/cm.h"
#include "../../../arch/arm/mach-omap2/cm_regbits_34xx.h"

/*
 * OMAP USBHOST Register addresses: PHYSICAL ADDRESSES
 * 	Use omap_readl()/omap_writel() functions
 */

/* USBHOST: TLL, UUH, OHCI, EHCI */
#define	OMAP_USBHOST_BASE	(L4_34XX_BASE + 0x60000)

/* TLL Register Set */
#define	OMAP_USBHOST_TLL_BASE	(OMAP_USBHOST_BASE + 0x2000)
#define	OMAP_USBTLL_REVISION	(OMAP_USBHOST_TLL_BASE + 0x00)
#define	OMAP_USBTLL_SYSCONFIG	(OMAP_USBHOST_TLL_BASE + 0x10)
	#define	OMAP_USBTLL_SYSCONFIG_CACTIVITY_SHIFT	8
	#define	OMAP_USBTLL_SYSCONFIG_SIDLEMODE_SHIFT	3
	#define	OMAP_USBTLL_SYSCONFIG_ENAWAKEUP_SHIFT	2
	#define	OMAP_USBTLL_SYSCONFIG_SOFTRESET_SHIFT	1
	#define	OMAP_USBTLL_SYSCONFIG_AUTOIDLE_SHIFT	0
#define	OMAP_USBTLL_SYSSTATUS	(OMAP_USBHOST_TLL_BASE + 0x14)
	#define	OMAP_USBTLL_SYSSTATUS_RESETDONE_SHIFT	0
#define	OMAP_USBTLL_IRQSTATUS	(OMAP_USBHOST_TLL_BASE + 0x18)
#define	OMAP_USBTLL_IRQENABLE	(OMAP_USBHOST_TLL_BASE + 0x1C)

#define	OMAP_TLL_SHARED_CONF	(OMAP_USBHOST_TLL_BASE + 0x30)
	#define	OMAP_TLL_SHARED_CONF_USB_90D_DDR_EN_SHFT	6
	#define	OMAP_TLL_SHARED_CONF_USB_180D_SDR_EN_SHIFT	5
	#define	OMAP_TLL_SHARED_CONF_USB_DIVRATION_SHIFT	2
	#define	OMAP_TLL_SHARED_CONF_FCLK_REQ_SHIFT		1
	#define	OMAP_TLL_SHARED_CONF_FCLK_IS_ON_SHIFT		0

#define	OMAP_TLL_CHANNEL_CONF(num)\
			(OMAP_USBHOST_TLL_BASE + (0x040 + 0x004 * num))
	#define	OMAP_TLL_CHANNEL_CONF_ULPINOBITSTUFF_SHIFT	11
	#define	OMAP_TLL_CHANNEL_CONF_ULPI_ULPIAUTOIDLE_SHIFT	10
	#define	OMAP_TLL_CHANNEL_CONF_UTMIAUTOIDLE_SHIFT	9
	#define	OMAP_TLL_CHANNEL_CONF_ULPIDDRMODE_SHIFT		8
	#define	OMAP_TLL_CHANNEL_CONF_CHANEN_SHIFT		0

#define	OMAP_TLL_ULPI_FUNCTION_CTRL(num)\
			(OMAP_USBHOST_TLL_BASE + (0x804 + 0x100 * num))
#define	OMAP_TLL_ULPI_INTERFACE_CTRL(num)\
			(OMAP_USBHOST_TLL_BASE + (0x807 + 0x100 * num))
#define	OMAP_TLL_ULPI_OTG_CTRL(num)\
			(OMAP_USBHOST_TLL_BASE + (0x80A + 0x100 * num))
#define	OMAP_TLL_ULPI_INT_EN_RISE(num)\
			(OMAP_USBHOST_TLL_BASE + (0x80D + 0x100 * num))
#define	OMAP_TLL_ULPI_INT_EN_FALL(num)\
			(OMAP_USBHOST_TLL_BASE + (0x810 + 0x100 * num))
#define	OMAP_TLL_ULPI_INT_STATUS(num)\
			(OMAP_USBHOST_TLL_BASE + (0x813 + 0x100 * num))
#define	OMAP_TLL_ULPI_INT_LATCH(num)\
			(OMAP_USBHOST_TLL_BASE + (0x814 + 0x100 * num))
#define	OMAP_TLL_ULPI_DEBUG(num)\
			(OMAP_USBHOST_TLL_BASE + (0x815 + 0x100 * num))
#define	OMAP_TLL_ULPI_SCRATCH_REGISTER(num)\
			(OMAP_USBHOST_TLL_BASE + (0x816 + 0x100 * num))

#define OMAP_TLL_CHANNEL_COUNT		3
	#define OMAP_TLL_CHANNEL_1_EN_MASK	1
	#define OMAP_TLL_CHANNEL_2_EN_MASK	2
	#define OMAP_TLL_CHANNEL_3_EN_MASK	4

/* UHH Register Set */
#define	OMAP_USBHOST_UHH_BASE	(OMAP_USBHOST_BASE + 0x4000)
#define	OMAP_UHH_REVISION	(OMAP_USBHOST_UHH_BASE + 0x00)
#define	OMAP_UHH_SYSCONFIG	(OMAP_USBHOST_UHH_BASE + 0x10)
	#define	OMAP_UHH_SYSCONFIG_MIDLEMODE_SHIFT	12
	#define	OMAP_UHH_SYSCONFIG_CACTIVITY_SHIFT	8
	#define	OMAP_UHH_SYSCONFIG_SIDLEMODE_SHIFT	3
	#define	OMAP_UHH_SYSCONFIG_ENAWAKEUP_SHIFT	2
	#define	OMAP_UHH_SYSCONFIG_SOFTRESET_SHIFT	1
	#define	OMAP_UHH_SYSCONFIG_AUTOIDLE_SHIFT	0

#define	OMAP_UHH_SYSSTATUS	(OMAP_USBHOST_UHH_BASE + 0x14)
#define	OMAP_UHH_HOSTCONFIG	(OMAP_USBHOST_UHH_BASE + 0x40)
	#define	OMAP_UHH_HOSTCONFIG_ULPI_BYPASS_SHIFT	0
	#define OMAP_UHH_HOSTCONFIG_INCR4_BURST_EN_SHIFT	2
	#define OMAP_UHH_HOSTCONFIG_INCR8_BURST_EN_SHIFT	3
	#define OMAP_UHH_HOSTCONFIG_INCR16_BURST_EN_SHIFT	4
	#define OMAP_UHH_HOSTCONFIG_INCRX_ALIGN_EN_SHIFT	5

#define	OMAP_UHH_DEBUG_CSR	(OMAP_USBHOST_UHH_BASE + 0x44)

/* EHCI Register Set */
#define	OMAP_USBHOST_EHCI_BASE	(OMAP_USBHOST_BASE + 0x4800)
#define	EHCI_INSNREG05_ULPI		(OMAP_USBHOST_EHCI_BASE + 0xA4)
	#define	EHCI_INSNREG05_ULPI_CONTROL_SHIFT	31
	#define	EHCI_INSNREG05_ULPI_PORTSEL_SHIFT	24
	#define	EHCI_INSNREG05_ULPI_OPSEL_SHIFT		22
	#define	EHCI_INSNREG05_ULPI_REGADD_SHIFT	16
	#define	EHCI_INSNREG05_ULPI_EXTREGADD_SHIFT	8
	#define	EHCI_INSNREG05_ULPI_WRDATA_SHIFT	0

/* OHCI Register Set */
#define	OMAP_USBHOST_OHCI_BASE	(OMAP_USBHOST_BASE + 0x4400)

#endif/* __EHCI_OMAP_H*/
