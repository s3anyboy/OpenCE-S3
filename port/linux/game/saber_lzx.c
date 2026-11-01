/*
SABER_LZX.C

The LZX decoder of the Saber3D packages (saber_paks.c): Halo: Combat Evolved
Anniversary's Xbox 360 packages are compressed in blocks, each an Xbox 360
XMemCompress stream of LZX frames (a 128 KB window).

Written from the LZX format's description (Microsoft's LZX, without the
DELTA extensions). The bit stream is 16-bit little-endian words, read from
their most significant bit. A stream is a sequence of chunks, each either
0xFF, its uncompressed size (big-endian 16 bits), its compressed size and
its data, or its compressed size and its data (32768 bytes uncompressed);
a chunk of compressed size 0 ends it. Each chunk is one frame, and the
decoder's window, trees and repeated offsets carry over from frame to frame.

Everything is checked against the stream: a damaged one is refused, never
read or written past.
*/

/* ---------- headers */

#include "cseries.h"
#include "saber.h"

#include <stddef.h>
#include <string.h>

/* ---------- constants */

#define MIN_MATCH        2
#define NUM_CHARS        256
#define PRETREE_SYMS     20
#define PRETREE_BITS     6
#define ALIGNED_SYMS     8
#define ALIGNED_BITS     7
#define NUM_PRIMARY_LEN  7
#define LENGTH_SYMS      250          /* 249 secondary lengths + 1 */
#define LENGTH_BITS      12
#define MAIN_MAXSYMS     (NUM_CHARS + 50 * 8)
#define MAIN_BITS        12
#define FRAME_SIZE       32768
#define MAX_WINDOW_BITS  21

enum { BT_VERBATIM = 1, BT_ALIGNED = 2, BT_UNCOMPRESSED = 3 };

static const unsigned char extra_bits[51] = {
	0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13,14,14,15,15,16,16,
	17,17,17,17,17,17,17,17,17,17,17,17,17,17,17
};

typedef struct {
	const unsigned char *in, *in_end;
	unsigned int bitbuf;     /* left-aligned */
	int bits;                /* valid bits in bitbuf */
	int overrun;
} bits_t;

typedef struct saber_lzx_state saber_lzx_state;

struct saber_lzx_state {
	unsigned char *window;
	unsigned int window_size, window_pos;
	int window_bits, num_slots, main_syms;
	unsigned int position_base[51];
	unsigned int R0, R1, R2;
	int header_read, intel_e8;
	int block_type;
	unsigned int block_remaining, block_length;
	unsigned char pretree_len[PRETREE_SYMS];
	unsigned char main_len[MAIN_MAXSYMS];
	unsigned char length_len[LENGTH_SYMS];
	unsigned char aligned_len[ALIGNED_SYMS];
	unsigned short pretree_tab[(1 << PRETREE_BITS) + PRETREE_SYMS * 2];
	unsigned short main_tab[(1 << MAIN_BITS) + MAIN_MAXSYMS * 2];
	unsigned short length_tab[(1 << LENGTH_BITS) + LENGTH_SYMS * 2];
	unsigned short aligned_tab[(1 << ALIGNED_BITS) + ALIGNED_SYMS * 2];
	int length_empty;
	unsigned long frames_done;
};

/* ---------------------------------------------------------------- bits */
static void fill(bits_t *b) {
	while (b->bits <= 16) {
		unsigned int w;
		if (b->in + 1 < b->in_end) { w = b->in[0] | (b->in[1] << 8); b->in += 2; }
		else { w = 0; b->overrun++; b->in = b->in_end; }
		b->bitbuf |= w << (16 - b->bits);
		b->bits += 16;
	}
}
static unsigned int peek(bits_t *b, int n) { fill(b); return b->bitbuf >> (32 - n); }
static void drop(bits_t *b, int n) { b->bitbuf <<= n; b->bits -= n; }
static unsigned int getbits(bits_t *b, int n) {
	unsigned int v;
	if (n == 0) return 0;
	if (n > 16) { v = getbits(b, n - 16) << 16; return v | getbits(b, 16); }
	v = peek(b, n); drop(b, n); return v;
}

/* ------------------------------------------------------------- huffman */
/* canonical Huffman decode table: direct lookup for codes up to tbits, then a binary tree
   stored after the direct entries. Returns 0 on success. */
static int make_table(int nsyms, int tbits, const unsigned char *len, unsigned short *tab) {
	unsigned int pos = 0, tmask = 1u << tbits, bitmask = tmask >> 1;
	unsigned int next_sym = ((tmask >> 1) < (unsigned)nsyms) ? (unsigned)nsyms : (tmask >> 1);
	int bit_num = 1, sym;
	unsigned int leaf, fill_n;
	while (bit_num <= tbits) {
		for (sym = 0; sym < nsyms; sym++) {
			if (len[sym] == bit_num) {
				leaf = pos;
				if ((pos += bitmask) > tmask) return 1;
				for (fill_n = bitmask; fill_n-- > 0;) tab[leaf++] = (unsigned short)sym;
			}
		}
		bitmask >>= 1; bit_num++;
	}
	if (pos != tmask) {
		for (sym = pos; sym < (int)tmask; sym++) tab[sym] = 0xFFFF;
		pos <<= 16; tmask <<= 16; bitmask = 1u << 15;
		while (bit_num <= 16) {
			for (sym = 0; sym < nsyms; sym++) {
				if (len[sym] == bit_num) {
					leaf = pos >> 16;
					for (fill_n = 0; fill_n < (unsigned)(bit_num - tbits); fill_n++) {
						if (tab[leaf] == 0xFFFF) {
							tab[next_sym << 1] = 0xFFFF; tab[(next_sym << 1) + 1] = 0xFFFF;
							tab[leaf] = (unsigned short)next_sym++;
						}
						leaf = tab[leaf] << 1;
						if ((pos >> (15 - fill_n)) & 1) leaf++;
					}
					tab[leaf] = (unsigned short)sym;
					if ((pos += bitmask) > tmask) return 1;
				}
			}
			bitmask >>= 1; bit_num++;
		}
	}
	if (pos == tmask) return 0;
	for (sym = 0; sym < nsyms; sym++) if (len[sym]) return 1;   /* incomplete */
	return 0;                                                    /* empty table */
}
static int decode_sym(bits_t *b, const unsigned short *tab, int tbits, int nsyms, const unsigned char *len) {
	unsigned int sym, i;
	fill(b);
	sym = tab[b->bitbuf >> (32 - tbits)];
	if (sym >= (unsigned)nsyms) {
		i = 1u << (32 - tbits);
		do {
			i >>= 1;
			if (i == 0) return -1;
			sym = tab[(sym << 1) | ((b->bitbuf & i) ? 1 : 0)];
		} while (sym >= (unsigned)nsyms);
	}
	drop(b, len[sym]);
	return (int)sym;
}

/* lengths of symbols [first, last) via the pretree, delta coded against the old lengths */
static int read_lengths(saber_lzx_state *s, bits_t *b, unsigned char *lens, int first, int last) {
	int i, x, z;
	for (x = 0; x < PRETREE_SYMS; x++) s->pretree_len[x] = (unsigned char)getbits(b, 4);
	if (make_table(PRETREE_SYMS, PRETREE_BITS, s->pretree_len, s->pretree_tab)) return SABER_LZX_ERROR_DATA;
	for (x = first; x < last;) {
		z = decode_sym(b, s->pretree_tab, PRETREE_BITS, PRETREE_SYMS, s->pretree_len);
		if (z < 0) return SABER_LZX_ERROR_DATA;
		if (z == 17) {
			i = getbits(b, 4) + 4; while (i-- && x < last) lens[x++] = 0;
		} else if (z == 18) {
			i = getbits(b, 5) + 20; while (i-- && x < last) lens[x++] = 0;
		} else if (z == 19) {
			i = getbits(b, 1) + 4;
			z = decode_sym(b, s->pretree_tab, PRETREE_BITS, PRETREE_SYMS, s->pretree_len);
			if (z < 0 || z > 16) return SABER_LZX_ERROR_DATA;
			z = lens[x] - z; if (z < 0) z += 17;
			while (i-- && x < last) lens[x++] = (unsigned char)z;
		} else {
			z = lens[x] - z; if (z < 0) z += 17;
			lens[x++] = (unsigned char)z;
		}
	}
	return 0;
}

/* --------------------------------------------------------------- state */
size_t saber_lzx_state_size(void) { return sizeof(saber_lzx_state); }

int saber_lzx_initialize(saber_lzx_state *s, int window_bits, unsigned char *window) {
	int i; unsigned int base = 0;
	if (window_bits < 15 || window_bits > MAX_WINDOW_BITS) return SABER_LZX_ERROR_ARGUMENTS;
	memset(s, 0, sizeof(*s));
	s->window = window;
	s->window_bits = window_bits;
	s->window_size = 1u << window_bits;
	s->num_slots = window_bits == 20 ? 42 : window_bits == 21 ? 50 : window_bits * 2;
	s->main_syms = NUM_CHARS + s->num_slots * 8;
	for (i = 0; i < 51; i++) { s->position_base[i] = base; base += 1u << extra_bits[i]; }
	saber_lzx_reset(s);
	return 0;
}

void saber_lzx_reset(saber_lzx_state *s) {
	s->R0 = s->R1 = s->R2 = 1;
	s->header_read = 0; s->intel_e8 = 0;
	s->block_type = 0; s->block_remaining = 0; s->block_length = 0;
	s->window_pos = 0; s->frames_done = 0;
	memset(s->main_len, 0, sizeof(s->main_len));
	memset(s->length_len, 0, sizeof(s->length_len));
}

/* decode one frame of `out_size` bytes from `in`; output to `out` */
static int decode_frame(saber_lzx_state *s, const unsigned char *in, size_t in_len, unsigned char *out, unsigned int out_size) {
	bits_t b; unsigned int frame_end, start = s->window_pos;
	int err, i;
	b.in = in; b.in_end = in + in_len; b.bitbuf = 0; b.bits = 0; b.overrun = 0;
	if (s->window_pos + out_size > s->window_size) return SABER_LZX_ERROR_DATA;
	frame_end = s->window_pos + out_size;

	if (!s->header_read) {
		/* (Intel E8 call translation: never on in Saber's packages, and not
		done here) */
		s->intel_e8 = (int)getbits(&b, 1);
		if (s->intel_e8) return SABER_LZX_ERROR_DATA;
		s->header_read = 1;
	}
	while (s->window_pos < frame_end) {
		unsigned int this_run;
		if (s->block_remaining == 0) {
			if (s->block_type == BT_UNCOMPRESSED && (s->block_length & 1)) b.in++;   /* pad byte */
			s->block_type = (int)getbits(&b, 3);
			s->block_remaining = s->block_length = (getbits(&b, 16) << 8) | getbits(&b, 8);
			switch (s->block_type) {
			case BT_ALIGNED:
				for (i = 0; i < ALIGNED_SYMS; i++) s->aligned_len[i] = (unsigned char)getbits(&b, 3);
				if (make_table(ALIGNED_SYMS, ALIGNED_BITS, s->aligned_len, s->aligned_tab)) return SABER_LZX_ERROR_DATA;
				/* fall through */
			case BT_VERBATIM:
				if ((err = read_lengths(s, &b, s->main_len, 0, NUM_CHARS))) return err;
				if ((err = read_lengths(s, &b, s->main_len, NUM_CHARS, s->main_syms))) return err;
				if (make_table(s->main_syms, MAIN_BITS, s->main_len, s->main_tab)) return SABER_LZX_ERROR_DATA;
				if ((err = read_lengths(s, &b, s->length_len, 0, LENGTH_SYMS - 1))) return err;
				s->length_empty = 1;
				for (i = 0; i < LENGTH_SYMS; i++) if (s->length_len[i]) s->length_empty = 0;
				if (!s->length_empty && make_table(LENGTH_SYMS, LENGTH_BITS, s->length_len, s->length_tab)) return SABER_LZX_ERROR_DATA;
				break;
			case BT_UNCOMPRESSED: {
				/* align to 16 bits, then R0..R2 as 32-bit LE, then raw bytes */
				/* drop the rest of the current 16-bit word (a whole word if none is
				   started), and give back the words read ahead */
				{
					int k = b.bits & 15;
					int unread = k ? (b.bits - k) / 16 : b.bits / 16 - 1;
					b.in -= 2 * unread; b.bits = 0; b.bitbuf = 0;
				}
				if (b.in + 12 > b.in_end) return SABER_LZX_ERROR_DATA;
				s->R0 = b.in[0] | (b.in[1] << 8) | (b.in[2] << 16) | ((unsigned)b.in[3] << 24);
				s->R1 = b.in[4] | (b.in[5] << 8) | (b.in[6] << 16) | ((unsigned)b.in[7] << 24);
				s->R2 = b.in[8] | (b.in[9] << 8) | (b.in[10] << 16) | ((unsigned)b.in[11] << 24);
				b.in += 12;
				break; }
			default:
				return SABER_LZX_ERROR_DATA;
			}
		}
		this_run = s->block_remaining;
		if (this_run > frame_end - s->window_pos) this_run = frame_end - s->window_pos;
		s->block_remaining -= this_run;

		if (s->block_type == BT_UNCOMPRESSED) {
			if (b.in + this_run > b.in_end) return SABER_LZX_ERROR_DATA;
			memcpy(s->window + s->window_pos, b.in, this_run);
			b.in += this_run; s->window_pos += this_run;
			continue;
		}
		while (this_run > 0) {
			int sym = decode_sym(&b, s->main_tab, MAIN_BITS, s->main_syms, s->main_len);
			if (sym < 0) return SABER_LZX_ERROR_DATA;
			if (sym < NUM_CHARS) {
				s->window[s->window_pos++] = (unsigned char)sym; this_run--;
				continue;
			}
			sym -= NUM_CHARS;
			{
				unsigned int len = sym & NUM_PRIMARY_LEN, slot = sym >> 3, off, src;
				if (len == NUM_PRIMARY_LEN) {
					int l;
					if (s->length_empty) return SABER_LZX_ERROR_DATA;
					l = decode_sym(&b, s->length_tab, LENGTH_BITS, LENGTH_SYMS, s->length_len);
					if (l < 0) return SABER_LZX_ERROR_DATA;
					len += l;
				}
				len += MIN_MATCH;
				if (slot == 0) off = s->R0;
				else if (slot == 1) { off = s->R1; s->R1 = s->R0; s->R0 = off; }
				else if (slot == 2) { off = s->R2; s->R2 = s->R0; s->R0 = off; }
				else {
					unsigned int eb = extra_bits[slot];
					off = s->position_base[slot] - 2;
					if (s->block_type == BT_ALIGNED && eb >= 3) {
						int a;
						off += getbits(&b, eb - 3) << 3;
						a = decode_sym(&b, s->aligned_tab, ALIGNED_BITS, ALIGNED_SYMS, s->aligned_len);
						if (a < 0) return SABER_LZX_ERROR_DATA;
						off += a;
					} else {
						off += getbits(&b, eb);
					}
					s->R2 = s->R1; s->R1 = s->R0; s->R0 = off;
				}
				if (len > this_run) return SABER_LZX_ERROR_DATA;      /* matches never cross a frame */
				if (!s->frames_done && off > s->window_pos) return SABER_LZX_ERROR_DATA;
				src = (s->window_pos - off) & (s->window_size - 1);
				this_run -= len;
				while (len--) {
					s->window[s->window_pos++] = s->window[src];
					src = (src + 1) & (s->window_size - 1);
				}
			}
		}
	}
	if (b.overrun > 2) return SABER_LZX_ERROR_DATA;
	memcpy(out, s->window + start, out_size);
	if (s->window_pos == s->window_size) s->window_pos = 0;
	s->frames_done++;
	return 0;
}

long saber_lzx_decompress(saber_lzx_state *s, const unsigned char *in, size_t in_len,
	unsigned char *out, size_t out_cap) {
	size_t p = 0, o = 0;
	saber_lzx_reset(s);
	while (p + 2 <= in_len) {
		unsigned int usize, csize;
		if (in[p] == 0xFF) {
			if (p + 5 > in_len) return SABER_LZX_ERROR_DATA;
			usize = (in[p+1] << 8) | in[p+2]; csize = (in[p+3] << 8) | in[p+4]; p += 5;
		} else {
			usize = FRAME_SIZE; csize = (in[p] << 8) | in[p+1]; p += 2;
		}
		if (csize == 0) break;
		if (p + csize > in_len) return SABER_LZX_ERROR_DATA;
		if (usize > FRAME_SIZE) return SABER_LZX_ERROR_DATA;
		if (o + usize > out_cap) return SABER_LZX_ERROR_SPACE;
		{
			int e = decode_frame(s, in + p, csize, out + o, usize);
			if (e) return e;
		}
		p += csize; o += usize;
	}
	return (long)o;
}
