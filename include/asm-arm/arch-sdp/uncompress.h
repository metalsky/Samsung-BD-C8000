/*
 *  linux/include/asm-arm/arch-versatile/uncompress.h
 *
 *  Copyright (C) 2003 ARM Limited
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
/* TODO: not used constant value as address */
#if 0
#define S5H2X_UART_UTRSTAT 	(*(volatile unsigned char *)0x30090A50)
#define S5H2X_UART_UTXH 	(*(volatile unsigned char *)0x30090A60)
#endif
/*
 * This does not append a newline
 */
static inline void putc(int c)
{
#if 0
	while (!(S5H2X_UART_UTRSTAT & (1 << 2)))
		barrier();

	S5H2X_UART_UTXH = (c & 0xFF);
#endif
}

static inline void flush(void)
{
#if 0
	while (!(S5H2X_UART_UTRSTAT & (1 << 2)))
		barrier();
#endif
}

/*
 * nothing to do
 */
#define arch_decomp_setup()
#define arch_decomp_wdog()
