/* cmr2deck.c -- CMR2 native viewer for the Steam Deck.
 *
 * WHAT THIS IS
 *   A native x86_64 Linux program that reads Colin McRae Rally 2.0's OWN files
 *   straight off the disc and draws them on the Deck's GPU through SDL3's
 *   Vulkan backend. No wine, no D3D7, no shim, no pre-extracted intermediates.
 *
 * PIPELINE (all in-process, all from the shipped game data)
 *   .bfl / .c3d
 *     -> gzip inflate            (our own, src/inflate.c -- SteamOS has no zlib.h)
 *     -> CMPR unwrap             ("CMPR" + u32 size, then the real payload)
 *     -> PP_F geometry parse     (header-driven: no per-car hardcoded offsets)
 *     -> BFL archive parse       (trailing 24-byte TOC, DDS blobs in TOC order)
 *     -> DXT5 (BC3) decode       (CPU, to RGBA8)
 *     -> SDL_GPU vertex/index/texture upload
 *     -> Vulkan draw, interactive window on the Deck
 *
 * FORMAT NOTES -- what is PROVEN vs what is INFERRED
 *   PROVEN, verified against real files this session, sizes reconciling to the byte:
 *     - PP_F header fields and the data-block walk order (loader order in C3D.md).
 *       Block layout check on 205a1N:
 *         48 + 15*396 + 14*288 + 0 + 0 + 888*76 + 1204*52 + 888*20 + 1*92
 *            + 27*24 + 27*260 = 165636 = the exact decompressed payload size.
 *     - vertex stride 48, position @ +36, normal @ +12, colour @ +0, uv @ +24.
 *       Tested against five rival layouts: only this one yields 1204/1204 usable
 *       vertices and a car-shaped bbox 3.82 x 1.28 x 1.76 m. Stride 52 collapses
 *       the point cloud onto a unit cube; position @ +0 gives exactly 2x2x2.
 *     - face records 76 bytes, three u16 indices at +52, indexed from C20+12.
 *     - the part record: +12 vblk (byte offset, stride 48), +16 V (vertex count),
 *       +36 face offset (byte offset into the c20 block), +40 F (face count).
 *     - BFL = CMPR container: "CMPR" + u32 size, texture blocks, then the TOC
 *       pointer in the LAST FOUR BYTES of the payload. TOC entries are
 *       {u32 size; u32 offset; u32 nameLen; name padded to 4} and each block
 *       sits at payload + offset. 44 of the 220 car .bfl hold DDS(DXT5), 176
 *       hold TGA -- both decode now. Verified byte-for-byte against an
 *       independent decoder for every block of eight cars (81 blocks).
 *   INFERRED / STILL OPEN, and flagged honestly rather than papered over:
 *     - WHEEL PLACEMENT. DONE (M2, below). The four wheel meshes carry no
 *       position of their own -- the position lives in the .c3d's scene-node
 *       block, and this reads it: nodes 1..4 on 205a1N give wheelbase 2.539 m
 *       and track 1.435 m, which are the car's own numbers. Set NODEPLACE=0 to
 *       fall back to the old mirrored-corner inference (WX/WY/WZ, or
 *       NOWHEELPLACE=1) for an A/B.
 *     - PER-PART TEXTURE ID. Part-record dwords 12/13 hold something material-ish
 *       (9820 / 1620 / 2960 / 4740 ...) but nothing in 0..TEXCOUNT. Textures are
 *       therefore bound by matching the part NAME against the .c3d's own trailer
 *       path table -- data-driven, works across cars, not a 205-only table.
 *
 * usage:  cmr2deck [car] [--game DIR] [--shot FILE] [--list]
 */
#include <SDL3/SDL.h>
#include "inflate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <dirent.h>

/* ---------------------------------------------------------------- utils --- */
static void *xread(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return NULL; }
    void *p = malloc((size_t)n);
    if (fread(p, 1, (size_t)n, f) != (size_t)n) { free(p); fclose(f); return NULL; }
    fclose(f); *len = (size_t)n; return p;
}

/* gzip -> CMPR-unwrapped payload. malloc'd, caller frees. */
static uint8_t *load_res(const char *path, size_t *outlen) {
    size_t n; uint8_t *raw = xread(path, &n);
    if (!raw) return NULL;
    uint8_t *buf = raw; size_t blen = n;
    if (blen > 2 && buf[0] == 0x1f && buf[1] == 0x8b) {
        size_t dl = 0;
        uint8_t *d = gunzip_alloc(buf, blen, &dl);
        if (!d) { free(raw); return NULL; }
        free(raw); buf = d; blen = dl;
    }
    if (blen > 8 && !memcmp(buf, "CMPR", 4)) {
        uint32_t sz; memcpy(&sz, buf + 4, 4);
        if ((size_t)sz + 8 <= blen) { memmove(buf, buf + 8, sz); blen = sz; }
    }
    *outlen = blen; return buf;
}

static uint16_t ru16(const uint8_t *p, size_t o) { uint16_t v; memcpy(&v, p + o, 2); return v; }
static uint32_t ru32(const uint8_t *p, size_t o) { uint32_t v; memcpy(&v, p + o, 4); return v; }

static int includes_ci(const char *hay, const char *needle) {
    size_t nl = strlen(needle); if (!nl) return 0;
    for (const char *s = hay; *s; s++) {
        size_t i = 0;
        while (i < nl && s[i]) {
            char a = s[i], b = needle[i];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) break;
            i++;
        }
        if (i == nl) return 1;
    }
    return 0;
}

/* ------------------------------------------------------------- DXT5/BC3 --- */
/* One 16-byte block -> 4x4 RGBA8, written with the given row pitch. */
static void dxt5_block(const uint8_t *b, uint8_t *out, int pitch) {
    /* BC3 block layout, and this time read in the order the format defines:
     *   b[0..1]    alpha endpoints
     *   b[2..7]    48-bit alpha indices, 3 bits per texel, texel 0 in bits 0..2
     *   b[8..9]    colour endpoint 0 (565), b[10..11] colour endpoint 1
     *   b[12..15]  32-bit colour indices, 2 bits per texel, texel 0 in bits 0..1
     */
    uint16_t c0 = (uint16_t)(b[8] | (b[9] << 8));
    uint16_t c1 = (uint16_t)(b[10] | (b[11] << 8));
    uint8_t r[4], g[4], bl[4];
    int r0 = (c0 >> 11) & 0x1f, g0 = (c0 >> 5) & 0x3f, b0 = c0 & 0x1f;
    int r1 = (c1 >> 11) & 0x1f, g1 = (c1 >> 5) & 0x3f, b1 = c1 & 0x1f;
    r[0] = (uint8_t)((r0 << 3) | (r0 >> 2));
    g[0] = (uint8_t)((g0 << 2) | (g0 >> 4));
    bl[0] = (uint8_t)((b0 << 3) | (b0 >> 2));
    r[1] = (uint8_t)((r1 << 3) | (r1 >> 2));
    g[1] = (uint8_t)((g1 << 2) | (g1 >> 4));
    bl[1] = (uint8_t)((b1 << 3) | (b1 >> 2));
    if (c0 > c1) {                       /* 4-colour mode: 2/3, 1/3, then 1/3, 2/3 */
        r[2]  = (uint8_t)((2 * r[0]  + r[1]) / 3);
        g[2]  = (uint8_t)((2 * g[0]  + g[1]) / 3);
        bl[2] = (uint8_t)((2 * bl[0] + bl[1]) / 3);
        r[3]  = (uint8_t)((r[0]  + 2 * r[1]) / 3);
        g[3]  = (uint8_t)((g[0]  + 2 * g[1]) / 3);
        bl[3] = (uint8_t)((bl[0] + 2 * bl[1]) / 3);
    } else {                             /* 3-colour mode: midpoint, then black */
        r[2]  = (uint8_t)((r[0]  + r[1]) / 2);
        g[2]  = (uint8_t)((g[0]  + g[1]) / 2);
        bl[2] = (uint8_t)((bl[0] + bl[1]) / 2);
        r[3] = g[3] = bl[3] = 0;
    }
    uint8_t a[8];
    a[0] = b[0]; a[1] = b[1];
    if (a[0] > a[1]) {                   /* 8-alpha mode */
        for (int i = 1; i <= 6; i++)
            a[i + 1] = (uint8_t)(((7 - i) * a[0] + i * a[1]) / 7);
    } else {                             /* 6-alpha mode: 4 values, then 0 and 255 */
        for (int i = 1; i <= 4; i++)
            a[i + 1] = (uint8_t)(((5 - i) * a[0] + i * a[1]) / 5);
        a[6] = 0; a[7] = 255;
    }
    uint32_t cbits = (uint32_t)b[12] | ((uint32_t)b[13] << 8) |
                     ((uint32_t)b[14] << 16) | ((uint32_t)b[15] << 24);
    uint64_t abits = 0;
    for (int i = 0; i < 6; i++) abits |= (uint64_t)b[2 + i] << (8 * i);
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            int i  = y * 4 + x;
            int ci = (int)((cbits >> (2 * i)) & 3);
            int ai = (int)((abits >> (3 * i)) & 7);
            uint8_t *o = out + y * pitch + x * 4;
            o[0] = r[ci]; o[1] = g[ci]; o[2] = bl[ci]; o[3] = a[ai];
        }
    }
}

/* A full DDS blob -> RGBA8. DXT5 is what every car texture uses; uncompressed
 * 32bpp is handled too because some of the shared banks use it. */
static uint8_t *dds_decode(const uint8_t *d, size_t n, int *w, int *h) {
    if (n < 128 || memcmp(d, "DDS ", 4)) return NULL;
    uint32_t flags = ru32(d, 8);
    int W = (int)ru32(d, 16), H = (int)ru32(d, 12);
    uint32_t pf     = ru32(d, 80);
    if (W <= 0 || H <= 0 || W > 8192 || H > 8192) return NULL;
    const uint8_t *src = d + 128;
    size_t srclen = n - 128;
    uint8_t *out = calloc((size_t)W * H * 4, 1);
    if (!out) return NULL;
    int isdxt5 = !memcmp(d + 84, "DXT5", 4) || !memcmp(d + 84, "DX5 ", 4) ||
                 !memcmp(d + 84, "ATI2", 4);
    if (isdxt5 || (pf & 0x4) == 0) {
        /* BC3 block walk. The 27 car textures are all DXT5. */
        if (!isdxt5) { free(out); return NULL; }
        int bw = (W + 3) / 4, bh = (H + 3) / 4;
        for (int by = 0; by < bh; by++) {
            for (int bx = 0; bx < bw; bx++) {
                size_t off = ((size_t)by * bw + bx) * 16;
                if (off + 16 > srclen) goto done;
                uint8_t blk[64];
                dxt5_block(src + off, blk, 16);
                for (int y = 0; y < 4; y++) {
                    int py = by * 4 + y; if (py >= H) break;
                    for (int x = 0; x < 4; x++) {
                        int px = bx * 4 + x; if (px >= W) break;
                        memcpy(out + ((size_t)py * W + px) * 4, blk + y * 16 + x * 4, 4);
                    }
                }
            }
        }
    } else if (flags & 0x40) {   /* DDSD_LINEARSIZE / uncompressed 32bpp BGRA */
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                size_t s = ((size_t)y * W + x) * 4;
                if (s + 4 > srclen) goto done;
                uint8_t *o = out + s;
                o[0] = src[s + 2]; o[1] = src[s + 1]; o[2] = src[s + 0]; o[3] = src[s + 3];
            }
        }
    } else { free(out); return NULL; }
done:
    *w = W; *h = H;
    return out;
}

/* ------------------------------------------------------------- .tga blobs ---
 * 176 of the 220 car .bfl files hold .tga, not .dds. Until this existed the
 * whole ".tga" half of the game's car textures was unreachable: bfl_parse
 * scored blocks against "DDS " only, so a TGA container scored 0 and was
 * rejected whole, and 205c1 rendered 27 placeholders and a white car.
 *
 * The blocks are plain TGA: 18-byte header, no colour map, no id field, type 2
 * (uncompressed truecolour), 24 or 32 bpp, descriptor 0x08 (alpha present,
 * origin bottom-left), then a 26-byte TGA 2.0 footer. RLE (type 10) is decoded
 * too -- not because any block here uses it, but because a TGA that suddenly
 * does should not go quietly white.
 */
static void tga_store(uint8_t *out, int W, int H, int topdown, int bpp,
                      size_t idx, const uint8_t *px) {
    size_t y = idx / (size_t)W, x = idx % (size_t)W;
    if (!topdown) y = (size_t)H - 1 - y;          /* origin bottom-left */
    uint8_t *o = out + (y * (size_t)W + x) * 4;
    o[0] = px[2]; o[1] = px[1]; o[2] = px[0];     /* BGR(A) -> RGBA */
    o[3] = (bpp == 4) ? px[3] : 0xff;
}

static uint8_t *tga_decode(const uint8_t *d, size_t n, int *w, int *h) {
    if (n < 18) return NULL;
    int idlen = d[0], cmap = d[1], type = d[2];
    int W = (int)ru16(d, 12), H = (int)ru16(d, 14);
    int depth = d[16], desc = d[17];
    if (cmap || idlen > 255) return NULL;
    if (type != 2 && type != 10) return NULL;
    if (depth != 24 && depth != 32) return NULL;
    if (W <= 0 || H <= 0 || W > 8192 || H > 8192) return NULL;
    int bpp = depth / 8;
    size_t total = (size_t)W * (size_t)H;
    size_t p = 18 + (size_t)idlen, k = 0;
    if (p >= n) return NULL;
    uint8_t *out = calloc(total * 4, 1);
    if (!out) return NULL;
    int topdown = (desc & 0x20) != 0;             /* bit 5 set = top-left */
    if (type == 2) {
        if (p + total * (size_t)bpp > n) { free(out); return NULL; }
        const uint8_t *src = d + p;
        for (; k < total; k++)
            tga_store(out, W, H, topdown, bpp, k, src + k * (size_t)bpp);
    } else {                                      /* type 10, run-length */
        uint8_t buf[4];
        while (k < total) {
            if (p >= n) break;
            int hdr = d[p++], run = (hdr & 0x7f) + 1;
            if (hdr & 0x80) {
                if (p + (size_t)bpp > n) break;
                memcpy(buf, d + p, (size_t)bpp); p += (size_t)bpp;
                for (int r = 0; r < run && k < total; r++, k++)
                    tga_store(out, W, H, topdown, bpp, k, buf);
            } else {
                for (int r = 0; r < run && k < total; r++, k++) {
                    if (p + (size_t)bpp > n) break;
                    memcpy(buf, d + p, (size_t)bpp); p += (size_t)bpp;
                    tga_store(out, W, H, topdown, bpp, k, buf);
                }
            }
        }
    }
    *w = W; *h = H;
    return out;
}

/* One door for both containers. The magic decides, not the filename: a block
 * whose TOC entry says .tga but whose bytes say "DDS " still decodes. */
static uint8_t *tex_decode(const uint8_t *d, size_t n, int *w, int *h) {
    if (n >= 4 && !memcmp(d, "DDS ", 4)) return dds_decode(d, n, w, h);
    if (n >= 18 && d[1] == 0 && (d[2] == 2 || d[2] == 10) &&
        (d[16] == 24 || d[16] == 32))
        return tga_decode(d, n, w, h);
    return NULL;
}

/* ------------------------------------------------------------ BFL archive --- */
#define BFL_MAX 1024
typedef struct { char name[64]; size_t off, len; } BflEnt;
typedef struct { BflEnt e[BFL_MAX]; int n; } Bfl;

/* ---- .bfl archive ---------------------------------------------------------
 * The payload is  gzip -> "CMPR" + 8-byte blob header, then a run of texture
 * blocks, then a table of contents at the very END.
 *
 *   block  = 8-byte prefix + "DDS " + 124-byte DDS header + pixel data
 *   TOC    = N x 24-byte records:  char name[12]  u32 offset  u32 size  u32 12
 *
 * `offset` is absolute from the start of the payload and points at the block's
 * 8-byte PREFIX, so the "DDS " magic is at offset+8. Sizes are contiguous:
 * offset[i+1] == offset[i] + size[i].
 *
 * The trap: the FINAL record is truncated to 16 bytes -- name + offset, no
 * size. The old code walked back from `len` in whole 24-byte steps and so
 * landed 8 bytes into the previous record, failed its first probe and returned
 * 0 textures. Everything rendered flat white.
 *
 * Fix: locate the TOC as the longest run of 24-byte-strided "<name>.dds"
 * records whose end is either exactly the payload end or 8 bytes short of it.
 * Then validate every offset against the "DDS " magic -- nothing here is
 * trusted, every field is checked before it is used.
 */
static int bfl_ext_ok(const uint8_t *p, size_t o) {
    return !memcmp(p + o + 8, ".dds", 4) || !memcmp(p + o + 8, ".tga", 4);
}

static int bfl_rec_ok(const uint8_t *p, size_t len, size_t o) {
    if (o + 16 > len) return 0;
    if (!bfl_ext_ok(p, o)) return 0;
    for (int i = 0; i < 8; i++) {
        int c = p[o + i];
        if (c < 0x21 || c > 0x7e) return 0;
    }
    return 1;
}

/* Score a candidate run by how many of its records point at a block whose
 * magic actually validates. That is what makes the TOC findable without
 * relying on where in the file it happens to stop. */
static int bfl_score(const uint8_t *p, size_t len, size_t start, int cnt) {
    int val = 0;
    for (int i = 0; i < cnt; i++) {
        size_t o = start + (size_t)i * 24;
        if (o + 20 > len) break;
        size_t off = ru32(p, o + 16);
        if (off + 16 > len) continue;
        if (!memcmp(p + off, "DDS ", 4)) val++;
        else if (!memcmp(p + off + 8, "DDS ", 4)) val++;
        else if (!memcmp(p + off, "-XFILE.", 7)) val++;
    }
    return val;
}

static int bfl_parse_heuristic(const uint8_t *p, size_t len, Bfl *out) {
    out->n = 0;
    if (len < 64) return 0;

    size_t toc = 0; int count = 0, bestscore = -1;
    for (size_t s = 0; s + 16 <= len; s++) {
        if (!bfl_rec_ok(p, len, s)) continue;
        size_t k = s; int cnt = 0;
        while (cnt < BFL_MAX && bfl_rec_ok(p, len, k)) { cnt++; k += 24; }
        int sc = bfl_score(p, len, s, cnt);
        /* prefer the run that validates the most records; break ties on the
         * run that reaches furthest into the payload */
        if (sc > bestscore || (sc == bestscore && cnt > count)) {
            bestscore = sc; count = cnt; toc = s;
        }
        s = k;                                  /* the run consumed these bytes */
    }
    if (count < 1 || bestscore < 1) return 0;

    int n = 0;
    for (int i = 0; i < count && n < BFL_MAX; i++) {
        size_t o = toc + (size_t)i * 24;
        if (o + 16 > len) break;

        char nm[13];
        memcpy(nm, p + o, 12); nm[12] = 0;
        char *dot = strchr(nm, '.'); if (dot) *dot = 0;
        for (char *c = nm; *c; c++) if (*c >= 'A' && *c <= 'Z') *c += 32;

        /* The record is (name, size, offset, 12). Trust neither field on its
         * own -- accept only whichever one lands on a real block magic, and
         * fall back to the other if the first guess is wrong. The final record
         * in every one of these files is truncated to name + one u32, so a
         * record with no size gets one derived from the next block. */
        size_t f0 = ru32(p, o + 12);
        size_t f1 = (o + 20 <= len) ? ru32(p, o + 16) : 0;
        size_t off = 0, blen = 0;
        size_t cand[2] = { f1, f0 };
        for (int c = 0; c < 2; c++) {
            size_t v = cand[c];
            if (!v || v + 16 > len) continue;
            int magic = !memcmp(p + v, "DDS ", 4) || !memcmp(p + v, "-XFILE.", 7);
            int magic8 = (v + 8 + 4 <= len) ? !memcmp(p + v + 8, "DDS ", 4) : 0;
            if (!magic && !magic8) continue;
            off  = magic8 ? v + 8 : v;
            blen = (c == 0 ? f0 : f1);
            break;
        }
        if (!off) continue;
        size_t next = (i + 1 < count) ? (size_t)ru32(p, o + 24 + 16) : len;
        if (blen < 128 || off + blen > len || blen > len - off) blen = 0;
        if (!blen) { if (next > off) blen = next - off; else continue; }

        snprintf(out->e[n].name, sizeof out->e[n].name, "%s", nm);
        out->e[n].off = off;
        out->e[n].len = blen;
        n++;
    }
    out->n = n;
    return n;
}

/* ---- the container spec, from the MOD (not from my guessing) --------------
 * Verified against the published reader -- work/cmr2lab-publish/tools/
 * bfl-read.py, which is the BFLExtractCsharp patterns/bfl.hexpat spec:
 *
 *   "CMPR" | u32 containerSize          (8-byte header; containerSize counts
 *                                        the bytes AFTER the 8-byte header)
 *   ... pixel blocks, block i at absolute offset  entry[i].offset + 8  ...
 *   u32 tocPointer                      <- the LAST 4 BYTES of the payload
 *                                          (== containerSize + 4)
 *   TOC at tocPointer + 8, entries of:
 *        u32 size; u32 offset; u32 strLen; char name[strLen], padded to 4
 *
 * The previous parser guessed a 24-byte stride backwards off the end and
 * scored a candidate TOC by validating "DDS " magics. Both the stride and the
 * scorer are guesses, and both are wrong in the same direction:
 *   - it found 26 of 27 blocks on 205a1 (the truncated-looking last record
 *     is not truncated at all under this spec -- the name is variable-length),
 *   - it validated nothing on a .tga container, so 176 of the 220 car .bfl
 *     files were rejected whole and their cars drew flat white.
 * With the spec there is no scoring, no stride and no guessing: the pointer
 * is where the format says it is, and every field is range-checked before use.
 * The heuristic stays underneath as a fallback for a payload that is not CMPR.
 *
 * Checked on both kinds: 205a1 (dds, 27/27) and 205c1 (tga, 27/27).
 */
static int bfl_parse_cmpr(const uint8_t *p, size_t len, Bfl *out) {
    out->n = 0;
    /* load_res() normally strips "CMPR" + containerSize and hands over the
     * payload alone. Accept both -- see the coordinate note above. */
    size_t pay = (len >= 16 && !memcmp(p, "CMPR", 4)) ? 8 : 0;
    if (len < pay + 16) return 0;
    size_t plen   = len - pay;                       /* payload length */
    uint32_t tocptr = ru32(p, len - 4);              /* last 4 bytes of the file */
    if ((size_t)tocptr + 12 > plen) return 0;
    size_t pos = pay + (size_t)tocptr;

    int n = 0;
    while (n < BFL_MAX && pos + 12 <= len - 4) {
        uint32_t size = ru32(p, pos), off = ru32(p, pos + 4), slen = ru32(p, pos + 8);
        pos += 12;
        if (slen < 1 || slen > 48) break;            /* end of the TOC */
        if (pos + slen > len) break;
        char nm[64];
        memcpy(nm, p + pos, slen);
        nm[slen] = 0;
        for (uint32_t k = 0; k < slen; k++)
            if (nm[k] < 0x20 || nm[k] > 0x7e) return n;   /* not a TOC after all */
        pos += slen + ((4 - (slen % 4)) % 4);        /* pad to 4 */

        size_t data = pay + (size_t)off;
        if (size < 16 || data + size > len) continue;
        char *dot = strchr(nm, '.'); if (dot) *dot = 0;
        for (char *c = nm; *c; c++) if (*c >= 'A' && *c <= 'Z') *c += 32;
        snprintf(out->e[n].name, sizeof out->e[n].name, "%s", nm);
        out->e[n].off = data;
        out->e[n].len = size;
        n++;
    }
    out->n = n;
    return n;
}

static int bfl_parse(const uint8_t *p, size_t len, Bfl *out) {
    int n = bfl_parse_cmpr(p, len, out);
    if (n > 0) return n;
    printf("[WARN] .bfl is not a CMPR container -- falling back to the heuristic\n");
    return bfl_parse_heuristic(p, len, out);
}

static const uint8_t *bfl_find(const Bfl *b, const uint8_t *p, const char *name, size_t *blen) {
    char key[32];
    snprintf(key, sizeof key, "%s", name);
    char *dot = strchr(key, '.'); if (dot) *dot = 0;
    for (char *c = key; *c; c++) if (*c >= 'A' && *c <= 'Z') *c += 32;
    for (int i = 0; i < b->n; i++)
        if (!strcmp(b->e[i].name, key)) {
            if (blen) *blen = b->e[i].len;
            return p + b->e[i].off;
        }
    return NULL;
}

/* ----------------------------------------------------------- PP_F / .c3d --- */
#define MAXPARTS 64
#define MAXTEX   64
#define VSTRIDE  36

/* ---- .c3d block layout -------------------------------------------------
 * Taken from the game's own loader, Sector_RelocateStageMeshFile
 * (CMR2 0x004b93c0), decompiled in .lena_cmr2/port/tree/CMR2Decomp/Sector.cpp.
 * Every constant below is that function's arithmetic, and all of it reconciles
 * byte-exact against 205a1N.c3d:
 *
 *   48 + 15*396 + (14+0)*288 + 0*160 + 0*136 + 0*28 + 888*76
 *      + 1204*48 + 1204*4 + 888*20 + 1*92            = 157968
 *   157968 + 27*24 + 27*260                            = 165636 = payload
 *
 * The trap that cost a session: the vertex block starts at pVertexData with NO
 * leading pad, and the vertex record is a plain D3D7 FVF --
 *   +0 position 3f, +12 normal 3f, +24 diffuse DWORD, +28 specular DWORD,
 *   +32 uv set 0 (2f), +40 uv set 1 (2f)     = 0x30 = 48 bytes
 * An earlier version used VBLK = C14+12 with position at +36, which is
 * algebraically the SAME BYTES AS THE NEXT VERTEX's position: every vertex was
 * drawn carrying its neighbour's coordinates and its own normal/uv read out of
 * the wrong place. The silhouette still looked car-shaped, which is why it
 * survived. Proven by dumping both bases: at 77508 the "normal" is unit length
 * and the diffuse word is 0xffffffff; at 77520 it is NaN and 0.5.
 * ---------------------------------------------------------------------- */
#define MESH_STRIDE      288     /* 0x120  Mesh record                 */
#define NODE_STRIDE      396     /* 0x18c  scene node                  */
#define TRIANGLE_STRIDE   76     /* 0x4c   MeshTriangle                */
#define VERTEX_STRIDE     48     /* 0x30   FVF vertex                  */
#define TRI_INDEX_OFF     64     /* 0x40   MeshTriangle::vertexIndex   */
#define MESH_VBLOFF       12     /* 0x0c   Mesh::pVertexData (bytes)   */
#define MESH_VCOUNT       16     /* 0x10   Mesh::field_0x10            */
#define MESH_TRIOFF       36     /* 0x24   Mesh::pTriangles   (bytes)  */
#define MESH_TCOUNT       40     /* 0x28   Mesh::triangleCount         */
#define NODE_OBJ          12     /* 0x0c   SceneNode::pObject (mesh off)*/
#define NODE_LOCAL        0x58   /*        SceneNode::local  (FixMatrix)*/
#define NODE_TYPE        0x178  /*        SceneNode::type             */
#define MESH_FLAGS       0x30   /* 0x30   Mesh::flags                 */
#define TRI_TEXOFF       4      /* 0x04   MeshTriangle texture, field_0x2c = 0 */

#define MESH_INDEXED 0
#define MESH_STRIP   1

typedef struct {
    char     name[16];
    uint32_t vblk;      /* byte offset of this part's vertices, stride 48 */
    uint32_t V;         /* vertex count in the file */
    uint32_t facoff;    /* byte offset of this part's face records, stride 76 */
    uint32_t F;         /* face count in the file */
    uint32_t built;     /* indices we actually emitted */
    uint32_t istart;    /* first index in the combined index buffer */
    uint32_t vstart;    /* first vertex of this part in the combined buffer */
    int      tex;            /* fallback only: the name-matched pick_tex()  */
    unsigned int meshflags;  /* Mesh::flags, +0x30: cull, alpha, lighting   */
    int      transparent;
    int      wheel;
} C3dPart;

/* One scene node's placement of one part: a 3x3 rotation and a translation,
 * both already converted out of 16.16 to metres. */
typedef struct {
    int   have, node, identity;
    float m[9], t[3];
} NodeX;

/* One draw: a contiguous run of triangles that share one texture.
 * This is the game's own unit of work -- Game_DrawMeshTextureRuns walks the
 * triangle list in order and starts a new draw whenever the texture changes,
 * and the texture is the int32 at record+4 + field_0x2c*4 (field_0x2c = 0). */
typedef struct {
    uint32_t istart, icount;   /* into C3d::idx                              */
    int      tex;              /* index into the .c3d texture-name table     */
    int      alpha;            /* mesh flag bit 3, as Graphics_DrawMeshLOD   */
    int      part;             /* which part, for the log                    */
} DrawRun;

typedef struct {
    uint8_t *p; size_t len;
    C3dPart  part[MAXPARTS];
    int      nparts;
    uint8_t *verts; int nverts;      /* VSTRIDE each */
    uint32_t *idx;   int nidx;
    size_t   vcap, icap;             /* hard capacities -- never write past */
    int      ntri;
    long     cfile_faces;            /* face records in the file (c20) */
    float    lo[3], hi[3];
    int      ntex;
    char     texname[MAXTEX][32];
    DrawRun *run; int nruns, runcap;   /* per-triangle texture runs */
} C3d;

/* WHICH READING OF THE PART RECORD WE ARE RENDERING.  See the long comment
 * above c3d_load.  0 = indexed (the default, and the answer), 1 = triangle
 * strip.  Both are implemented so the two can be rendered side by side. */
static int g_mesh_mode = 0;

static void vtx_out(const uint8_t *base, size_t byteoff, uint32_t i, uint8_t *dst) {
    const uint8_t *b = base + byteoff + (size_t)i * VERTEX_STRIDE;
    memcpy(dst,      b +  0, 12);   /* position @ +0  (D3DFVF_XYZ)    */
    memcpy(dst + 12, b + 12, 12);   /* normal   @ +12 (D3DFVF_NORMAL) */
    memcpy(dst + 24, b + 24,  4);   /* diffuse  @ +24                 */
    memcpy(dst + 28, b + 32,  8);   /* uv set 0 @ +32 (D3DFVF_TEX0)   */
}

/* part name -> texture, resolved against the .c3d's own texture path table. */
static int pick_tex(const C3d *c, const char *partname) {
    static const struct { const char *part; const char *want; } R[] = {
        { "whl",   "whd"   }, { "wheel", "whd"   },
        { "semit", "gli"   }, { "glass", "gli"   }, { "gl",    "gli"   },
        { "light", "lig"   },
        { "und",   "und"   },
        { "boot",  "bodbu" },
    };
    for (size_t i = 0; i < sizeof R / sizeof R[0]; i++)
        if (includes_ci(partname, R[i].part))
            for (int t = 0; t < c->ntex; t++)
                if (includes_ci(c->texname[t], R[i].want)) return t;
    for (int t = 0; t < c->ntex; t++) if (includes_ci(c->texname[t], "nwbdf")) return t;
    for (int t = 0; t < c->ntex; t++) if (includes_ci(c->texname[t], "bodbu")) return t;
    return 0;
}

/* ---------------------------------------------------------- THE TWO MESHES ---
 * A part record holds V vertices (stride 48, position at +36) and F face
 * records of 76 bytes.  Two readings of that exist in my notes and they
 * contradict each other, so both are built here and rendered side by side:
 *
 *   INDEXED (--mesh indexed, default)
 *     vertex i of a part is vertex i of the part's block.  Triangle k is the
 *     three u16 at face record k + 52, indices into that SAME part's block
 *     starting at 0.  Vertices are shared; the index buffer carries topology.
 *
 *   STRIP (--mesh strip)
 *     there is no index buffer at all: the part's vertices are consumed in
 *     file order and triangle k is (vstart+k, vstart+k+1, vstart+k+2), with the
 *     winding flipped on odd k.  Face records are ignored for topology.
 *
 * SETTLED as INDEXED.  Measurements from tools/c3dprobe.c over the whole
 * Car/ directory are in work/CMR2/CHECKPOINT.md; the two that decide it:
 *   - 888/888 face records on 205a1N hold a triple that is in range [0,V) for
 *     that part with no repeated index.  For the wheel (V=86) three unrelated
 *     u16 land inside 0..85 about once in 4.4e8, so this is not chance.
 *   - the INDEXED reading reconstructs exactly 888 triangles, which is what the
 *     header (c20=888) and the 888*76-byte block both say.  The STRIP reading
 *     invents 1176 and has to throw the face block away entirely.
 * The strip path survives as the falsifiable control, not as a fallback.
 * ---------------------------------------------------------------------------- */

typedef struct {
    int   tris, degenerate, holes;
    double bridge_pct, median_edge_ratio, max_edge;
} MeshStats;

static float edge_len(const uint8_t *vp, uint32_t a, uint32_t b) {
    const float *A = (const float *)(vp + (size_t)a * VSTRIDE);
    const float *B = (const float *)(vp + (size_t)b * VSTRIDE);
    float x = A[0] - B[0], y = A[1] - B[1], z = A[2] - B[2];
    return sqrtf(x * x + y * y + z * z);
}

static int cmp_f(const void *a, const void *b) {
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

static int c3d_load(C3d *c, const char *path, int wheelplace, int nodeplace,
                    float wx, float wy, float wz) {
    memset(c, 0, sizeof *c);
    c->p = load_res(path, &c->len);
    if (!c->p) { fprintf(stderr, "[ERR] cannot read %s\n", path); return 0; }
    if (c->len < 64 || memcmp(c->p, "PP_F", 4)) {
        fprintf(stderr, "[ERR] %s: no PP_F magic\n", path); return 0;
    }
    const uint8_t *h = c->p;
    uint32_t c10 = ru32(h, 0x10), c14 = ru32(h, 0x14);
    uint16_t c18 = ru16(h, 0x18), c1a = ru16(h, 0x1a), c1c = ru16(h, 0x1c);
    uint16_t c1e = ru16(h, 0x1e), c28 = ru16(h, 0x28), c26 = ru16(h, 0x26);
    uint16_t c24 = ru16(h, 0x24);
    uint32_t c20 = ru32(h, 0x20);

    size_t C1A   = 48 + (size_t)c18 * NODE_STRIDE;             /* pMeshArray    */
    size_t OBJ   = C1A + (size_t)(c1a + c1c) * MESH_STRIDE;    /* pStageObjects */
    size_t SEC   = OBJ + (size_t)c1c * 160;                    /* pSectors      */
    size_t C20   = SEC + (size_t)c1e * 136 + (size_t)c28 * 28; /* pTriangles    */
    size_t VBLK  = C20 + (size_t)c20 * TRIANGLE_STRIDE;        /* pVertexData   */
    size_t VEND  = VBLK + (size_t)c14 * VERTEX_STRIDE;
    size_t LIGHT = VEND;
    size_t FLAGS = LIGHT + (size_t)c14 * 4;
    size_t FEND  = VBLK;
    size_t TRAIL = FLAGS + (size_t)c10 * 20 + (size_t)c26 * 92;

    /* structural sanity -- every one of these has bitten us */
    if (C1A + (size_t)(c1a + c1c) * MESH_STRIDE > c->len || VBLK > c->len ||
        TRAIL > c->len) {
        fprintf(stderr, "[ERR] %s: block layout runs past the payload (%zu bytes)\n",
                path, c->len);
        return 0;
    }

    if (c1a > MAXPARTS) c1a = MAXPARTS;
    c->nparts = c1a;

    /* Texture name table: the loader uses the absolute pointer at +0xc
     * (textureRecords = *(int *)(pData+0xc) + pData). The tail-relative form
     * (TRAIL + c24*24) lands on the same byte in every file we have. Prefer the
     * header, fall back to the tail so one bad field cannot lose the names. */
    size_t texbase = ru32(h, 0x0c);
    if (texbase < TRAIL || texbase + (size_t)c24 * 260 > c->len)
        texbase = TRAIL + (size_t)c24 * 24;
    c->ntex = c24 > MAXTEX ? MAXTEX : c24;
    for (int i = 0; i < c->ntex; i++) {
        size_t b = texbase + (size_t)i * 260;
        if (b + 260 > c->len) { c->ntex = i; break; }
        const char *pth = (const char *)(c->p + b), *base = pth;
        for (const char *s = pth; *s; s++) if (*s == '\\' || *s == '/') base = s + 1;
        snprintf(c->texname[i], sizeof c->texname[i], "%s", base);
    }

    /* ---------------------------------------------------------------------
     * CAPACITY.  THE BUG THAT KILLED THIS PROGRAM.
     *   old: capacity = sum(V) * 36 = 1204 * 36 = 43344 bytes
     *        written   = sum(F)*3 * 36 = 2664 * 36 = 95904 bytes
     *        -> 52560 bytes heap overflow -> glibc "malloc(): corrupted top
     *           size" and a core dump, every single run.
     * Neither reading ever needs more than sum(V) vertices: INDEXED shares
     * vertices, STRIP consumes each once. So the budget is sum(V), and it is
     * ENFORCED rather than assumed.
     * ------------------------------------------------------------------- */
    long totv = 0, totf = 0;
    for (int i = 0; i < c1a; i++) {
        const uint8_t *r = c->p + C1A + (size_t)i * MESH_STRIDE;
        totv += (long)ru32(r, MESH_VCOUNT);
        totf += (long)ru32(r, MESH_TCOUNT);
    }
    c->cfile_faces = totf;
    c->vcap = (size_t)totv + 8;
    /* INDEXED needs 3*sum(F).  STRIP needs 3*sum(V-2), which is LARGER on every
     * file we have (+46% overall).  Budget for whichever is bigger, or the strip
     * control gets silently truncated and the comparison is worthless -- which is
     * exactly what happened on the first run (icap 2670/2672, 6 parts left empty). */
    c->icap = 3 * (size_t)(totf > totv ? totf : totv) + 8;
    if (c->vcap > 4u << 20 || c->icap > 16u << 20) {
        fprintf(stderr, "[ERR] %s: absurd part counts (V=%ld F=%ld)\n", path, totv, totf);
        return 0;
    }
    c->verts = calloc(c->vcap, VSTRIDE);
    c->idx   = malloc(c->icap * sizeof(uint32_t));
    float *edges = malloc(c->icap * sizeof(float));
    if (!c->verts || !c->idx || !edges) { fprintf(stderr, "[ERR] out of memory\n"); return 0; }

    MeshStats S; memset(&S, 0, sizeof S);
    float lo[3] = { 1e9f, 1e9f, 1e9f }, hi[3] = { -1e9f, -1e9f, -1e9f };
    int corner = 0, nedge = 0, truncated = 0, fb_bad = 0;
    int nametex = getenv("NAMETEX") ? 1 : 0;   /* the OLD path, for the A/B */
    int texruns = 0;                 /* runs created from the file's texture IDs */
    int texcount[MAXTEX]; memset(texcount, 0, sizeof texcount);
    int runsat[MAXPARTS]; memset(runsat, 0, sizeof runsat);
    int texruns_before = 0;

    /* ---- M2: the node transforms, straight out of the file ----------------
     * Every mesh in a car is instanced by a scene node, and the node carries
     * the 16.16 transform. This is where the wheels have always been: the
     * part records hold geometry only, no position, which is why the previous
     * code had to mirror corner signs and guess. Node -> mesh is the pObject
     * field, a byte offset into the mesh array, so the part index is
     * pObject / 288 and it is worth checking that it divides exactly.
     * --------------------------------------------------------------------- */
    NodeX nx[MAXPARTS];
    memset(nx, 0, sizeof nx);
    int nplaced = 0;
    float idmove = 0.0f;   /* worst vertex move by an identity node */
    int nidbad = 0;
    if (nodeplace) {
        for (int n = 0; n < (int)c18 && n < 4096; n++) {
            const uint8_t *r = c->p + 48 + (size_t)n * NODE_STRIDE;
            uint32_t obj = ru32(r, NODE_OBJ);
            if (obj == 0xffffffffu || obj % MESH_STRIDE) continue;
            uint32_t mi = obj / MESH_STRIDE;
            if (mi >= (uint32_t)c1a || (int)mi >= MAXPARTS) continue;
            if (ru32(r, NODE_TYPE) != 0) continue;          /* 0 = SCENE_NODE_MESH */
            if (nx[mi].have) continue;                      /* two nodes, one mesh */
            /* 16.16 fixed -> metres. Read as int32 and scaled; do NOT write
             * through a const array (that optimises to garbage). */
            /* FixMatrix is FOUR 4-word rows carried as 16 words:
             *   right.xyz (0..2), rw (3), up.xyz (4..6), uw (7),
             *   forward.xyz (8..10), fw (11), position.xyz (12..14), pw (15)
             * The w word sits BETWEEN the vectors, so the rotation is words
             * 0,1,2 / 4,5,6 / 8,9,10 and NOT the first nine words. Reading the
             * first nine gives a matrix that looks like the identity when
             * printed (1,0,0,0,0,1,0,0,0) and shears every vertex with it:
             * y came out 0 and z took y's value, which flattened the whole car
             * to 0.36 m tall. Exactly the failure this file keeps producing --
             * arithmetic that runs, produces a shape, and is wrong. */
            float f[16];
            for (int k = 0; k < 16; k++)
                f[k] = (float)(int32_t)ru32(r, NODE_LOCAL + 4 * k) / 65536.0f;
            nx[mi].m[0] = f[0]; nx[mi].m[1] = f[1]; nx[mi].m[2] = f[2];
            nx[mi].m[3] = f[4]; nx[mi].m[4] = f[5]; nx[mi].m[5] = f[6];
            nx[mi].m[6] = f[8]; nx[mi].m[7] = f[9]; nx[mi].m[8] = f[10];
            for (int k = 0; k < 3; k++) nx[mi].t[k] = f[12 + k];
            /* identity test on the 16.16 WORDS, at the padded offsets */
            int ident = ((int32_t)ru32(r, NODE_LOCAL +  0) == 65536 &&
                         (int32_t)ru32(r, NODE_LOCAL +  4) == 0     &&
                         (int32_t)ru32(r, NODE_LOCAL +  8) == 0     &&
                         (int32_t)ru32(r, NODE_LOCAL + 16) == 0     &&
                         (int32_t)ru32(r, NODE_LOCAL + 20) == 65536 &&
                         (int32_t)ru32(r, NODE_LOCAL + 24) == 0     &&
                         (int32_t)ru32(r, NODE_LOCAL + 32) == 0     &&
                         (int32_t)ru32(r, NODE_LOCAL + 36) == 0     &&
                         (int32_t)ru32(r, NODE_LOCAL + 40) == 65536);
            nx[mi].have = 1;
            nx[mi].node = n;
            /* "trivial" = no rotation AND no translation, which is every body
             * part. Those must not move a single vertex; anything else is a
             * misread matrix and this counter is what says so. */
            nx[mi].identity = ident && f[12] == 0.0f && f[13] == 0.0f && f[14] == 0.0f;
            if (getenv("NODEDEBUG")) {
                printf("[NDBG] node %d raw:", n);
                for (int k = 0; k < 16; k++) printf(" %d", (int32_t)ru32(r, NODE_LOCAL + 4 * k));
                printf("\n        f:");
                for (int k = 0; k < 16; k++) printf(" %.3f", f[k]);
                printf("\n        m:");
                for (int k = 0; k < 9; k++) printf(" %.3f", nx[mi].m[k]);
                printf("  ident=%d\n", ident);
            }
            char nm[13];
            memcpy(nm, c->p + C1A + (size_t)mi * MESH_STRIDE, 12); nm[12] = 0;
            for (char *q2 = nm; *q2; q2++) if (*q2 == ' ') *q2 = 0;
            printf("[NODE] part %2u %-12s <- node %2d  pos=(%.3f, %.3f, %.3f)%s\n",
                   mi, nm, n, nx[mi].t[0], nx[mi].t[1], nx[mi].t[2],
                   ident ? "  [identity]" : "  [mirrored/rotated]");
        }
    }

    for (int i = 0; i < c1a; i++) {
        const uint8_t *r = c->p + C1A + (size_t)i * MESH_STRIDE;
        C3dPart *P = &c->part[i];
        memcpy(P->name, r, 12); P->name[12] = 0;
        for (char *s = P->name; *s; s++) if (*s == ' ') *s = 0;
        P->vblk   = ru32(r, MESH_VBLOFF);
        P->V      = ru32(r, MESH_VCOUNT);
        P->facoff = ru32(r, MESH_TRIOFF);
        P->F      = ru32(r, MESH_TCOUNT);
        /* The texture for this part is the one its triangles name; pick_tex()
         * survives only for the STRIP control path, which throws the face
         * records away. The name match is NOT the answer any more. */
        P->tex    = pick_tex(c, P->name);
        P->meshflags = ru32(r, MESH_FLAGS);
        /* alpha: mesh flags bit 3. cull: bit 0. Both read, neither guessed. */
        P->transparent = (P->meshflags >> 3) & 1;
        P->wheel  = includes_ci(P->name, "whl") || includes_ci(P->name, "wheel");
        P->istart = (uint32_t)c->nidx;
        P->vstart = (uint32_t)c->nverts;

        int q = -1;
        if (P->wheel && wheelplace) { q = corner & 3; corner++; }

        /* ---- vertices: emit each file vertex exactly once, in file order.
         *      both topologies index into this, so the buffer means the same
         *      thing whichever reading we are testing. ---- */
        size_t vb = VBLK + P->vblk;
        int nv = (int)P->V;
        if (vb + (size_t)nv * VERTEX_STRIDE > VEND) {
            nv = VEND > vb ? (int)((VEND - vb) / VERTEX_STRIDE) : 0;
            truncated++;
            fprintf(stderr, "[WARN] part '%s': V=%u runs past VEND, clamping to %d\n",
                    P->name, P->V, nv);
        }
        if ((size_t)c->nverts + (size_t)nv > c->vcap) {
            nv = (int)(c->vcap - (size_t)c->nverts);
            if (nv < 0) nv = 0;
            truncated++;
        }
        for (int k = 0; k < nv; k++) {
            uint8_t *dst = c->verts + (size_t)c->nverts * VSTRIDE;
            vtx_out(c->p, vb, (uint32_t)k, dst);
            /* SANITISE BEFORE THE TRANSFORM, not after. A non-finite position
             * component multiplied by a zero in the rotation matrix poisons
             * the OTHER two axes as well (NaN * 0 == NaN), so a vertex that
             * used to survive as NaN-on-one-axis collapses to the origin and
             * takes the triangle with it. Clamp first; then transform. */
            {
                float *w = (float *)dst;
                for (int a = 0; a < 3; a++)
                    if (w[a] != w[a] || fabsf(w[a]) > 1e5f) { w[a] = 0.0f; };
            }
            if (nx[i].have) {
                /* THE FILE'S OWN TRANSFORM (M2). p' = p.x*right + p.y*up +
                 * p.z*forward + position, the same convention the game's
                 * FixMatrix_Multiply uses. */
                float *v = (float *)dst;
                float x = v[0], y = v[1], z = v[2];
                float nx0 = x * nx[i].m[0] + y * nx[i].m[3] + z * nx[i].m[6] + nx[i].t[0];
                float ny0 = x * nx[i].m[1] + y * nx[i].m[4] + z * nx[i].m[7] + nx[i].t[1];
                float nz0 = x * nx[i].m[2] + y * nx[i].m[5] + z * nx[i].m[8] + nx[i].t[2];
                /* SELF-CHECK. Every body part in every car carries an identity
                 * node, so an identity node that moves a vertex means the
                 * matrix is being read wrong -- which is exactly how the first
                 * version of this failed, silently, at 1/7th scale. */
                if (nx[i].identity) {
                    float d = fabsf(nx0 - x) + fabsf(ny0 - y) + fabsf(nz0 - z);
                    if (d > idmove) idmove = d;
                    if (d > 1e-3f) nidbad++;
                }
                v[0] = nx0; v[1] = ny0; v[2] = nz0;
            } else if (q >= 0) {
                /* fallback only: no node referenced this wheel, so fall back to
                 * the old inference. WX/WY/WZ or NOWHEELPLACE=1 override it. */
                static const float SX[4] = { +1.0f, +1.0f, -1.0f, -1.0f };
                static const float SZ[4] = { +1.0f, -1.0f, +1.0f, -1.0f };
                float *v = (float *)dst;
                v[0] += wx * SX[q];
                v[1] += wy;
                v[2] += wz * SZ[q];
            }
            float *v = (float *)dst;
            for (int a = 0; a < 3; a++) {
                if (v[a] < lo[a]) lo[a] = v[a];
                if (v[a] > hi[a]) hi[a] = v[a];
            }
            c->nverts++;
        }

        if (!nv) { S.holes++; continue; }
        size_t fb = C20 + P->facoff;
        int fb_ok = (P->facoff % TRIANGLE_STRIDE == 0) &&
                    fb + (size_t)P->F * TRIANGLE_STRIDE <= FEND;
        if (!fb_ok) fb_bad++;

        if (g_mesh_mode == MESH_STRIP) {
            /* the STRIP control has no per-triangle texture (it discards the
             * face records by definition), so it gets one run per part with the
             * name-matched texture -- and the log says so. */
            if (c->nruns >= c->runcap) {
                int nc = c->runcap ? c->runcap * 2 : 256;
                DrawRun *nr = realloc(c->run, (size_t)nc * sizeof *nr);
                if (!nr) { fprintf(stderr, "[ERR] out of memory (runs)\n"); return 0; }
                c->run = nr; c->runcap = nc;
            }
            c->run[c->nruns].istart = (uint32_t)c->nidx;
            c->run[c->nruns].icount = 0;
            c->run[c->nruns].tex    = P->tex;
            c->run[c->nruns].alpha  = P->transparent;
            c->run[c->nruns].part   = i;
            c->nruns++;
            for (int k = 0; k + 2 < nv; k++) {
                if ((size_t)c->nidx + 3 > c->icap) break;
                uint32_t a = P->vstart + (uint32_t)k;
                uint32_t b = P->vstart + (uint32_t)k + 1;
                uint32_t d = P->vstart + (uint32_t)k + 2;
                if (k & 1) { uint32_t t = a; a = b; b = t; }   /* keep winding */
                c->idx[c->nidx++] = a; c->idx[c->nidx++] = b; c->idx[c->nidx++] = d;
            }
            c->run[c->nruns - 1].icount = (uint32_t)c->nidx - c->run[c->nruns - 1].istart;
            runsat[i] = c->nruns;
        } else {
            if (!P->F || !fb_ok) { fb_bad++; }
            else {
                for (uint32_t k = 0; k < P->F; k++) {
                    const uint8_t *rec = c->p + fb + (size_t)k * TRIANGLE_STRIDE;
                    uint16_t ix[3];
                    memcpy(ix, rec + TRI_INDEX_OFF, 6);
                    if (ix[0] >= (uint16_t)nv || ix[1] >= (uint16_t)nv ||
                        ix[2] >= (uint16_t)nv) continue;
                    if (ix[0] == ix[1] || ix[1] == ix[2] || ix[0] == ix[2]) continue;
                    if ((size_t)c->nidx + 3 > c->icap) break;
                    /* texture of this triangle: record+4, field_0x2c = 0.
                     * -1 means "no texture" and is not a slot we can bind.
                     * NAMETEX=1 is the OLD name-matched guess, kept so the two
                     * paths can be rendered and diffed (tools/preview.py --diff)
                     * instead of argued about. */
                    int t = nametex ? P->tex : (int)ru32(rec, TRI_TEXOFF);
                    if (t < 0 || t >= MAXTEX) t = -1;
                    /* a new run when the texture changes, exactly like
                     * Game_DrawMeshTextureRuns / Graphics_DrawMeshTextureBatches */
                    if (c->nruns == 0 || c->run[c->nruns - 1].tex != t ||
                        c->run[c->nruns - 1].alpha != P->transparent) {
                        if (c->nruns >= c->runcap) {
                            int nc = c->runcap ? c->runcap * 2 : 256;
                            DrawRun *nr = realloc(c->run, (size_t)nc * sizeof *nr);
                            if (!nr) { fprintf(stderr, "[ERR] out of memory (runs)\n"); return 0; }
                            c->run = nr; c->runcap = nc;
                        }
                        c->run[c->nruns].istart = (uint32_t)c->nidx;
                        c->run[c->nruns].icount = 0;
                        c->run[c->nruns].tex    = t;
                        c->run[c->nruns].alpha  = P->transparent;
                        c->run[c->nruns].part   = i;
                        c->nruns++;
                        texruns++;
                    }
                    c->run[c->nruns - 1].icount += 3;
                    if (t >= 0) texcount[t]++;
                    c->idx[c->nidx++] = P->vstart + ix[0];
                    c->idx[c->nidx++] = P->vstart + ix[1];
                    c->idx[c->nidx++] = P->vstart + ix[2];
                }
                if (c->nruns > 0) runsat[i] = c->nruns;   /* end of this part's runs */
            }
        }
        if (getenv("PARTBOUNDS")) {
            float l[3] = { 1e9f, 1e9f, 1e9f }, h[3] = { -1e9f, -1e9f, -1e9f };
            for (uint32_t q2 = P->vstart; q2 < (uint32_t)c->nverts; q2++) {
                const float *w = (const float *)(c->verts + (size_t)q2 * VSTRIDE);
                for (int a = 0; a < 3; a++) { if (w[a] < l[a]) l[a] = w[a]; if (w[a] > h[a]) h[a] = w[a]; }
            }
            printf("[PB] %2d %-12s x[%.3f,%.3f] y[%.3f,%.3f] z[%.3f,%.3f]\n",
                   i, P->name, l[0], h[0], l[1], h[1], l[2], h[2]);
        }
        P->built = (uint32_t)c->nidx - P->istart;
        if (!P->built) S.holes++;

        /* ---- bridging metric.  A wrong topology does not show up as missing
         * triangles, it shows up as LONG THIN ones that stitch unrelated
         * clusters together.  Normalise each triangle's longest edge by its
         * part's own bbox diagonal so parts of different sizes are comparable,
         * then flag anything beyond 5x the median ratio. ---- */
        float plo[3] = { 1e9f, 1e9f, 1e9f }, phi[3] = { -1e9f, -1e9f, -1e9f };
        for (uint32_t v = P->vstart; v < (uint32_t)c->nverts; v++) {
            const float *v3 = (const float *)(c->verts + (size_t)v * VSTRIDE);
            for (int a = 0; a < 3; a++) {
                if (v3[a] < plo[a]) plo[a] = v3[a];
                if (v3[a] > phi[a]) phi[a] = v3[a];
            }
        }
        float dx = phi[0]-plo[0], dy = phi[1]-plo[1], dz = phi[2]-plo[2];
        float pdiag = sqrtf(dx*dx + dy*dy + dz*dz);
        if (pdiag < 1e-4f) pdiag = 1.0f;
        for (uint32_t k = P->istart; k + 2 < (uint32_t)c->nidx; k += 3) {
            float e  = edge_len(c->verts, c->idx[k], c->idx[k+1]);
            float e2 = edge_len(c->verts, c->idx[k+1], c->idx[k+2]);
            float e3 = edge_len(c->verts, c->idx[k+2], c->idx[k]);
            if (e2 > e) e = e2;
            if (e3 > e) e = e3;
            if (e <= 1e-5f) S.degenerate++;
            if (e > S.max_edge) S.max_edge = e;
            if (nedge < (int)c->icap) edges[nedge++] = e / pdiag;
        }
    }
    memcpy(c->lo, lo, sizeof lo); memcpy(c->hi, hi, sizeof hi);
    c->ntri = c->nidx / 3;

    if (nedge > 0) {
        qsort(edges, (size_t)nedge, sizeof(float), cmp_f);
        float med = edges[nedge / 2];
        S.median_edge_ratio = med;
        int out = 0;
        for (int k = 0; k < nedge; k++) if (edges[k] > 5.0f * med) out++;
        S.bridge_pct = 100.0 * out / nedge;
    }
    free(edges);

    printf("[M2] node placement %s: identity nodes moved at most %.6f m over %d vertices (%d over 1mm)\n",
           nodeplace ? "ON" : "off", (double)idmove, c->nverts, nidbad);

    printf("[MESH] %-7s parts=%d verts=%d tris=%d  (vcap=%zu/%zu icap=%zu/%zu)\n",
           g_mesh_mode == MESH_STRIP ? "strip" : "indexed", c->nparts, c->nverts, c->ntri,
           (size_t)c->nverts, c->vcap, (size_t)c->nidx, c->icap);
    printf("[MESH] degenerate tris=%d  holes/empty parts=%d  bridging = %.2f%% "
           "(tris over 5x median edge)  median edge = %.4f of part diag\n",
           S.degenerate, S.holes, S.bridge_pct, S.median_edge_ratio);
    /* texruns counts only the indexed path's runs; the strip path's per-part
     * runs are not texture evidence, so they are not counted here. */
    texruns = texruns > 0 ? texruns : 0;
    {
        int used = 0;
        for (int t = 0; t < MAXTEX; t++) if (texcount[t]) used++;
        printf("[TEXRUNS] %d runs from the %s, %d distinct textures bound", c->nruns,
               nametex ? "PART NAMES (NAMETEX=1, the old guess)" : "file's own per-triangle texture IDs", used);
        if (g_mesh_mode == MESH_STRIP) printf("  (STRIP path: name-matched, one run per part)");
        printf("\n");
        printf("[TEXRUNS]");
        for (int t = 0; t < c->ntex; t++)
            if (texcount[t]) printf("  %d:%s x%d", t, c->texname[t], texcount[t]);
        printf("\n");
        (void)texruns; (void)texruns_before; (void)runsat;
    }
    if (truncated) printf("[MESH] WARN %d part(s) had to be clamped to the vertex budget\n", truncated);
    if (fb_bad)    printf("[MESH] WARN %d part(s) had an unusable face block\n", fb_bad);
    return c->nverts > 0;
}

/* ------------------------------------------------------------------ main --- */
static const char *GAME_ENV = "CMR2_GAME";

static int list_cars(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) { fprintf(stderr, "[ERR] cannot open %s\n", dir); return 1; }
    struct dirent *de;
    int n = 0;
    printf("cars in %s:\n", dir);
    while ((de = readdir(d))) {
        const char *s = de->d_name;
        size_t l = strlen(s);
        if (l > 4 && !strcasecmp(s + l - 4, ".c3d")) {
            printf("  %.*s\n", (int)(l - 4), s);
            n++;
        }
    }
    closedir(d);
    printf("%d geometry files\n", n);
    return 0;
}

int main(int argc, char **argv) {
    /* stdout is unbuffered from the very first line. setvbuf() must run before
     * any other operation on the stream (POSIX: otherwise the behaviour is
     * undefined), and the old call at the SDL init block ran after ~20 printf()s
     * -- glibc silently ignored it, so when the viewer was launched detached with
     * `> /tmp/cmr2win.log` the log stayed empty for the whole session. */
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *car = "205a1N", *game = getenv(GAME_ENV), *shot = NULL;
    int spin = getenv("SPIN") ? 1 : 0, list = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--game") && i + 1 < argc) game = argv[++i];
        else if (!strcmp(argv[i], "--list")) list = 1;
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            printf(
"cmr2deck [car] [--game DIR] [--shot FILE] [--list]\n"
"          [--mesh indexed|strip]\n"
"  Native CMR2 car viewer. Reads the game's own .c3d geometry and .bfl\n"
"  DDS(DXT5) / TGA textures and renders them on the Deck's GPU via SDL3.\n"
"  textures and renders them on the Deck's GPU via SDL3/Vulkan.\n"
"  controls: arrows orbit | A/D yaw | W wireframe | Q/S zoom | SPACE spin\n"
"            R reset | wheel zoom | ESC quit\n"
"  env:      CMR2_GAME, SPIN=1, MESH=indexed|strip\n"
"            NODEPLACE=0 no placed transforms | NOWHEELPLACE=1 no wheels\n"
"            WX/WY/WZ override the wheel offset        | TEXDUMP=DIR\n"
"            MESH=indexed|strip (same as --mesh)\n");
            return 0;
        }
        else if (!strcmp(argv[i], "--mesh") && i + 1 < argc) {
            const char *m = argv[++i];
            if      (!strcmp(m, "strip"))   g_mesh_mode = MESH_STRIP;
            else if (!strcmp(m, "indexed")) g_mesh_mode = MESH_INDEXED;
            else { fprintf(stderr, "[ERR] --mesh takes 'indexed' or 'strip'\n"); return 2; }
        }
        else if (argv[i][0] != '-') car = argv[i];
    }
    { const char *m = getenv("MESH");          /* env is the fallback, arg wins */
      if (m && !strcmp(m, "strip")) g_mesh_mode = MESH_STRIP; }

    char base_dir[1200];
    if (!game) {
        const char *base = SDL_GetBasePath();
        snprintf(base_dir, sizeof base_dir, "%s..", base ? base : ".");
        game = base_dir;
    }
    char dir[1400], c3dpath[1500], bflpath[1500];
    snprintf(dir, sizeof dir, "%s/game/Game/Cars", game);
    {
        char probe[1600];
        snprintf(probe, sizeof probe, "%s/205a1N.c3d", dir);
        FILE *f = fopen(probe, "rb");
        if (!f) snprintf(dir, sizeof dir, "%s/Game/Cars", game);   /* --game points at the root */
        else fclose(f);
    }

    /* --list is acted on AFTER every argument has been parsed, so
     * "cmr2deck --list --game DIR" and "cmr2deck --game DIR --list" agree.
     * It used to run inside the parse loop and therefore only saw a --game
     * that happened to come earlier. */
    if (list) return list_cars(dir);

    snprintf(c3dpath, sizeof c3dpath, "%s/%s.c3d", dir, car);
    { FILE *f = fopen(c3dpath, "rb");
      if (!f) { snprintf(c3dpath, sizeof c3dpath, "%s/%sN.c3d", dir, car); f = fopen(c3dpath, "rb"); }
      if (!f) { fprintf(stderr, "[ERR] no .c3d for '%s' in %s\n", car, dir); return 1; }
      fclose(f); }
    /* Geometry and texture names do NOT always agree: 205a1N.c3d carries its
     * textures in 205a1.bfl (no N).  Try the literal name first, then the name
     * with the trailing letter shaved off, then the name with N appended. */
    {
        char cand[4][1500]; int nc = 0;
        snprintf(cand[nc++], 1500, "%s/%s.bfl", dir, car);
        size_t cl = strlen(car);
        if (cl > 1) snprintf(cand[nc++], 1500, "%s/%.*s.bfl", dir, (int)(cl - 1), car);
        if (cl < 40) snprintf(cand[nc++], 1500, "%s/%sN.bfl", dir, car);
        bflpath[0] = 0;
        for (int k = 0; k < nc; k++) {
            FILE *f = fopen(cand[k], "rb");
            if (f) { fclose(f); snprintf(bflpath, sizeof bflpath, "%s", cand[k]); break; }
        }
        if (!bflpath[0]) snprintf(bflpath, sizeof bflpath, "%s/%s.bfl", dir, car);
    }

    int wheelplace = getenv("NOWHEELPLACE") ? 0 : 1;
    /* M2: place parts from the .c3d's own scene nodes. NODEPLACE=0 reverts to
     * the mirrored-corner inference for an A/B against the same render. */
    int nodeplace  = (!getenv("NOWHEELPLACE") && !getenv("NODEPLACE")) ? 1 : 0;
    float wx = 1.20f, wy = -0.23f, wz = 0.75f;
    const char *e;
    if ((e = getenv("WX"))) wx = (float)atof(e);
    if ((e = getenv("WY"))) wy = (float)atof(e);
    if ((e = getenv("WZ"))) wz = (float)atof(e);
    /* fixed camera, so two runs can be compared pixel for pixel */
    float yaw0 = 38.0f, elev0 = 14.0f, distk = 1.12f;
    if ((e = getenv("YAW")))  yaw0  = (float)atof(e);
    if ((e = getenv("ELEV"))) elev0 = (float)atof(e);
    if ((e = getenv("DIST"))) distk = (float)atof(e);

    printf("=== CMR2 native viewer -- Steam Deck, x86_64, SDL3/Vulkan ===\n");
    printf("[INFO] car  %s\n", car);
    printf("[INFO] c3d  %s\n", c3dpath);

    C3d c;
    if (!c3d_load(&c, c3dpath, wheelplace, nodeplace, wx, wy, wz)) return 1;
    /* was: 100.0 * nidx / 3.0 -- the denominator was a constant, so the
     * "percentage of face records kept" printed 88800.0%. It is triangles
     * loaded / face records in the file, and it is the fastest way to see the
     * strip reading overrun by 32%: >100% means more triangles than the file has. */
    printf("[OK] geometry: %d parts, %d verts, %d indices (%d of %ld face records kept = %.1f%%)\n",
           c.nparts, c.nverts, c.nidx, c.ntri, c.cfile_faces,
           c.cfile_faces ? 100.0 * (double)c.ntri / (double)c.cfile_faces : 0.0);
    printf("[INFO] bbox x[%.3f,%.3f] y[%.3f,%.3f] z[%.3f,%.3f] = %.2f x %.2f x %.2f m\n",
           c.lo[0], c.hi[0], c.lo[1], c.hi[1], c.lo[2], c.hi[2],
           c.hi[0] - c.lo[0], c.hi[1] - c.lo[1], c.hi[2] - c.lo[2]);
    for (int i = 0; i < c.nparts; i++)
        printf("[PART] %-12s verts=%-5u tris=%-5u tex=%-2d %-14s%s\n",
               c.part[i].name, c.part[i].built, c.part[i].built / 3,
               c.part[i].tex, c.texname[c.part[i].tex],
               c.part[i].transparent ? "  [alpha]" : c.part[i].wheel ? "  [wheel]" : "");

    /* ---- textures ---- */
    size_t blen = 0;
    uint8_t *bfl = load_res(bflpath, &blen);
    Bfl B; int have_bfl = 0;
    if (bfl) have_bfl = bfl_parse(bfl, blen, &B);
    if (have_bfl) printf("[OK] bfl: %zu bytes, %d textures\n", blen, B.n);
    else printf("[WARN] no usable .bfl (%s) -- drawing untextured\n", bflpath);

    int NT = c.ntex > 0 ? c.ntex : 1;
    SDL_GPUTexture *tex[MAXTEX];
    int tw[MAXTEX], th[MAXTEX], got[MAXTEX];
    memset(tex, 0, sizeof tex); memset(got, 0, sizeof got);


    /* ---- presentation config. EVERY value below is printed before the first
     * frame, because none of it is game data and a frame that hides its own
     * shading model is a frame that can be mistaken for a render of the game.
     * The alpha states and the blend pair ARE the game's; the ambient value and
     * the light direction are not -- CMR2's object light is a stage light. */
    /* MSAA count -> SDL_GPUSampleCount. The enum is NOT the number of samples
     * (SDL_GPU_SAMPLECOUNT_1 == 0), and casting 4 to it asks for 8x, which the
     * backend rejects -- that cost a core dump to find. */
    int   msaa   = getenv("MSAA") ? atoi(getenv("MSAA")) : 4;
    SDL_GPUSampleCount scount = SDL_GPU_SAMPLECOUNT_1;
    if      (msaa == 2) scount = SDL_GPU_SAMPLECOUNT_2;
    else if (msaa == 4) scount = SDL_GPU_SAMPLECOUNT_4;
    else if (msaa == 8) scount = SDL_GPU_SAMPLECOUNT_8;
    else msaa = 1;
    const char *cullenv = getenv("CULL");
    SDL_GPUCullMode cull = SDL_GPU_CULLMODE_BACK;
    if (cullenv) {
        if      (!strcmp(cullenv, "none"))  cull = SDL_GPU_CULLMODE_NONE;
        else if (!strcmp(cullenv, "back"))  cull = SDL_GPU_CULLMODE_BACK;
        else if (!strcmp(cullenv, "front")) cull = SDL_GPU_CULLMODE_FRONT;
        else { fprintf(stderr, "[ERR] CULL takes none|back|front\n"); return 2; }
    }
    float ambient = getenv("AMB")  ? (float)atof(getenv("AMB"))  : 0.40f;
    float gain    = getenv("GAIN") ? (float)atof(getenv("GAIN")) : 0.55f;
    float fillg   = getenv("FILL") ? (float)atof(getenv("FILL")) : 0.36f;
    float ldir[3] = { -0.42f, 0.72f, 0.55f };
    float ldir2[3] = { 0.62f, 0.28f, -0.60f };
    if (getenv("LDIR"))  sscanf(getenv("LDIR"),  "%f,%f,%f", &ldir[0],  &ldir[1],  &ldir[2]);
    if (getenv("LDIR2")) sscanf(getenv("LDIR2"), "%f,%f,%f", &ldir2[0], &ldir2[1], &ldir2[2]);
    float *nrm2[2] = { ldir, ldir2 };
    for (int q = 0; q < 2; q++) {
        float l = sqrtf(nrm2[q][0]*nrm2[q][0] + nrm2[q][1]*nrm2[q][1] + nrm2[q][2]*nrm2[q][2]);
        if (l < 1e-6f) { nrm2[q][0] = 0; nrm2[q][1] = 1; nrm2[q][2] = 0; l = 1; }
        nrm2[q][0] /= l; nrm2[q][1] /= l; nrm2[q][2] /= l;
    }
    int   shadow  = getenv("SHADOW") ? (atoi(getenv("SHADOW")) != 0) : 1;
    int   vshade  = getenv("VSHADE") ? 1 : 0;   /* vertex colour as diffuse term */
    int   use_bg  = getenv("BG") ? (atoi(getenv("BG")) != 0) : 1;
    float alpharef_alpha = 1.0f / 255.0f;       /* ALPHAREF 1   + D3DCMP_GREATER */
    float alpharef_solid = 128.0f / 255.0f;     /* ALPHAREF 128 + D3DCMP_GREATER */
    /* ---- SDL / GPU ---- */
    int headless = (shot != NULL);
    if (headless) SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    /* (stdout buffering is set at the top of main -- setvbuf is UB this late) */
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) { fprintf(stderr, "[ERR] SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_GPUDevice *dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, NULL);
    if (!dev) { fprintf(stderr, "[ERR] CreateGPUDevice: %s\n", SDL_GetError()); return 1; }
    printf("[OK] GPU device: %s\n", SDL_GetGPUDeviceDriver(dev));

    int W = 1280, H = 720;
    /* the Deck's own panel is 1280x800; OFFW/OFFH so a shot can be taken at it */
    if (headless) {
        const char *ow = getenv("OFFW"), *oh = getenv("OFFH");
        if (ow) W = atoi(ow);
        if (oh) H = atoi(oh);
        if (W < 64) W = 64; if (H < 64) H = 64;
        if (W > 4096) W = 4096; if (H > 4096) H = 4096;
    }
    SDL_Window *win = NULL;
    if (!headless) {
        Uint32 wflags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE;
        if (getenv("FULLSCREEN")) wflags |= SDL_WINDOW_FULLSCREEN;
        win = SDL_CreateWindow("CMR2 native -- Steam Deck", W, H, wflags);
        if (win && (wflags & SDL_WINDOW_FULLSCREEN)) {
            SDL_DisplayID did = SDL_GetDisplayForWindow(win);
            const SDL_DisplayMode *dm = did ? SDL_GetCurrentDisplayMode(did) : NULL;
            if (dm && SDL_SetWindowFullscreenMode(win, dm))
                SDL_SetWindowSize(win, dm->w, dm->h);
        }
        if (!win || !SDL_ClaimWindowForGPUDevice(dev, win)) {
            fprintf(stderr, "[WARN] window unavailable (%s) -- falling back to offscreen\n",
                    SDL_GetError());
            if (win) { SDL_DestroyWindow(win); win = NULL; }
            headless = 1;
        } else {
            printf("[OK] window + swapchain, video driver = %s\n", SDL_GetCurrentVideoDriver());
        }
    }
    /* ---- render targets. MSAA is mine: the game rendered into a plain 32-bit
     * surface, so this is presentation, not fidelity -- it is printed as such.
     * The colour format is the SWAPCHAIN's when there is a window: SDL refuses a
     * resolve whose format does not match its colour target, and a Deck
     * swapchain is not R8G8B8A8_UNORM. Offscreen keeps UNORM, which is the
     * format the game's own 32-bit surface corresponds to. */
    SDL_GPUTextureFormat ctf = win ? SDL_GetGPUSwapchainTextureFormat(dev, win)
                                   : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    int samples = msaa;
    SDL_GPUTexture *color = NULL, *color_ms = NULL;
    if (!win) {
        SDL_GPUTextureCreateInfo ci = {0};
        ci.type = SDL_GPU_TEXTURETYPE_2D;
        ci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        ci.width = W; ci.height = H; ci.layer_count_or_depth = 1; ci.num_levels = 1;
        ci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        color = SDL_CreateGPUTexture(dev, &ci);
        printf("[INFO] offscreen %dx%d (no window)\n", W, H);
    }
    SDL_GPUTexture *depth = SDL_CreateGPUTexture(dev, &(SDL_GPUTextureCreateInfo){
        .type = SDL_GPU_TEXTURETYPE_2D, .format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
        .width = W, .height = H, .layer_count_or_depth = 1, .num_levels = 1,
        .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET, .sample_count = scount });
    if (samples > 1) {
        SDL_GPUTextureCreateInfo ci = {0};
        ci.type = SDL_GPU_TEXTURETYPE_2D;
        ci.format = ctf;
        ci.width = W; ci.height = H; ci.layer_count_or_depth = 1; ci.num_levels = 1;
        ci.sample_count = scount;
        ci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        color_ms = SDL_CreateGPUTexture(dev, &ci);
    }
    printf("[CFG] colour target format %d, samples=%d cull=%s ambient=%.2f key=%.2f fill=%.2f "
           "light=(%.2f,%.2f,%.2f) fill_dir=(%.2f,%.2f,%.2f) vshade=%d backdrop=%d "
           "castshadow=%d\n   (the ambient, both lights, the shadow, the backdrop and the "
           "framing are MINE. The alpha test, the alpha ref 0x80/1, the "
           "SRCALPHA/INVSRCALPHA blend pair and every texture/vertex/triangle are the "
           "game's)\n",
           (int)ctf, samples, cullenv ? cullenv : "back", ambient, gain, fillg,
           ldir[0], ldir[1], ldir[2], ldir2[0], ldir2[1], ldir2[2],
           vshade, use_bg, shadow);

    SDL_GPUSampler *samp = SDL_CreateGPUSampler(dev, &(SDL_GPUSamplerCreateInfo){
        .min_filter = SDL_GPU_FILTER_LINEAR, .mag_filter = SDL_GPU_FILTER_LINEAR,
        .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT });

    SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer(dev);
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cb);
    int nloaded = 0, nmissing = 0;
    for (int i = 0; i < NT; i++) {
        int w0 = 0, h0 = 0;
        uint8_t *rgba = NULL;
        size_t dl = 0;
        const uint8_t *blob = have_bfl ? bfl_find(&B, bfl, c.texname[i], &dl) : NULL;
        if (blob) rgba = tex_decode(blob, dl, &w0, &h0);
        if (rgba) nloaded++;
        else {
            nmissing++;
            w0 = h0 = 2; rgba = malloc(16);
            for (int k = 0; k < 16; k++) rgba[k] = 0xff;
            if (!blob) printf("[WARN] tex[%2d] %-14s not in .bfl\n", i, c.texname[i]);
            else       printf("[WARN] tex[%2d] %-14s undecodable -> flat white\n", i, c.texname[i]);
        }
        tw[i] = w0; th[i] = h0; got[i] = rgba ? 1 : 0;
        /* TEXDUMP=DIR writes every decoded texture out as PPM (P6) so a decode
         * can be diffed against an independent reader instead of eyeballed. */
        { const char *td = getenv("TEXDUMP");
          if (td) {
            char fp[1600];
            snprintf(fp, sizeof fp, "%s/%02d_%s.ppm", td, i, c.texname[i]);
            FILE *o = fopen(fp, "wb");
            if (o) {
                fprintf(o, "P6\n%d %d\n255\n", w0, h0);
                for (int q = 0; q < w0 * h0; q++) fwrite(rgba + q * 4, 1, 3, o);
                fclose(o);
            }
          } }
        SDL_GPUTextureCreateInfo tci = {0};
        tci.type = SDL_GPU_TEXTURETYPE_2D;
        tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        tci.width = (Uint32)tw[i]; tci.height = (Uint32)th[i];
        tci.layer_count_or_depth = 1; tci.num_levels = 1;
        tci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        tex[i] = SDL_CreateGPUTexture(dev, &tci);
        Uint32 nb = (Uint32)(tw[i] * th[i] * 4);
        SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(dev,
            &(SDL_GPUTransferBufferCreateInfo){ .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = nb });
        void *m = SDL_MapGPUTransferBuffer(dev, tb, false);
        memcpy(m, rgba, nb);
        SDL_UnmapGPUTransferBuffer(dev, tb);
        SDL_UploadToGPUTexture(cp, &(SDL_GPUTextureTransferInfo){ .transfer_buffer = tb },
            &(SDL_GPUTextureRegion){ .texture = tex[i], .w = (Uint32)tw[i], .h = (Uint32)th[i], .d = 1 }, false);
        SDL_ReleaseGPUTransferBuffer(dev, tb);
        free(rgba);
    }
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(cb);
    printf("[OK] textures: %d decoded from the .bfl, %d placeholders\n", nloaded, nmissing);

    /* ---- shaders ---- */
    const char *sbase = SDL_GetBasePath();
    char sp[1500];
    void *sblob[4] = {0}; size_t sblen[4] = {0};
    const char *sfn[4] = { "car.vert.spv", "car.frag.spv", "bg.vert.spv", "bg.frag.spv" };
    for (int i = 0; i < 4; i++) {
        snprintf(sp, sizeof sp, "%s%s", sbase ? sbase : "./", sfn[i]);
        sblob[i] = SDL_LoadFile(sp, &sblen[i]);
        if (!sblob[i]) {
            /* the backdrop is optional: without it the clear colour is used */
            if (i < 2) { fprintf(stderr, "[ERR] shader %s not next to the binary\n", sfn[i]); return 1; }
            if (use_bg) printf("[WARN] %s missing -- backdrop off\n", sfn[i]);
            use_bg = 0;
        }
    }
    SDL_GPUShader *vsh = SDL_CreateGPUShader(dev, &(SDL_GPUShaderCreateInfo){
        .code_size = sblen[0], .code = sblob[0], .entrypoint = "main",
        .format = SDL_GPU_SHADERFORMAT_SPIRV, .stage = SDL_GPU_SHADERSTAGE_VERTEX,
        .num_uniform_buffers = 1 });
    SDL_GPUShader *fsh = SDL_CreateGPUShader(dev, &(SDL_GPUShaderCreateInfo){
        .code_size = sblen[1], .code = sblob[1], .entrypoint = "main",
        .format = SDL_GPU_SHADERFORMAT_SPIRV, .stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .num_samplers = 1, .num_uniform_buffers = 1 });
    if (!vsh || !fsh) { fprintf(stderr, "[ERR] shader: %s\n", SDL_GetError()); return 1; }

    SDL_GPUVertexBufferDescription vbd = { .slot = 0, .pitch = VSTRIDE,
                                           .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX };
    SDL_GPUVertexAttribute attrs[4] = {
        { .location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,      .offset = 0  },
        { .location = 1, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,      .offset = 12 },
        { .location = 2, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, .offset = 24 },
        { .location = 3, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,      .offset = 28 },
    };
    SDL_GPUGraphicsPipeline *pipe[2][2], *bgpipe = NULL;
    for (int tp = 0; tp < 2; tp++) {
        for (int wf = 0; wf < 2; wf++) {
            SDL_GPUColorTargetBlendState blend = {0};
            if (tp) {
                /* the game's pair: D3DBLEND_SRCALPHA / D3DBLEND_INVSRCALPHA */
                blend.enable_blend = true;
                blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
                blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
                blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
                blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
                blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
                blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            }
            SDL_GPUColorTargetDescription ctd = {
                .format = ctf,
                .blend_state = blend,
            };
            SDL_GPUGraphicsPipelineCreateInfo pci = {0};
            pci.vertex_shader = vsh;
            pci.fragment_shader = fsh;
            pci.vertex_input_state.vertex_buffer_descriptions = &vbd;
            pci.vertex_input_state.num_vertex_buffers = 1;
            pci.vertex_input_state.vertex_attributes = attrs;
            pci.vertex_input_state.num_vertex_attributes = 4;
            pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            pci.rasterizer_state.fill_mode = wf ? SDL_GPU_FILLMODE_LINE : SDL_GPU_FILLMODE_FILL;
            pci.rasterizer_state.cull_mode = cull;
            pci.multisample_state.sample_count = scount;
            pci.depth_stencil_state.enable_depth_test = true;
            pci.depth_stencil_state.enable_depth_write = !tp;
            pci.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
            pci.target_info.color_target_descriptions = &ctd;
            pci.target_info.num_color_targets = 1;
            pci.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
            pci.target_info.has_depth_stencil_target = true;
            pipe[tp][wf] = SDL_CreateGPUGraphicsPipeline(dev, &pci);
            if (!pipe[tp][wf]) { fprintf(stderr, "[ERR] pipeline: %s\n", SDL_GetError()); return 1; }
        }
    }
    /* the ground shadow: the same vertices, darkened into the backdrop
     * (dst * (1 - src.a)), no depth, no cull, no wireframe variant */
    SDL_GPUGraphicsPipeline *shpipe = NULL;
    if (shadow) {
        SDL_GPUColorTargetBlendState sb = {0};
        sb.enable_blend = true;
        sb.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
        sb.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        sb.color_blend_op = SDL_GPU_BLENDOP_ADD;
        sb.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
        sb.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        sb.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        SDL_GPUColorTargetDescription ctd = { .format = ctf, .blend_state = sb };
        /* the shadow needs its own fragment stage: the car's fragment shader
         * takes a sampler, and a pipeline with a sampler must have one bound at
         * every draw or SDL aborts ("Missing fragment sampler binding!"). */
        snprintf(sp, sizeof sp, "%s%s", sbase ? sbase : "./", "shadow.frag.spv");
        size_t shsz = 0;
        void *shblob = SDL_LoadFile(sp, &shsz);
        if (!shblob) { fprintf(stderr, "[ERR] shadow.frag.spv not next to the binary\n"); return 1; }
        SDL_GPUShader *sfsh = SDL_CreateGPUShader(dev, &(SDL_GPUShaderCreateInfo){
            .code_size = shsz, .code = shblob, .entrypoint = "main",
            .format = SDL_GPU_SHADERFORMAT_SPIRV, .stage = SDL_GPU_SHADERSTAGE_FRAGMENT });
        if (!sfsh) { fprintf(stderr, "[ERR] shadow fragment shader: %s\n", SDL_GetError()); return 1; }
        SDL_GPUGraphicsPipelineCreateInfo pci = {0};
        pci.vertex_shader = vsh;
        pci.fragment_shader = sfsh;
        pci.vertex_input_state.vertex_buffer_descriptions = &vbd;
        pci.vertex_input_state.num_vertex_buffers = 1;
        pci.vertex_input_state.vertex_attributes = attrs;
        pci.vertex_input_state.num_vertex_attributes = 4;
        pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pci.multisample_state.sample_count = scount;
        pci.depth_stencil_state.enable_depth_test = false;
        pci.depth_stencil_state.enable_depth_write = false;
        pci.target_info.color_target_descriptions = &ctd;
        pci.target_info.num_color_targets = 1;
        pci.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
        pci.target_info.has_depth_stencil_target = true;
        shpipe = SDL_CreateGPUGraphicsPipeline(dev, &pci);
        if (!shpipe) { fprintf(stderr, "[ERR] shadow pipeline: %s\n", SDL_GetError()); return 1; }
    }
    if (use_bg) {
        SDL_GPUShader *bv = SDL_CreateGPUShader(dev, &(SDL_GPUShaderCreateInfo){
            .code_size = sblen[2], .code = sblob[2], .entrypoint = "main",
            .format = SDL_GPU_SHADERFORMAT_SPIRV, .stage = SDL_GPU_SHADERSTAGE_VERTEX });
        SDL_GPUShader *bf = SDL_CreateGPUShader(dev, &(SDL_GPUShaderCreateInfo){
            .code_size = sblen[3], .code = sblob[3], .entrypoint = "main",
            .format = SDL_GPU_SHADERFORMAT_SPIRV, .stage = SDL_GPU_SHADERSTAGE_FRAGMENT });
        if (!bv || !bf) { fprintf(stderr, "[ERR] backdrop shader: %s\n", SDL_GetError()); return 1; }
        SDL_GPUColorTargetDescription ctd = { .format = ctf, .blend_state = {0} };
        SDL_GPUGraphicsPipelineCreateInfo pci = {0};
        pci.vertex_shader = bv;
        pci.fragment_shader = bf;
        pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pci.multisample_state.sample_count = scount;
        pci.depth_stencil_state.enable_depth_test = false;
        pci.depth_stencil_state.enable_depth_write = false;
        pci.target_info.color_target_descriptions = &ctd;
        pci.target_info.num_color_targets = 1;
        pci.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
        pci.target_info.has_depth_stencil_target = true;
        bgpipe = SDL_CreateGPUGraphicsPipeline(dev, &pci);
        if (!bgpipe) { fprintf(stderr, "[ERR] backdrop pipeline: %s\n", SDL_GetError()); return 1; }
    }


    Uint32 vbsz = (Uint32)c.nverts * VSTRIDE, ibsz = (Uint32)c.nidx * 4;
    SDL_GPUBuffer *vb = SDL_CreateGPUBuffer(dev, &(SDL_GPUBufferCreateInfo){
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX, .size = vbsz });
    SDL_GPUBuffer *ib = SDL_CreateGPUBuffer(dev, &(SDL_GPUBufferCreateInfo){
        .usage = SDL_GPU_BUFFERUSAGE_INDEX, .size = ibsz });
    SDL_GPUTransferBuffer *bt = SDL_CreateGPUTransferBuffer(dev, &(SDL_GPUTransferBufferCreateInfo){
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = vbsz + ibsz });
    void *mp = SDL_MapGPUTransferBuffer(dev, bt, false);
    memcpy(mp, c.verts, vbsz);
    memcpy((uint8_t *)mp + vbsz, c.idx, ibsz);
    SDL_UnmapGPUTransferBuffer(dev, bt);
    cb = SDL_AcquireGPUCommandBuffer(dev);
    cp = SDL_BeginGPUCopyPass(cb);
    SDL_UploadToGPUBuffer(cp, &(SDL_GPUTransferBufferLocation){ .transfer_buffer = bt, .offset = 0 },
        &(SDL_GPUBufferRegion){ .buffer = vb, .offset = 0, .size = vbsz }, false);
    SDL_UploadToGPUBuffer(cp, &(SDL_GPUTransferBufferLocation){ .transfer_buffer = bt, .offset = vbsz },
        &(SDL_GPUBufferRegion){ .buffer = ib, .offset = 0, .size = ibsz }, false);
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(cb);
    printf("[OK] mesh on the GPU: %u B verts + %u B indices\n", vbsz, ibsz);

    /* ---- camera ---- */
    float cx = (c.lo[0] + c.hi[0]) * 0.5f, cy = (c.lo[1] + c.hi[1]) * 0.5f, cz = (c.lo[2] + c.hi[2]) * 0.5f;
    float maxdim = 0;
    for (int a = 0; a < 3; a++) { float d = c.hi[a] - c.lo[a]; if (d > maxdim) maxdim = d; }
    if (maxdim <= 0) maxdim = 4;
    float yaw = yaw0, elev = elev0, dist = maxdim * distk;
    int wire = 0, running = 1, frames = 0;
    int exitframes = getenv("EXITFRAMES") ? atoi(getenv("EXITFRAMES")) : 0;
    Uint64 t0 = SDL_GetTicks(), tlast = t0;

    /* ---- Steam Deck gamepad ------------------------------------------------
     * The Deck presents its controls as an X-Box 360 pad (js0 / event4,9).
     * In Game Mode there is no keyboard, so every action here also has a
     * button: B quits, A spin, X wire, Y reset, Start quits.
     */
    SDL_Gamepad *pad = NULL;
    {
        int ng = 0;
        SDL_JoystickID *ids = SDL_GetGamepads(&ng);
        for (int gi = 0; gi < ng && !pad; gi++)
            if (ids[gi] != 0) pad = SDL_OpenGamepad(ids[gi]);
        if (ids) SDL_free(ids);
        if (pad) printf("[OK] gamepad: %s\n", SDL_GetGamepadName(pad));
        else     printf("[INFO] no gamepad -- keyboard only\n");
    }

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) running = 0;
            else if (ev.type == SDL_EVENT_KEY_DOWN) {
                switch (ev.key.key) {
                    case SDLK_ESCAPE: running = 0; break;
                    case SDLK_SPACE:  spin = !spin; break;
                    case SDLK_W:      wire = !wire; break;
                    case SDLK_R:      yaw = yaw0; elev = elev0; dist = maxdim * distk; break;
                    case SDLK_LEFT:   yaw += 6.0f; break;
                    case SDLK_RIGHT:  yaw -= 6.0f; break;
                    case SDLK_A:      yaw -= 6.0f; break;
                    case SDLK_D:      yaw += 6.0f; break;
                    case SDLK_UP:     elev = fminf(elev + 4.0f, 85.0f); break;
                    case SDLK_DOWN:   elev = fmaxf(elev - 4.0f, -20.0f); break;
                    case SDLK_S:      dist *= 1.12f; break;
                    case SDLK_Q:      dist *= 0.89f; break;
                    default: break;
                }
            } else if (ev.type == SDL_EVENT_MOUSE_WHEEL) {
                dist *= (ev.wheel.y > 0 ? 0.9f : 1.1f);
                dist = fmaxf(fminf(dist, maxdim * 8.0f), maxdim * 0.4f);
            } else if (ev.type == SDL_EVENT_GAMEPAD_ADDED) {
                if (!pad) { pad = SDL_OpenGamepad(ev.gdevice.which);
                            if (pad) printf("[OK] gamepad connected: %s\n", SDL_GetGamepadName(pad)); }
            } else if (ev.type == SDL_EVENT_GAMEPAD_REMOVED) {
                if (pad && ev.gdevice.which == SDL_GetJoystickID(SDL_GetGamepadJoystick(pad))) {
                    SDL_CloseGamepad(pad); pad = NULL;
                    printf("[INFO] gamepad disconnected\n");
                }
            } else if (ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && pad &&
                       ev.gbutton.which == SDL_GetJoystickID(SDL_GetGamepadJoystick(pad))) {
                switch (ev.gbutton.button) {
                    case SDL_GAMEPAD_BUTTON_SOUTH: spin = !spin; break;
                    case SDL_GAMEPAD_BUTTON_WEST:  wire = !wire; break;
                    case SDL_GAMEPAD_BUTTON_NORTH: yaw = yaw0; elev = elev0; dist = maxdim * distk; break;
                    case SDL_GAMEPAD_BUTTON_EAST:
                    case SDL_GAMEPAD_BUTTON_START: running = 0; break;
                    default: break;
                }
            }
        }
        if (pad) {
            /* radial deadzone, then cubic response so small nudges are fine */
            float lx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX)  / 32767.0f;
            float ly = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY)  / 32767.0f;
            float rx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX) / 32767.0f;
            float lt = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER)  / 32767.0f;
            float rt = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) / 32767.0f;
            if (fabsf(lx) > 0.16f) yaw -= lx * fabsf(lx) * 6.0f;
            if (fabsf(ly) > 0.16f) elev = fmaxf(fminf(elev + ly * fabsf(ly) * 5.0f, 85.0f), -25.0f);
            if (fabsf(rx) > 0.16f) yaw -= rx * fabsf(rx) * 2.0f;      /* right stick: fine yaw */
            float zt = (rt - lt) * 0.07f;                            /* R2 out, L2 in */
            if (zt != 0.0f) dist = fmaxf(fminf(dist * (1.0f + zt), maxdim * 8.0f), maxdim * 0.4f);
        }
        if (spin) yaw += 0.5f;

        SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(dev);
        if (!cmd) break;
        SDL_GPUTexture *sw = NULL;
        if (win) {
            if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, win, &sw, NULL, NULL)) {
                fprintf(stderr, "[ERR] swapchain: %s\n", SDL_GetError());
                SDL_SubmitGPUCommandBuffer(cmd);
                break;
            }
            if (!sw) { SDL_SubmitGPUCommandBuffer(cmd); continue; }
            /* a resize invalidates the MSAA and depth textures */
            Uint32 nw = 0, nh = 0;
            SDL_GetWindowSizeInPixels(win, &nw, &nh);
            if ((int)nw != W || (int)nh != H) {
                W = (int)nw; H = (int)nh;
                SDL_ReleaseGPUTexture(dev, depth);
                depth = SDL_CreateGPUTexture(dev, &(SDL_GPUTextureCreateInfo){
                    .type = SDL_GPU_TEXTURETYPE_2D, .format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
                    .width = W, .height = H, .layer_count_or_depth = 1, .num_levels = 1,
                    .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
                    .sample_count = scount });
                if (color_ms) {
                    SDL_ReleaseGPUTexture(dev, color_ms);
                    color_ms = SDL_CreateGPUTexture(dev, &(SDL_GPUTextureCreateInfo){
                        .type = SDL_GPU_TEXTURETYPE_2D, .format = ctf,
                        .width = W, .height = H, .layer_count_or_depth = 1, .num_levels = 1,
                        .sample_count = scount,
                        .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET });
                }
            }
        }

        float rad = yaw * 3.14159265f / 180.0f, er = elev * 3.14159265f / 180.0f;
        float ex = cx + dist * cosf(er) * sinf(rad);
        float ey = cy + dist * sinf(er);
        float ez = cz + dist * cosf(er) * cosf(rad);
        float fx = cx - ex, fy = cy - ey, fz = cz - ez;
        float fl = sqrtf(fx * fx + fy * fy + fz * fz);
        if (fl < 1e-6f) fl = 1; fx /= fl; fy /= fl; fz /= fl;
        float rx = fy * 0 - fz * 1, ry = fz * 0 - fx * 0, rz = fx * 1 - fy * 0;   /* cross(f, up) */
        float rl = sqrtf(rx * rx + ry * ry + rz * rz);
        if (rl < 1e-6f) { rx = 1; ry = 0; rz = 0; rl = 1; }
        rx /= rl; ry /= rl; rz /= rl;
        float ux = ry * fz - rz * fy, uy = rz * fx - rx * fz, uz = rx * fy - ry * fx;
        float view[16] = {
            rx, ry, rz, 0,
            ux, uy, uz, 0,
            -fx, -fy, -fz, 0,
            -(rx * ex + ry * ey + rz * ez),
            -(ux * ex + uy * ey + uz * ez),
             (fx * ex + fy * ey + fz * ez), 1 };
        float nz = 0.05f, fzz = maxdim * 40.0f, fovy = 42.0f * 3.14159265f / 180.0f;
        float tt = 1.0f / tanf(fovy * 0.5f), asp = (float)W / (float)H;
        float proj[16] = { tt / asp, 0, 0, 0,
                           0, tt, 0, 0,
                           0, 0, (fzz + nz) / (nz - fzz), -1,
                           0, 0, (2 * fzz * nz) / (nz - fzz), 0 };
        float mvp[16];
        for (int cc = 0; cc < 4; cc++)
            for (int rr = 0; rr < 4; rr++) {
                float s = 0;
                for (int k = 0; k < 4; k++) s += proj[k * 4 + rr] * view[cc * 4 + k];
                mvp[cc * 4 + rr] = s;
            }
        /* ---- the ground shadow matrix: the same geometry, flattened onto
         * y = 0 along the key light. Affine, so it composes into the MVP and
         * the car's own vertex shader can draw it. ---- */
        /* column-major, so S[col*4 + row] with column vector (x,y,z,1):
         *   x' = x - Lx*y      y' = ybase      z' = z - Lz*y      w' = 1
         * which is the exact point where the ray from a vertex along the key
         * light direction meets the plane y = ybase. */
        float Lx = 0.0f, Lz = 0.0f, ybase = c.lo[1] - 0.012f;
        float S[16] = { 0 };
        if (shadow) {
            float ly = ldir[1] > 0.15f ? ldir[1] : 0.15f;
            Lx = ldir[0] / ly; Lz = ldir[2] / ly;
        }
        S[0] = 1.0f; S[4] = -Lx; S[10] = 1.0f; S[6] = -Lz; S[13] = ybase;
        S[15] = 1.0f;
        float smvp[16];
        for (int cc = 0; cc < 4; cc++)
            for (int rr = 0; rr < 4; rr++) {
                float s = 0;
                for (int k = 0; k < 4; k++) s += mvp[k * 4 + rr] * S[cc * 4 + k];
                smvp[cc * 4 + rr] = s;
            }
        float ubo[32];   /* mvp, light, light2, eye, params */
        memcpy(ubo, mvp, 64);
        ubo[16] = ldir[0]; ubo[17] = ldir[1]; ubo[18] = ldir[2]; ubo[19] = gain;
        ubo[20] = ldir2[0]; ubo[21] = ldir2[1]; ubo[22] = ldir2[2]; ubo[23] = fillg;
        ubo[24] = ex; ubo[25] = ey; ubo[26] = ez; ubo[27] = 0.0f;
        ubo[28] = ambient; ubo[29] = 1.0f;
        ubo[30] = alpharef_solid; ubo[31] = vshade ? 1.0f : 0.0f;

        /* ---- render pass. The MSAA texture is where the car is drawn; the
         * plain texture (or the swapchain) is the resolve target. ---- */
        SDL_GPUTexture *tgt = color_ms ? color_ms : (win ? sw : color);
        SDL_GPUTexture *res = color_ms ? (win ? sw : color) : NULL;
        SDL_GPUColorTargetInfo ct = {0};
        ct.texture = tgt;
        ct.load_op = SDL_GPU_LOADOP_CLEAR;
        ct.store_op = res ? SDL_GPU_STOREOP_RESOLVE : SDL_GPU_STOREOP_STORE;
        ct.clear_color = (SDL_FColor){ 0.055f, 0.06f, 0.075f, 1.0f };
        if (res) ct.resolve_texture = res;
        SDL_GPUDepthStencilTargetInfo dti = {0};
        dti.texture = depth;
        dti.clear_depth = 1.0f;
        dti.load_op = SDL_GPU_LOADOP_CLEAR;
        dti.store_op = SDL_GPU_STOREOP_DONT_CARE;
        SDL_GPURenderPass *rp = SDL_BeginGPURenderPass(cmd, &ct, 1, &dti);

        /* the backdrop first, depth off: mine, see the frame note */
        if (bgpipe) {
            SDL_BindGPUGraphicsPipeline(rp, bgpipe);
            SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
        }

        SDL_BindGPUVertexBuffers(rp, 0, &(SDL_GPUBufferBinding){ .buffer = vb, .offset = 0 }, 1);
        SDL_BindGPUIndexBuffer(rp, &(SDL_GPUBufferBinding){ .buffer = ib, .offset = 0 },
                               SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_PushGPUVertexUniformData(cmd, 0, ubo, sizeof ubo);

        int shadow_draws = 0;
        int nocar = getenv("NOCAR") ? 1 : 0;
        int drawn = 0, skipped = 0;
        /* ---- the ground shadow, then the car ------------------------------ */
        if (shpipe && !nocar) {
            float subo[32];
            memcpy(subo, ubo, sizeof subo);
            memcpy(subo, smvp, 64);
            subo[31] = 2.0f;                       /* shadow mode */
            SDL_BindGPUGraphicsPipeline(rp, shpipe);
            SDL_PushGPUVertexUniformData(cmd, 0, subo, sizeof subo);
            SDL_PushGPUFragmentUniformData(cmd, 0, subo, sizeof subo);
            for (int r = 0; r < c.nruns; r++) {
                DrawRun *R = &c.run[r];
                if (!R->icount || R->alpha) continue;   /* glass casts no shadow */
                if (R->tex < 0 || R->tex >= NT || !tex[R->tex]) continue;
                SDL_DrawGPUIndexedPrimitives(rp, R->icount, 1, R->istart, 0, 0);
                shadow_draws++;
            }
            /* the shadow's MVP is a push uniform: without this the car draws
             * itself flattened onto the floor and vanishes. That is exactly what
             * happened on the first run of this pass. */
            SDL_PushGPUVertexUniformData(cmd, 0, ubo, sizeof ubo);
        }
        for (int pass = 0; pass < 2; pass++) {
            /* ALPHAREF, from Graphics_SwitchAlphaBlendAndTest: 1 while blending,
             * 0x80 while not, with ALPHAFUNC = D3DCMP_GREATER either way. */
            ubo[28] = ambient; ubo[29] = 1.0f;
            ubo[30] = pass ? alpharef_alpha : alpharef_solid;
            ubo[31] = vshade ? 1.0f : 0.0f;
            SDL_PushGPUFragmentUniformData(cmd, 0, ubo, sizeof ubo);
            SDL_BindGPUGraphicsPipeline(rp, pipe[pass][wire]);
            int bound = -2;
            for (int r = 0; r < c.nruns; r++) {
                DrawRun *R = &c.run[r];
                if (!R->icount || nocar) continue;
                if (!!R->alpha != pass) continue;
                int ti = R->tex;
                if (ti < 0 || ti >= NT || !tex[ti]) { skipped++; continue; }
                if (ti != bound) {          /* bind only on a real change */
                    SDL_GPUTextureSamplerBinding b = { .texture = tex[ti], .sampler = samp };
                    SDL_BindGPUFragmentSamplers(rp, 0, &b, 1);
                    bound = ti;
                }
                SDL_DrawGPUIndexedPrimitives(rp, R->icount, 1, R->istart, 0, 0);
                drawn++;
            }
        }
        SDL_EndGPURenderPass(rp);

        if (win) {
            if (!SDL_SubmitGPUCommandBuffer(cmd)) { fprintf(stderr, "[ERR] submit: %s\n", SDL_GetError()); break; }
        } else {
            Uint32 rowbytes = (Uint32)W * 4, imgsz = rowbytes * (Uint32)H;
            SDL_GPUTransferBuffer *dtb = SDL_CreateGPUTransferBuffer(dev,
                &(SDL_GPUTransferBufferCreateInfo){ .usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, .size = imgsz });
            SDL_GPUCopyPass *c2 = SDL_BeginGPUCopyPass(cmd);
            SDL_DownloadFromGPUTexture(c2, &(SDL_GPUTextureRegion){ .texture = color, .w = (Uint32)W, .h = (Uint32)H, .d = 1 },
                &(SDL_GPUTextureTransferInfo){ .transfer_buffer = dtb, .offset = 0 });
            SDL_EndGPUCopyPass(c2);
            SDL_SubmitGPUCommandBuffer(cmd);
            SDL_WaitForGPUIdle(dev);
            void *px = SDL_MapGPUTransferBuffer(dev, dtb, false);
            char bp[1600];
            snprintf(bp, sizeof bp, "%s", shot);
            size_t sl = strlen(bp);
            if (sl < 4 || strcasecmp(bp + sl - 4, ".bmp")) snprintf(bp + sl, 8, ".bmp");
            FILE *o = fopen(bp, "wb");
            if (o) {
                unsigned char hdr[54] = {0};
                int filesz = 54 + (int)imgsz;
                hdr[0] = 'B'; hdr[1] = 'M';
                memcpy(hdr + 2, &filesz, 4);
                int off = 54; memcpy(hdr + 10, &off, 4);
                int ih = 40;  memcpy(hdr + 14, &ih, 4);
                memcpy(hdr + 18, &W, 4); memcpy(hdr + 22, &H, 4);
                short one = 1; memcpy(hdr + 26, &one, 2);
                short bpp = 32; memcpy(hdr + 28, &bpp, 2);
                memcpy(hdr + 34, &imgsz, 4);
                fwrite(hdr, 1, 54, o);
                unsigned char *row = malloc(rowbytes);
                for (int y = H - 1; y >= 0; y--) {
                    unsigned char *src = (unsigned char *)px + (size_t)y * rowbytes;
                    for (int x = 0; x < W; x++) {
                        row[x * 4 + 0] = src[x * 4 + 2];
                        row[x * 4 + 1] = src[x * 4 + 1];
                        row[x * 4 + 2] = src[x * 4 + 0];
                        row[x * 4 + 3] = 255;
                    }
                    fwrite(row, 1, rowbytes, o);
                }
                free(row);
                fclose(o);
                printf("[OK] wrote %s\n", bp);
            }
            int lit = 0, total = W * H;
            unsigned char *p8 = (unsigned char *)px;
            for (int i = 0; i < total; i++) {
                /* background is (0.055,0.06,0.075) -> ~14,15,19 in 8 bit */
                if (p8[i * 4] > 24 || p8[i * 4 + 1] > 24 || p8[i * 4 + 2] > 28) lit++;
            }
            printf("[OK] %d shadow draws, %d texture-run draws (%d runs refused: no texture), "
                   "%d/%d pixels lit (%.1f%%)\n",
                   shadow_draws, drawn, skipped, lit, total, 100.0 * lit / total);
            SDL_UnmapGPUTransferBuffer(dev, dtb);
            SDL_ReleaseGPUTransferBuffer(dev, dtb);
            running = 0;
        }
        frames++;
        if (exitframes > 0 && frames >= exitframes) running = 0;
        if (win) {
            Uint64 now = SDL_GetTicks();
            if (now - tlast >= 1000) {
                char t[160];
                snprintf(t, sizeof t, "CMR2 native  %s  |  %d draws  %.0f fps  |  "
                         "Ls orbit  L2/R2 zoom  A spin  X wire  Y reset  B quit",
                         car, drawn, frames * 1000.0 / (double)(now - t0));
                SDL_SetWindowTitle(win, t);
                tlast = now;
            }
        }
    }

    free(c.run);
    if (win) { SDL_ReleaseWindowFromGPUDevice(dev, win); SDL_DestroyWindow(win); }
    SDL_DestroyGPUDevice(dev);
    SDL_Quit();
    printf("[DONE] %d frames\n", frames);
    return 0;
}
