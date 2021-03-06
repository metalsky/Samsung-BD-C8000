/* memset/bzero -- set memory area to CH/0
   Highly optimized version for ix86, x>=5.
   Copyright (C) 1996, 1997, 2000, 2003, 2005 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Torbjorn Granlund, <tege@matematik.su.se>

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, write to the Free
   Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307 USA.  */

#include <sysdep.h>
#include "asm-syntax.h"
#include "bp-sym.h"
#include "bp-asm.h"

/* BEWARE: `#ifdef memset' means that memset is redefined as `bzero' */
#define BZERO_P (defined memset)

#define PARMS	LINKAGE+4	/* space for 1 saved reg */
#define RTN	PARMS
#define DEST	RTN+RTN_SIZE
#if BZERO_P
# define LEN	DEST+PTR_SIZE
#else
# define CHR	DEST+PTR_SIZE
# define LEN	CHR+4
#endif

        .text
ENTRY (BP_SYM (memset))
	ENTER

	pushl	%edi
	cfi_adjust_cfa_offset (4)

	movl	DEST(%esp), %edi
	cfi_rel_offset (edi, 0)
	movl	LEN(%esp), %edx
	CHECK_BOUNDS_BOTH_WIDE (%edi, DEST(%esp), %edx)
#if BZERO_P
	xorl	%eax, %eax	/* we fill with 0 */
#else
	movb	CHR(%esp), %al
	movb	%al, %ah
	movl	%eax, %ecx
	shll	$16, %eax
	movw	%cx, %ax
#endif
	cld

/* If less than 36 bytes to write, skip tricky code (it wouldn't work).  */
	cmpl	$36, %edx
	movl	%edx, %ecx	/* needed when branch is taken! */
	jl	L(2)

/* First write 0-3 bytes to make the pointer 32-bit aligned.  */
	movl	%edi, %ecx	/* Copy ptr to ecx... */
	negl	%ecx		/* ...and negate that and... */
	andl	$3, %ecx	/* ...mask to get byte count.  */
	subl	%ecx, %edx	/* adjust global byte count */
	rep
	stosb

	subl	$32, %edx	/* offset count for unrolled loop */
	movl	(%edi), %ecx	/* Fetch destination cache line */

	.align	2, 0x90		/* supply 0x90 for broken assemblers */
L(1):	movl	28(%edi), %ecx	/* allocate cache line for destination */
	subl	$32, %edx	/* decr loop count */
	movl	%eax, 0(%edi)	/* store words pairwise */
	movl	%eax, 4(%edi)
	movl	%eax, 8(%edi)
	movl	%eax, 12(%edi)
	movl	%eax, 16(%edi)
	movl	%eax, 20(%edi)
	movl	%eax, 24(%edi)
	movl	%eax, 28(%edi)
	leal	32(%edi), %edi	/* update destination pointer */
	jge	L(1)

	leal	32(%edx), %ecx	/* reset offset count */

/* Write last 0-7 full 32-bit words (up to 8 words if loop was skipped).  */
L(2):	shrl	$2, %ecx	/* convert byte count to longword count */
	rep
	stosl

/* Finally write the last 0-3 bytes.  */
	movl	%edx, %ecx
	andl	$3, %ecx
	rep
	stosb

#if !BZERO_P
	/* Load result (only if used as memset).  */
	movl DEST(%esp), %eax	/* start address of destination is result */
	RETURN_BOUNDED_POINTER (DEST(%esp))
#endif
	popl	%edi
	cfi_adjust_cfa_offset (-4)
	cfi_restore (edi)

	LEAVE
#if BZERO_P
	ret
#else
	RET_PTR
#endif
END (BP_SYM (memset))
libc_hidden_builtin_def (memset)
