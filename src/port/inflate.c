/* inflate.c -- DEFLATE + gzip. See inflate.h for why this exists.
 *
 * Structure:
 *   bit reader that zero-pads past EOF (the standard trick -- it lets the
 *   decoder run off the end of the last block without a special case)
 *   canonical Huffman tables with a full 15-bit lookup (fast, no bit-by-bit)
 *   the three block types: stored, fixed, dynamic
 *
 * Verified against Python's gzip for byte-identity on real CMR2 .bfl/.c3d.
 */
#include "inflate.h"
#include <stdlib.h>
#include <string.h>

#define MAXBITS 15
#define NLITSYM 288
#define NDIST   30
#define NCLEN   19

/* ---------------------------------------------------------------- bits --- */
typedef struct {
    const uint8_t *in;
    size_t inlen, inpos;
    uint32_t buf;
    int cnt;
    int overrun;   /* set if we walked past the end (used for validation) */
} Bits;

static void bits_init(Bits *b, const uint8_t *in, size_t n) {
    b->in = in; b->inlen = n; b->inpos = 0; b->buf = 0; b->cnt = 0; b->overrun = 0;
}

/* Keep 25..32 bits available. Past EOF we feed zeros, which is correct for
 * the final partial byte and harmless for the trailer we never read. */
static void bits_fill(Bits *b) {
    while (b->cnt <= 24) {
        uint32_t c = 0;
        if (b->inpos < b->inlen) c = b->in[b->inpos++];
        else b->overrun = 1;
        b->buf |= c << b->cnt;
        b->cnt += 8;
    }
}

static uint32_t bits_peek(Bits *b, int n) {
    if (b->cnt < n) bits_fill(b);
    return b->buf & ((1u << n) - 1u);
}

static void bits_drop(Bits *b, int n) { b->buf >>= n; b->cnt -= n; }

static uint32_t bits_take(Bits *b, int n) {
    uint32_t v = bits_peek(b, n);
    bits_drop(b, n);
    return v;
}

static void bits_align(Bits *b) {
    int r = b->cnt & 7;
    if (r) bits_drop(b, r);
}

/* ------------------------------------------------------------- huffman --- */
typedef struct {
    uint16_t count[MAXBITS + 1];
    uint16_t sym[NLITSYM];
    int      lookup[1 << MAXBITS];   /* -1 empty; else (sym << 4) | len */
    int      nsym;
} Huff;

static int bitrev(int code, int len) {
    int r = 0;
    for (int i = 0; i < len; i++) { r = (r << 1) | (code & 1); code >>= 1; }
    return r;
}

/* lens[] are code lengths per symbol. Returns 0 ok, -1 malformed.
 * allow_incomplete permits the "one distance code" case DEFLATE allows. */
static int huff_build(Huff *h, const uint8_t *lens, int n, int allow_incomplete) {
    memset(h->count, 0, sizeof h->count);
    for (int i = 0; i < n; i++) h->count[lens[i]]++;
    h->nsym = n;
    for (int i = 0; i < (1 << MAXBITS); i++) h->lookup[i] = -1;

    /* Kraft inequality. left MUST start at 1 and shift at the top of each
     * iteration; starting at 0 makes l==1 go negative for any table that has
     * a short code, which rejects every valid table. */
    int left = 1;
    for (int l = 1; l <= MAXBITS; l++) {
        left <<= 1;
        left -= h->count[l];
        if (left < 0) return -1;                 /* over-subscribed */
    }
    int nz = 0;
    for (int l = 1; l <= MAXBITS; l++) nz += h->count[l];
    if (nz == 0) return 0;                        /* no codes at all */
    if (left > 0 && !(allow_incomplete && nz == 1)) return -1;

    /* canonical symbol ordering */
    uint16_t offs[MAXBITS + 2];
    offs[1] = 0;
    for (int l = 1; l <= MAXBITS; l++) offs[l + 1] = offs[l] + h->count[l];
    for (int s = 0; s < n; s++)
        if (lens[s]) h->sym[offs[lens[s]]++] = (uint16_t)s;

    int code = 0, si = 0;
    for (int l = 1; l <= MAXBITS; l++) {
        for (int k = 0; k < h->count[l]; k++) {
            int s = h->sym[si++];
            int rc = bitrev(code, l);
            int step = 1 << l;
            for (int j = rc; j < (1 << MAXBITS); j += step)
                h->lookup[j] = (s << 4) | l;
            code++;
        }
        code <<= 1;
    }

    /* incomplete single-code table: fill the holes with it at length 1 */
    if (nz == 1) {
        int s = -1;
        for (int i = 0; i < n; i++) if (lens[i]) { s = i; break; }
        for (int i = 0; i < (1 << MAXBITS); i++)
            if (h->lookup[i] < 0) h->lookup[i] = (s << 4) | 1;
    }
    return 0;
}

static int huff_decode(Bits *b, const Huff *h) {
    if (b->cnt < MAXBITS) bits_fill(b);
    int e = h->lookup[b->buf & ((1 << MAXBITS) - 1)];
    if (e < 0) return -1;
    bits_drop(b, e & 15);
    return e >> 4;
}

/* ---------------------------------------------------------- constants --- */
static const uint16_t len_base[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
static const uint8_t len_extra[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
static const uint16_t dist_base[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,
    2049,3073,4097,6145,8193,12289,16385,24577 };
static const uint8_t dist_extra[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };
static const uint8_t clen_order[NCLEN] = {
    16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };

/* ------------------------------------------------------------ growing --- */
typedef struct { uint8_t *p; size_t len, cap; } Out;

static int out_need(Out *o, size_t extra) {
    if (o->len + extra <= o->cap) return 0;
    size_t cap = o->cap ? o->cap : (1u << 16);
    while (cap < o->len + extra) cap <<= 1;
    uint8_t *np = realloc(o->p, cap);
    if (!np) return -1;
    o->p = np; o->cap = cap;
    return 0;
}

/* --------------------------------------------------------------- main --- */
enum { BTYPE_STORED = 0, BTYPE_FIXED = 1, BTYPE_DYNAMIC = 2 };

static int inflate_core(Bits *b, Out *o, int *done) {
    Huff lit, dist;
    static int fixed_ready = 0;
    static Huff fixed_lit, fixed_dist;

    for (;;) {
        int final = (int)bits_take(b, 1);
        int type  = (int)bits_take(b, 2);

        if (type == BTYPE_STORED) {
            bits_align(b);
            /* the byte reservoir now holds whole bytes; take from it first */
            int have = b->cnt >> 3;
            uint8_t hdr[4];
            for (int i = 0; i < 4; i++) {
                if (i < have) { hdr[i] = (uint8_t)(b->buf & 0xff); b->buf >>= 8; b->cnt -= 8; }
                else if (b->inpos < b->inlen) hdr[i] = b->in[b->inpos++];
                else { b->overrun = 1; hdr[i] = 0; }
            }
            uint32_t len  = (uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8);
            uint32_t nlen = (uint32_t)hdr[2] | ((uint32_t)hdr[3] << 8);
            if ((len ^ 0xffffu) != nlen) return -1;
            if (out_need(o, len) < 0) return -1;
            for (uint32_t i = 0; i < len; i++) {
                if (b->inpos < b->inlen) o->p[o->len++] = b->in[b->inpos++];
                else { b->overrun = 1; o->p[o->len++] = 0; }
            }
        } else {
            if (type == BTYPE_FIXED) {
                if (!fixed_ready) {
                    uint8_t l[288];
                    for (int i = 0;   i < 144; i++) l[i] = 8;
                    for (int i = 144; i < 256; i++) l[i] = 9;
                    for (int i = 256; i < 280; i++) l[i] = 7;
                    for (int i = 280; i < 288; i++) l[i] = 8;
                    if (huff_build(&fixed_lit, l, 288, 0) < 0) return -1;
                    uint8_t d[30];
                    for (int i = 0; i < 30; i++) d[i] = 5;
                    if (huff_build(&fixed_dist, d, 30, 0) < 0) return -1;
                    fixed_ready = 1;
                }
                lit = fixed_lit; dist = fixed_dist;
            } else if (type == BTYPE_DYNAMIC) {
                int hlit  = (int)bits_take(b, 5) + 257;
                int hdist = (int)bits_take(b, 5) + 1;
                int hclen = (int)bits_take(b, 4) + 4;
                if (hlit > 286 || hdist > 30) return -1;

                uint8_t clen[NCLEN];
                memset(clen, 0, sizeof clen);
                for (int i = 0; i < hclen; i++) clen[clen_order[i]] = (uint8_t)bits_take(b, 3);
                Huff clh;
                if (huff_build(&clh, clen, NCLEN, 0) < 0) return -1;

                uint8_t lens[NLITSYM + NDIST];
                int n = 0, total = hlit + hdist;
                while (n < total) {
                    int s = huff_decode(b, &clh);
                    if (s < 0) return -1;
                    if (s < 16) {
                        lens[n++] = (uint8_t)s;
                    } else {
                        int rep, val = 0;
                        if (s == 16) {
                            if (n == 0) return -1;
                            val = lens[n - 1];
                            rep = 3 + (int)bits_take(b, 2);
                        } else if (s == 17) rep = 3 + (int)bits_take(b, 3);
                        else               rep = 11 + (int)bits_take(b, 7);
                        if (n + rep > total) return -1;
                        while (rep--) lens[n++] = (uint8_t)val;
                    }
                }
                if (lens[256] == 0) return -1;   /* no end-of-block code */
                if (huff_build(&lit,  lens,        hlit,  0) < 0) return -1;
                if (huff_build(&dist, lens + hlit, hdist, 1) < 0) return -1;
            } else {
                return -1;   /* type 3 reserved */
            }

            for (;;) {
                int s = huff_decode(b, &lit);
                if (s < 0) return -1;
                if (s < 256) {
                    if (out_need(o, 1) < 0) return -1;
                    o->p[o->len++] = (uint8_t)s;
                } else if (s == 256) {
                    break;                       /* end of block */
                } else {
                    s -= 257;
                    if (s >= 29) return -1;
                    int length = len_base[s] + (int)bits_take(b, len_extra[s]);
                    int ds = huff_decode(b, &dist);
                    if (ds < 0 || ds >= 30) return -1;
                    int d = dist_base[ds] + (int)bits_take(b, dist_extra[ds]);
                    if ((size_t)d > o->len) return -1;    /* back-ref before start */
                    if (out_need(o, (size_t)length) < 0) return -1;
                    for (int i = 0; i < length; i++) {
                        o->p[o->len] = o->p[o->len - (size_t)d];
                        o->len++;
                    }
                }
            }
        }
        if (final) { *done = 1; return 0; }
    }
}

uint8_t *inflate_alloc(const uint8_t *in, size_t inlen, size_t *outlen) {
    Bits b; Out o;
    int done = 0;
    bits_init(&b, in, inlen);
    o.p = NULL; o.len = 0; o.cap = 0;
    if (inflate_core(&b, &o, &done) < 0) { free(o.p); return NULL; }
    if (outlen) *outlen = o.len;
    return o.p;
}

uint8_t *gunzip_alloc(const uint8_t *in, size_t inlen, size_t *outlen) {
    if (inlen < 18 || in[0] != 0x1f || in[1] != 0x8b || in[2] != 0x08) return NULL;
    uint8_t flg = in[3];
    size_t p = 10;
    if (flg & 0x04) {                        /* FEXTRA */
        if (p + 2 > inlen) return NULL;
        size_t xlen = (size_t)in[p] | ((size_t)in[p + 1] << 8);
        p += 2 + xlen;
    }
    if (flg & 0x08) { while (p < inlen && in[p]) p++; p++; }        /* FNAME */
    if (flg & 0x10) { while (p < inlen && in[p]) p++; p++; }        /* FCOMMENT */
    if (flg & 0x02) p += 2;                                         /* FHCRC */
    if (p >= inlen) return NULL;
    return inflate_alloc(in + p, inlen - p, outlen);
}
