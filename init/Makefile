#
# Makefile for the linux kernel.
#

ifeq ($(CONFIG_EXECUTE_AUTHULD),y)
obj-y	:= main.o version.o mounts.o rijndael-alg-fst.o aes-cmac.o Se.o Secureboot.o MicomCtrl_mod.o
else
obj-y                          := main.o version.o mounts.o
endif

ifneq ($(CONFIG_BLK_DEV_INITRD),y)
obj-y                          += noinitramfs.o
else
obj-$(CONFIG_BLK_DEV_INITRD)   += initramfs.o
endif
obj-$(CONFIG_GENERIC_CALIBRATE_DELAY) += calibrate.o

mounts-y			:= do_mounts.o
mounts-$(CONFIG_BLK_DEV_RAM)	+= do_mounts_rd.o
mounts-$(CONFIG_BLK_DEV_INITRD)	+= do_mounts_initrd.o
mounts-$(CONFIG_BLK_DEV_MD)	+= do_mounts_md.o

# files to be removed upon make clean
clean-files := ../include/linux/compile.h

# dependencies on generated files need to be listed explicitly

$(obj)/version.o: include/linux/compile.h

# compile.h changes depending on hostname, generation number, etc,
# so we regenerate it always.
# mkcompile_h will make sure to only update the
# actual file if its content has changed.

include/linux/compile.h: FORCE
	@echo '  CHK     $@'
	$(Q)$(CONFIG_SHELL) $(srctree)/scripts/mkcompile_h $@ \
	"$(UTS_MACHINE)" "$(CONFIG_SMP)" "$(CONFIG_PREEMPT)" "$(CONFIG_PREEMPT_RT)" "$(CC) $(KBUILD_CFLAGS)"
