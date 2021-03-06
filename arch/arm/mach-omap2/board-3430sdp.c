/*
 * linux/arch/arm/mach-omap2/board-3430sdp.c
 *
 * Copyright (C) 2007 Texas Instruments
 *
 * Modified from mach-omap2/board-generic.c
 *
 * Initial code: Syed Mohammed Khasim
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/spi/spi.h>
#include <linux/spi/ads7846.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <asm/arch/twl4030.h>
#include <asm/arch/mcspi.h>
#include <asm/arch/gpio.h>
#include <asm/arch/mux.h>
#include <asm/arch/board.h>
#include <asm/arch/usb-musb.h>
#include <asm/arch/usb-ehci.h>
#include <asm/arch/hsmmc.h>
#include <asm/arch/common.h>
#include <asm/arch/keypad.h>
#include <asm/arch/dma.h>
#include <asm/arch/gpmc.h>
#include <asm/arch/twl4030-rtc.h>
#include <asm/arch/power_companion.h>

#include <asm/io.h>
#include <asm/delay.h>
#include <asm/arch/control.h>

#include "ti-compat.h"
#define CONTROL_SYSC_SMARTIDLE	(0x2 << 3)
#define CONTROL_SYSC_AUTOIDLE	(0x1)

#define	SDP3430_SMC91X_CS	3
#define PRCM_INTERRUPT_MASK	(1 << 11)
#define UART1_INTERRUPT_MASK	(1 << 8)
#define UART2_INTERRUPT_MASK    (1 << 9)
#define UART3_INTERRUPT_MASK    (1 << 10)

#define ENABLE_VAUX3_DEDICATED	0x03
#define ENABLE_VAUX3_DEV_GRP	0x20


#define TWL4030_MSECURE_GPIO	22
#include "prcm-regs.h"

unsigned int uart_interrupt_mask_value;
static struct resource sdp3430_smc91x_resources[] = {
	[0] = {
		.start	= OMAP34XX_ETHR_START,
		.end	= OMAP34XX_ETHR_START + SZ_4K,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 0,
		.end	= 0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device sdp3430_smc91x_device = {
	.name		= "smc91x",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(sdp3430_smc91x_resources),
	.resource	= sdp3430_smc91x_resources,
};
extern int console_detect(char *str);
static u32 *uart_detect(void){
	char str[7];
	u32 *temp_ptr = 0;
	if (console_detect(str))
		printk(KERN_INFO"Invalid console paramter....\n");

	if (!strcmp(str, "ttyS0")) {
		temp_ptr = (u32 *)(&CONTROL_PADCONF_UART1_CTS);
		uart_interrupt_mask_value = UART1_INTERRUPT_MASK;
		}
	else if (!strcmp(str, "ttyS1")) {
		temp_ptr = (u32 *)(&CONTROL_PADCONF_UART2_TX);
		uart_interrupt_mask_value = UART2_INTERRUPT_MASK;
		}
	else if (!strcmp(str, "ttyS2")) {
		temp_ptr = (u32 *)(&CONTROL_PADCONF_UART3_RTS_SD);
		uart_interrupt_mask_value = UART3_INTERRUPT_MASK;
		}
	else
		printk(KERN_INFO
		"!!!!!!!!! Unable to recongnize Console UART........\n");
	return (u32 *)(temp_ptr);
}
void  init_wakeupconfig(void){
	u32 *ptr;
	ptr = (u32 *)uart_detect();
	*ptr |= (u32)((IO_PAD_WAKEUPENABLE | IO_PAD_OFFPULLUDENABLE |
			IO_PAD_OFFOUTENABLE | IO_PAD_OFFENABLE |
			IO_PAD_INPUTENABLE | IO_PAD_PULLUDENABLE)
			<<  IO_PAD_HIGH_SHIFT);

}

void uart_padconf_control(void){
	u32 *ptr;
	ptr = (u32 *)uart_detect();
		*ptr |= (u32)((IO_PAD_WAKEUPENABLE)
			 << IO_PAD_HIGH_SHIFT);
}
void setup_board_wakeup_source(u32 wakeup_source)
{
	if ((wakeup_source & PRCM_WAKEUP_T2_KEYPAD) ||
		(wakeup_source & PRCM_WAKEUP_TOUCHSCREEN)) {
		PRCM_GPIO1_SYSCONFIG = 0x15;
		PM_WKEN_WKUP |= 0x8;
		PM_MPUGRPSEL_WKUP = 0x8;
		/* Unmask GPIO interrupt*/
		INTC_MIR_0 = ~((1<<29));
	}
	if (wakeup_source & PRCM_WAKEUP_T2_KEYPAD) {
		CONTROL_PADCONF_SYS_NIRQ &= 0xFFFFFFF8;
		CONTROL_PADCONF_SYS_NIRQ |= 0x4;
		GPIO1_SETIRQENABLE1 |= 0x1;
		GPIO1_SETWKUENA |= 0x1;
		GPIO1_FALLINGDETECT |= 0x1;
	}
	/* Unmasking the PRCM interrupts */
	INTC_MIR_0 &= ~PRCM_INTERRUPT_MASK;
	 /* Unmasking the UART interrupts */
	INTC_MIR_2 = ~uart_interrupt_mask_value;
}

static void scm_clk_init(void)
{
	struct clk *p_omap_ctrl_clk = NULL;

	p_omap_ctrl_clk = clk_get (NULL, "omapctrl_ick");
	if (p_omap_ctrl_clk != NULL) {
		if (clk_enable(p_omap_ctrl_clk)	!= 0) {
			printk(KERN_ERR "failed to enable scm clks\n");
			clk_put(p_omap_ctrl_clk);
		}
	}
	/* Sysconfig set to SMARTIDLE and AUTOIDLE */ 
	CONTROL_SYSCONFIG = (CONTROL_SYSC_SMARTIDLE | CONTROL_SYSC_AUTOIDLE);
}

static int sdp3430_keymap[] = {
	KEY(0, 0, KEY_LEFT),
	KEY(0, 1, KEY_RIGHT),
	KEY(0, 2, KEY_A),
	KEY(0, 3, KEY_B),
	KEY(0, 4, KEY_C),
	KEY(1, 0, KEY_DOWN),
	KEY(1, 1, KEY_UP),
	KEY(1, 2, KEY_E),
	KEY(1, 3, KEY_F),
	KEY(1, 4, KEY_G),
	KEY(2, 0, KEY_ENTER),
	KEY(2, 1, KEY_I),
	KEY(2, 2, KEY_J),
	KEY(2, 3, KEY_K),
	KEY(2, 4, KEY_3),
	KEY(3, 0, KEY_M),
	KEY(3, 1, KEY_N),
	KEY(3, 2, KEY_O),
	KEY(3, 3, KEY_P),
	KEY(3, 4, KEY_Q),
	KEY(4, 0, KEY_R),
	KEY(4, 1, KEY_4),
	KEY(4, 2, KEY_T),
	KEY(4, 3, KEY_U),
	KEY(4, 4, KEY_D),
	KEY(5, 0, KEY_V),
	KEY(5, 1, KEY_W),
	KEY(5, 2, KEY_L),
	KEY(5, 3, KEY_S),
	KEY(5, 4, KEY_H),
	0
};

static struct omap_kp_platform_data sdp3430_kp_data = {
	.rows		= 5,
	.cols		= 6,
	.keymap 	= sdp3430_keymap,
	.keymapsize 	= ARRAY_SIZE(sdp3430_keymap),
	.rep		= 1,
};

static struct platform_device sdp3430_kp_device = {
	.name		= "omap_twl4030keypad",
	.id		= -1,
	.dev		= {
		.platform_data	= &sdp3430_kp_data,
	},
};

static int ts_gpio;

#if defined(CONFIG_RTC_DRV_TWL4030) || defined (CONFIG_RTC_DRV_TWL4030_MODULE)
static int twl4030_rtc_init(void)
{
	int ret = 0;

	/* 3430ES2.0 doesn't have msecure/gpio-22 line connected to T2 */
	if (is_device_type_gp() && is_sil_rev_less_than(OMAP3430_REV_ES2_0)) {
		u32 msecure_pad_config_reg = omap_ctrl_base_get() + 0xA3C;
		int mux_mask = 0x04;
		u16 tmp;

		ret = omap_request_gpio(TWL4030_MSECURE_GPIO);
		if (ret < 0) {
			printk(KERN_ERR "twl4030_rtc_init: can't"
				"reserve GPIO:%d !\n", TWL4030_MSECURE_GPIO);
			goto out;
		}
		/*
		 * TWL4030 will be in secure mode if msecure line from OMAP
		 * is low. Make msecure line high in order to change the
		 * TWL4030 RTC time and calender registers.
		 */
		omap_set_gpio_direction(TWL4030_MSECURE_GPIO, 0);

		tmp = omap_readw(msecure_pad_config_reg);
		tmp &= 0xF8;	/* To enable mux mode 03/04 = GPIO_RTC */
		tmp |= mux_mask;/* To enable mux mode 03/04 = GPIO_RTC */
		omap_writew(tmp, msecure_pad_config_reg);

		omap_set_gpio_dataout(TWL4030_MSECURE_GPIO, 1);
	}
out:
	return ret;
}

static void twl4030_rtc_exit(void)
{
	if (is_device_type_gp() && is_sil_rev_less_than(OMAP3430_REV_ES2_0))
	{
	    omap_free_gpio(TWL4030_MSECURE_GPIO);
	}
}

static struct twl4030rtc_platform_data sdp3430_twl4030rtc_data = {
	.init = &twl4030_rtc_init,
	.exit = &twl4030_rtc_exit,
};

static struct platform_device sdp3430_twl4030rtc_device = {
	.name		= "twl4030_rtc",
	.id		= -1,
	.dev		= {
		.platform_data	= &sdp3430_twl4030rtc_data,
	},
};
#endif

/**
 * @brief ads7846_dev_init : Requests & sets GPIO line for pen-irq
 *
 * @return - void. If request gpio fails then Flag KERN_ERR.
 */
static void ads7846_dev_init(void)
{
	if (omap_request_gpio(ts_gpio) < 0) {
		printk(KERN_ERR "can't get ads746 pen down GPIO\n");
		return;
	}

	omap_set_gpio_direction(ts_gpio, 1);

	omap_set_gpio_debounce(ts_gpio, 1);
	omap_set_gpio_debounce_time(ts_gpio, 0xa);
}

static int ads7846_get_pendown_state(void)
{
	return !omap_get_gpio_datain(ts_gpio);
}

/*
 * This enable(1)/disable(0) the voltage for TS: uses twl4030 calls
 */
static int ads7846_vaux_control(int vaux_cntrl)
{
	int ret = 0;

#ifdef CONFIG_TWL4030_CORE
	/* check for return value of ldo_use: if success it returns 0*/
	if (vaux_cntrl == VAUX_ENABLE)
		ret = twl4030_vaux3_ldo_use();
	else if (vaux_cntrl == VAUX_DISABLE)
		ret = twl4030_vaux3_ldo_unuse();
#else
	ret = -EIO;
#endif
	return ret;
}

static struct ads7846_platform_data tsc2046_config = {
	.x_max			= 0x0fff,
	.y_max			= 0x0fff,
	.x_plate_ohms		= 180,
	.pressure_max		= 255,
	.debounce_max		= 18,
	.debounce_tol		= 8,
	.debounce_rep		= 1,
	.settle_delay_usecs	= 75,
	.get_pendown_state	= ads7846_get_pendown_state,
	.keep_vref_on		= 1,
	.vaux_control		= ads7846_vaux_control,
};


static struct omap2_mcspi_device_config tsc2046_mcspi_config = {
	.turbo_mode	= 0,
	.single_channel	= 1,  /* 0: slave, 1: master */
};

static struct spi_board_info sdp3430_spi_board_info[] __initdata = {
	[0] = {
		/*
		 * TSC2046 operates at a max freqency of 2MHz, so
		 * operate slightly below at 1.5MHz
		 */
		.modalias		= "ads7846",
		.bus_num		= 1,
		.chip_select		= 0,
		.max_speed_hz		= 1500000,
		.controller_data	= &tsc2046_mcspi_config,
		.irq			= 0,
		.platform_data		= &tsc2046_config,
	},
};

static struct platform_device sdp3430_lcd_device = {
	.name		= "sdp2430_lcd",
	.id		= -1,
};

static struct platform_device *sdp3430_devices[] __initdata = {
	&sdp3430_smc91x_device,
	&sdp3430_kp_device,
	&sdp3430_lcd_device,
#if defined(CONFIG_RTC_DRV_TWL4030) || defined (CONFIG_RTC_DRV_TWL4030_MODULE)
	&sdp3430_twl4030rtc_device,
#endif
};

static inline void __init sdp3430_init_smc91x(void)
{
	int eth_cs;
	unsigned long cs_mem_base;
	unsigned int rate;
	struct clk *l3ck;
	int eth_gpio = 0;

	eth_cs = SDP3430_SMC91X_CS;

	l3ck = clk_get(NULL, "l3_ck");
	if (IS_ERR(l3ck))
		rate = 100000000;
	else
		rate = clk_get_rate(l3ck);

	if (gpmc_cs_request(eth_cs, SZ_16M, &cs_mem_base) < 0) {
		printk(KERN_ERR "Failed to request GPMC mem for smc91x\n");
		return;
	}

	sdp3430_smc91x_resources[0].start = cs_mem_base + 0x0;
	sdp3430_smc91x_resources[0].end   = cs_mem_base + 0xf;
	udelay(100);

	if (is_sil_rev_greater_than(OMAP3430_REV_ES1_0))
		eth_gpio = OMAP34XX_ETHR_GPIO_IRQ_SDPV2;
	else
		eth_gpio = OMAP34XX_ETHR_GPIO_IRQ_SDPV1;

	sdp3430_smc91x_resources[1].start = OMAP_GPIO_IRQ(eth_gpio);

	if (omap_request_gpio(eth_gpio) < 0) {
		printk(KERN_ERR "Failed to request GPIO%d for smc91x IRQ\n",
			eth_gpio);
		return;
	}
	omap_set_gpio_direction(eth_gpio, 1);
}

static void __init omap_3430sdp_init_irq(void)
{
	omap2_init_common_hw();
	omap_init_irq();
	omap_gpio_init();
	sdp3430_init_smc91x();
}

static struct omap_uart_config sdp3430_uart_config __initdata = {
	.enabled_uarts	= ((1 << 0) | (1 << 1) | (1 << 2)),
};

static struct omap_lcd_config sdp3430_lcd_config __initdata = {
	.ctrl_name	= "internal",
};

static struct omap_mmc_config sdp3430_mmc_config __initdata = {
	.mmc [0] = {
		.enabled	= 1,
		.wire4		= 1,
	},
};

static struct omap_board_config_kernel sdp3430_config[] __initdata = {
	{ OMAP_TAG_UART,	&sdp3430_uart_config },
	{OMAP_TAG_LCD,		&sdp3430_lcd_config},
	{OMAP_TAG_MMC,		&sdp3430_mmc_config },
};

static int __init omap3430_i2c_init(void)
{
	omap_register_i2c_bus(1, 2600, NULL, 0);
	omap_register_i2c_bus(2, 400, NULL, 0);
	omap_register_i2c_bus(3, 400, NULL, 0);
	return 0;
}

extern void __init sdp3430_flash_init(void);

static void __init omap_3430sdp_init(void)
{
	/* System Control module clock initialization */
	scm_clk_init();	
	platform_add_devices(sdp3430_devices, ARRAY_SIZE(sdp3430_devices));
	omap_board_config = sdp3430_config;
	omap_board_config_size = ARRAY_SIZE(sdp3430_config);
	if (is_sil_rev_greater_than(OMAP3430_REV_ES1_0))
		ts_gpio = OMAP34XX_TS_GPIO_IRQ_SDPV2;
	else
		ts_gpio = OMAP34XX_TS_GPIO_IRQ_SDPV1;
	sdp3430_spi_board_info[0].irq = OMAP_GPIO_IRQ(ts_gpio);
	spi_register_board_info(sdp3430_spi_board_info,
				ARRAY_SIZE(sdp3430_spi_board_info));
	ads7846_dev_init();
	sdp3430_flash_init();
	omap_serial_init();
	usb_musb_init();
	usb_ehci_init();
	hsmmc_init();
}

static void __init omap_3430sdp_map_io(void)
{
	omap2_set_globals_343x();
	omap2_map_common_io();
}
arch_initcall(omap3430_i2c_init);

MACHINE_START(OMAP_3430SDP, "OMAP3430 3430SDP board")
	/* Maintainer: Syed Khasim - Texas Instruments Inc */
	.phys_io	= 0x48000000,
	.io_pg_offst	= ((0xd8000000) >> 18) & 0xfffc,
	.boot_params	= 0x80000100,
	.map_io		= omap_3430sdp_map_io,
	.init_irq	= omap_3430sdp_init_irq,
	.init_machine	= omap_3430sdp_init,
	.timer		= &omap_timer,
MACHINE_END
