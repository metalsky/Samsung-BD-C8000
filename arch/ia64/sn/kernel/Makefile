# arch/ia64/sn/kernel/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1999,2001-2006 Silicon Graphics, Inc.  All Rights Reserved.
#

EXTRA_CFLAGS += -Iarch/ia64/sn/include

obj-y				+= setup.o bte.o bte_error.o irq.o mca.o idle.o \
				   huberror.o io_acpi_init.o io_common.o \
				   io_init.o iomv.o klconflib.o pio_phys.o \
				   sn2/
obj-$(CONFIG_IA64_GENERIC)      += machvec.o
obj-$(CONFIG_SGI_TIOCX)		+= tiocx.o
obj-$(CONFIG_IA64_SGI_SN_XP)	+= xp.o
xp-y				:= xp_main.o xp_nofault.o
obj-$(CONFIG_IA64_SGI_SN_XP)	+= xpc.o
xpc-y				:= xpc_main.o xpc_channel.o xpc_partition.o
obj-$(CONFIG_IA64_SGI_SN_XP)	+= xpnet.o
obj-$(CONFIG_PCI_MSI)		+= msi_sn.o
