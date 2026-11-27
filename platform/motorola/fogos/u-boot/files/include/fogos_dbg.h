/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * FOGOS-DEBUG: diagnostico visual no framebuffer da Motorola.
 *   framebuffer 0x85200000, 720 px de largura, 4 bytes/pixel (stride 2880).
 *   Painel de excecao: linhas 700..980.  Rastro de probe: linhas 1000..1576.
 * So escreve na RAM do framebuffer e faz flush do cache. Remover quando o boot
 * estiver validado.
 */
#ifndef __FOGOS_DBG_H
#define __FOGOS_DBG_H

#include <cpu_func.h>

#ifndef FOGOS_FB
#define FOGOS_FB	0x85200000UL
#endif
#define FOGOS_W		720UL
#define FOGOS_STRIDE	2880UL

/* fonte 5x7 so com os digitos hexadecimais 0-9 A-F */
static const unsigned char fogos_font[16][7] = {
	{ 0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e }, /* 0 */
	{ 0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e }, /* 1 */
	{ 0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f }, /* 2 */
	{ 0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e }, /* 3 */
	{ 0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02 }, /* 4 */
	{ 0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e }, /* 5 */
	{ 0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e }, /* 6 */
	{ 0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 }, /* 7 */
	{ 0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e }, /* 8 */
	{ 0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c }, /* 9 */
	{ 0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 }, /* A */
	{ 0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e }, /* B */
	{ 0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e }, /* C */
	{ 0x1c, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1c }, /* D */
	{ 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f }, /* E */
	{ 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10 }, /* F */
};

/* desenha um digito hex (d >= 16 apaga a celula) com escala s; passo = 6*s px */
static inline void fogos_glyph(unsigned long x, unsigned long y,
			       unsigned int d, unsigned int s)
{
	volatile unsigned int *fb = (volatile unsigned int *)FOGOS_FB;
	unsigned int r, c, i, j;

	for (r = 0; r < 7; r++) {
		for (c = 0; c < 5; c++) {
			unsigned int v = (d < 16 &&
					  ((fogos_font[d][r] >> (4 - c)) & 1)) ?
					 0xffffffffU : 0U;

			for (i = 0; i < s; i++)
				for (j = 0; j < s; j++)
					fb[(y + r * s + i) * FOGOS_W +
					   x + c * s + j] = v;
		}
	}
}

/* n digitos hex de v (do mais significativo para o menos) */
static inline void fogos_hex(unsigned long x, unsigned long y,
			     unsigned long long v, unsigned int n,
			     unsigned int s)
{
	unsigned int i;

	for (i = 0; i < n; i++)
		fogos_glyph(x + i * 6 * s, y,
			    (unsigned int)(v >> (4 * (n - 1 - i))) & 15, s);
}

static inline void fogos_flush(unsigned long y0, unsigned long y1)
{
	flush_dcache_range(FOGOS_FB + y0 * FOGOS_STRIDE,
			   FOGOS_FB + y1 * FOGOS_STRIDE);
}

/* linha do painel de excecao: indice (1 digito) e valor de 64 bits */
static inline void fogos_panel_row(unsigned int row, unsigned long long v)
{
	unsigned long y = 700 + 40UL * row;

	fogos_hex(10, y, row, 1, 4);
	fogos_hex(58, y, v, 16, 4);
}

#endif /* __FOGOS_DBG_H */
