/*
 *  linux/drivers/ide/pci/slc90e66.c	Version 0.19	Sep 24, 2007
 *
 *  Copyright (C) 2000-2002 Andre Hedrick <andre@linux-ide.org>
 *  Copyright (C) 2006-2007 MontaVista Software, Inc. <source@mvista.com>
 *
 * This is a look-alike variation of the ICH0 PIIX4 Ultra-66,
 * but this keeps the ISA-Bridge and slots alive.
 *
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/delay.h>
#include <linux/init.h>

#include <asm/io.h>

static DEFINE_SPINLOCK(slc90e66_lock);

static void slc90e66_set_pio_mode(ide_drive_t *drive, const u8 pio)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	int is_slave		= drive->dn & 1;
	int master_port		= hwif->channel ? 0x42 : 0x40;
	int slave_port		= 0x44;
	unsigned long flags;
	u16 master_data;
	u8 slave_data;
 	int control = 0;
				     /* ISP  RTC */
	static const u8 timings[][2]= {
					{ 0, 0 },
					{ 0, 0 },
					{ 1, 0 },
					{ 2, 1 },
					{ 2, 3 }, };

	spin_lock_irqsave(&slc90e66_lock, flags);
	pci_read_config_word(dev, master_port, &master_data);

	if (pio > 1)
		control |= 1;	/* Programmable timing on */
	if (drive->media == ide_disk)
		control |= 4;	/* Prefetch, post write */
	if (pio > 2)
		control |= 2;	/* IORDY */
	if (is_slave) {
		master_data |=  0x4000;
		master_data &= ~0x0070;
		if (pio > 1) {
			/* Set PPE, IE and TIME */
			master_data |= control << 4;
		}
		pci_read_config_byte(dev, slave_port, &slave_data);
		slave_data &= hwif->channel ? 0x0f : 0xf0;
		slave_data |= ((timings[pio][0] << 2) | timings[pio][1]) <<
			       (hwif->channel ? 4 : 0);
	} else {
		master_data &= ~0x3307;
		if (pio > 1) {
			/* enable PPE, IE and TIME */
			master_data |= control;
		}
		master_data |= (timings[pio][0] << 12) | (timings[pio][1] << 8);
	}
	pci_write_config_word(dev, master_port, master_data);
	if (is_slave)
		pci_write_config_byte(dev, slave_port, slave_data);
	spin_unlock_irqrestore(&slc90e66_lock, flags);
}

static void slc90e66_set_dma_mode(ide_drive_t *drive, const u8 speed)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	u8 maslave		= hwif->channel ? 0x42 : 0x40;
	int sitre = 0, a_speed	= 7 << (drive->dn * 4);
	int u_speed = 0, u_flag = 1 << drive->dn;
	u16			reg4042, reg44, reg48, reg4a;

	pci_read_config_word(dev, maslave, &reg4042);
	sitre = (reg4042 & 0x4000) ? 1 : 0;
	pci_read_config_word(dev, 0x44, &reg44);
	pci_read_config_word(dev, 0x48, &reg48);
	pci_read_config_word(dev, 0x4a, &reg4a);

	switch(speed) {
		case XFER_UDMA_4:	u_speed = 4 << (drive->dn * 4); break;
		case XFER_UDMA_3:	u_speed = 3 << (drive->dn * 4); break;
		case XFER_UDMA_2:	u_speed = 2 << (drive->dn * 4); break;
		case XFER_UDMA_1:	u_speed = 1 << (drive->dn * 4); break;
		case XFER_UDMA_0:	u_speed = 0 << (drive->dn * 4); break;
		case XFER_MW_DMA_2:
		case XFER_MW_DMA_1:
		case XFER_SW_DMA_2:	break;
		default:		return;
	}

	if (speed >= XFER_UDMA_0) {
		if (!(reg48 & u_flag))
			pci_write_config_word(dev, 0x48, reg48|u_flag);
		/* FIXME: (reg4a & a_speed) ? */
		if ((reg4a & u_speed) != u_speed) {
			pci_write_config_word(dev, 0x4a, reg4a & ~a_speed);
			pci_read_config_word(dev, 0x4a, &reg4a);
			pci_write_config_word(dev, 0x4a, reg4a|u_speed);
		}
	} else {
		const u8 mwdma_to_pio[] = { 0, 3, 4 };
		u8 pio;

		if (reg48 & u_flag)
			pci_write_config_word(dev, 0x48, reg48 & ~u_flag);
		if (reg4a & a_speed)
			pci_write_config_word(dev, 0x4a, reg4a & ~a_speed);

		if (speed >= XFER_MW_DMA_0)
			pio = mwdma_to_pio[speed - XFER_MW_DMA_0];
		else
			pio = 2; /* only SWDMA2 is allowed */

		slc90e66_set_pio_mode(drive, pio);
	}
}

static void __devinit init_hwif_slc90e66 (ide_hwif_t *hwif)
{
	u8 reg47 = 0;
	u8 mask = hwif->channel ? 0x01 : 0x02;  /* bit0:Primary */

	hwif->set_pio_mode = &slc90e66_set_pio_mode;
	hwif->set_dma_mode = &slc90e66_set_dma_mode;

	pci_read_config_byte(hwif->pci_dev, 0x47, &reg47);

	if (hwif->dma_base == 0)
		return;

	if (hwif->cbl != ATA_CBL_PATA40_SHORT)
		/* bit[0(1)]: 0:80, 1:40 */
		hwif->cbl = (reg47 & mask) ? ATA_CBL_PATA40 : ATA_CBL_PATA80;
}

static const struct ide_port_info slc90e66_chipset __devinitdata = {
	.name		= "SLC90E66",
	.init_hwif	= init_hwif_slc90e66,
	.enablebits	= {{0x41,0x80,0x80}, {0x43,0x80,0x80}},
	.host_flags	= IDE_HFLAG_LEGACY_IRQS | IDE_HFLAG_BOOTABLE,
	.pio_mask	= ATA_PIO4,
	.swdma_mask	= ATA_SWDMA2_ONLY,
	.mwdma_mask	= ATA_MWDMA12_ONLY,
	.udma_mask	= ATA_UDMA4,
};

static int __devinit slc90e66_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	return ide_setup_pci_device(dev, &slc90e66_chipset);
}

static const struct pci_device_id slc90e66_pci_tbl[] = {
	{ PCI_VDEVICE(EFAR, PCI_DEVICE_ID_EFAR_SLC90E66_1), 0 },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, slc90e66_pci_tbl);

static struct pci_driver driver = {
	.name		= "SLC90e66_IDE",
	.id_table	= slc90e66_pci_tbl,
	.probe		= slc90e66_init_one,
};

static int __init slc90e66_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

module_init(slc90e66_ide_init);

MODULE_AUTHOR("Andre Hedrick");
MODULE_DESCRIPTION("PCI driver module for SLC90E66 IDE");
MODULE_LICENSE("GPL");
