/*
 * ld script to make SuperH Linux kernel
 * Written by Niibe Yutaka
 */
#include <asm/thread_info.h>
#include <asm/cache.h>
#include <asm-generic/vmlinux.lds.h>

#ifdef CONFIG_CPU_LITTLE_ENDIAN
OUTPUT_FORMAT("elf32-sh-linux", "elf32-sh-linux", "elf32-sh-linux")
#else
OUTPUT_FORMAT("elf32-shbig-linux", "elf32-shbig-linux", "elf32-shbig-linux")
#endif
OUTPUT_ARCH(sh)
ENTRY(_start)
SECTIONS
{
	. = CONFIG_PAGE_OFFSET + CONFIG_MEMORY_START + CONFIG_ZERO_PAGE_OFFSET;
	_text = .;			/* Text and read-only data */

	.empty_zero_page : {
		*(.empty_zero_page)
	} = 0

	.text : {
		*(.text.head)
		TEXT_TEXT
		SCHED_TEXT
		LOCK_TEXT
		KPROBES_TEXT
		*(.fixup)
		*(.gnu.warning)
	} = 0x0009

	. = ALIGN(16);		/* Exception table */
	__start___ex_table = .;
	__ex_table : { *(__ex_table) }
	__stop___ex_table = .;

	_etext = .;			/* End of text section */

	BUG_TABLE
	NOTES
	RO_DATA(PAGE_SIZE)

	. = ALIGN(THREAD_SIZE);
	.data : {			/* Data */
		*(.data.init_task)

		. = ALIGN(L1_CACHE_BYTES);
		*(.data.cacheline_aligned)

		. = ALIGN(L1_CACHE_BYTES);
		*(.data.read_mostly)

		. = ALIGN(PAGE_SIZE);
		*(.data.page_aligned)

		__nosave_begin = .;
		*(.data.nosave)
		. = ALIGN(PAGE_SIZE);
		__nosave_end = .;

		DATA_DATA
		CONSTRUCTORS
	}

	_edata = .;			/* End of data section */

	. = ALIGN(PAGE_SIZE);		/* Init code and data */
	__init_begin = .;
	_sinittext = .;
	.init.text : { *(.init.text) }
	_einittext = .;
	.init.data : { *(.init.data) }

	. = ALIGN(16);
	__setup_start = .;
	.init.setup : { *(.init.setup) }
	__setup_end = .;

	__initcall_start = .;
	.initcall.init : {
		INITCALLS
	}
	__initcall_end = .;
	__con_initcall_start = .;
	.con_initcall.init : { *(.con_initcall.init) }
	__con_initcall_end = .;

	SECURITY_INIT

#ifdef CONFIG_BLK_DEV_INITRD
	. = ALIGN(PAGE_SIZE);
	__initramfs_start = .;
	.init.ramfs : { *(.init.ramfs) }
	__initramfs_end = .;
#endif

	. = ALIGN(4);
	__machvec_start = .;
	.machvec.init : { *(.machvec.init) }
	__machvec_end = .;

	PERCPU(PAGE_SIZE)

	/*
	 * .exit.text is discarded at runtime, not link time, to deal with
	 * references from __bug_table
	 */
	.exit.text : { *(.exit.text) }
	.exit.data : { *(.exit.data) }

	. = ALIGN(PAGE_SIZE);
	.bss : {
		__init_end = .;
		__bss_start = .;		/* BSS */
		*(.bss.page_aligned)
		*(.bss)
		*(COMMON)
		. = ALIGN(4);
		_ebss = .;			/* uClinux MTD sucks */
		_end = . ;
	}

	/*
	 * When something in the kernel is NOT compiled as a module, the
	 * module cleanup code and data are put into these segments. Both
	 * can then be thrown away, as cleanup code is never called unless
	 * it's a module.
	 */
	/DISCARD/ : {
		*(.exitcall.exit)
	}

	STABS_DEBUG
	DWARF_DEBUG
}
