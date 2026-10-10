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
 *     - BFL = trailing run of 24-byte records (name[12], u32, u32, u32) with the
 *       texture blobs earlier in the payload in TOC order. Every blob is DXT5.
 *   INFERRED / STILL OPEN, and flagged honestly rather than papered over:
 *     - WHEEL PLACEMENT. The four wheel parts carry identical geometry and sit at
 *       the local origin; the part record has no translation field. It is not in
 *       the .c3d, and not in the .cin (that file is CMPR texture data). Best
 *       remaining lead: the c18 block -- 15 records of 396 bytes, the node /
 *       scene-graph block that the header's packed type pairs (0x000E000F,
 *       0x00020003) refer to. Until that is decoded, wheels are placed by
 *       inference. Override at runtime: WX / WY / WZ, or NOWHEELPLACE=1.
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
    if (c0 > c1) {
        for (int i = 0; i < 3; i++) {
            r[2 + i]  = (uint8_t)(((3 - i) * r[0]  + (i + 1) * r[1])  / 3);
            g[2 + i]  = (uint8_t)(((3 - i) * g[0]  + (i + 1) * g[1])  / 3);
            bl[2 + i] = (uint8_t)(((3 - i) * bl[0] + (i + 1) * bl[1]) / 3);
        }
    } else {
        for (int i = 0; i < 3; i++) {
            r[2 + i]  = (uint8_t)(((2 - i) * r[0]  + (i + 1) * r[1])  / 2);
            g[2 + i]  = (uint8_t)(((2 - i) * g[0]  + (i + 1) * g[1])  / 2);
            bl[2 + i] = (uint8_t)(((2 - i) * bl[0] + (i + 1) * bl[1]) / 2);
        }
    }
    uint8_t a[8];
    a[0] = b[4]; a[1] = b[5];
    if (a[0] > a[1]) {
        for (int i = 1; i <= 6; i++)
            a[i + 1] = (uint8_t)(((7 - i) * a[0] + i * a[1]) / 7);
    } else {
        for (int i = 1; i <= 4; i++)
            a[i + 1] = (uint8_t)(((5 - i) * a[0] + i * a[1]) / 5);
        a[6] = 0; a[7] = 255;
    }
    uint32_t cbits = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
                     ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
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

/* ------------------------------------------------------------ BFL archive --- */
#define BFL_MAX 1024
typedef struct { char name[16]; size_t off, len; } BflEnt;
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

static int bfl_parse(const uint8_t *p, size_t len, Bfl *out) {
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
    int      tex;
    int      transparent;
    int      wheel;
} C3dPart;

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

static int c3d_load(C3d *c, const char *path, int wheelplace,
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

    for (int i = 0; i < c1a; i++) {
        const uint8_t *r = c->p + C1A + (size_t)i * MESH_STRIDE;
        C3dPart *P = &c->part[i];
        memcpy(P->name, r, 12); P->name[12] = 0;
        for (char *s = P->name; *s; s++) if (*s == ' ') *s = 0;
        P->vblk   = ru32(r, MESH_VBLOFF);
        P->V      = ru32(r, MESH_VCOUNT);
        P->facoff = ru32(r, MESH_TRIOFF);
        P->F      = ru32(r, MESH_TCOUNT);
        P->tex    = pick_tex(c, P->name);
        P->transparent = includes_ci(P->name, "semit") || includes_ci(P->name, "gl");
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
            if (q >= 0) {
                /* INFERRED placement -- M2 replaces this with the c18 decode.
                 * Mirror the corner signs so the four wheels land on four
                 * corners.  Override with WX/WY/WZ, or NOWHEELPLACE=1. */
                static const float SX[4] = { +1.0f, +1.0f, -1.0f, -1.0f };
                static const float SZ[4] = { +1.0f, -1.0f, +1.0f, -1.0f };
                float *v = (float *)dst;
                v[0] += wx * SX[q];
                v[1] += wy;
                v[2] += wz * SZ[q];
            }
            float *v = (float *)dst;
            for (int a = 0; a < 3; a++) {
                if (v[a] != v[a] || fabsf(v[a]) > 1e5f) v[a] = 0.0f;
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
            for (int k = 0; k + 2 < nv; k++) {
                if ((size_t)c->nidx + 3 > c->icap) break;
                uint32_t a = P->vstart + (uint32_t)k;
                uint32_t b = P->vstart + (uint32_t)k + 1;
                uint32_t d = P->vstart + (uint32_t)k + 2;
                if (k & 1) { uint32_t t = a; a = b; b = t; }   /* keep winding */
                c->idx[c->nidx++] = a; c->idx[c->nidx++] = b; c->idx[c->nidx++] = d;
            }
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
                    c->idx[c->nidx++] = P->vstart + ix[0];
                    c->idx[c->nidx++] = P->vstart + ix[1];
                    c->idx[c->nidx++] = P->vstart + ix[2];
                }
            }
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

    printf("[MESH] %-7s parts=%d verts=%d tris=%d  (vcap=%zu/%zu icap=%zu/%zu)\n",
           g_mesh_mode == MESH_STRIP ? "strip" : "indexed", c->nparts, c->nverts, c->ntri,
           (size_t)c->nverts, c->vcap, (size_t)c->nidx, c->icap);
    printf("[MESH] degenerate tris=%d  holes/empty parts=%d  bridging = %.2f%% "
           "(tris over 5x median edge)  median edge = %.4f of part diag\n",
           S.degenerate, S.holes, S.bridge_pct, S.median_edge_ratio);
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
"  Native CMR2 car viewer. Reads the game's own .c3d geometry and .bfl DXT5\n"
"  textures and renders them on the Deck's GPU via SDL3/Vulkan.\n"
"  controls: arrows orbit | A/D yaw | W wireframe | Q/S zoom | SPACE spin\n"
"            R reset | wheel zoom | ESC quit\n"
"  env:      CMR2_GAME, WX/WY/WZ wheel offset, NOWHEELPLACE=1, SPIN=1\n"
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
    float wx = 1.20f, wy = -0.23f, wz = 0.75f;
    const char *e;
    if ((e = getenv("WX"))) wx = (float)atof(e);
    if ((e = getenv("WY"))) wy = (float)atof(e);
    if ((e = getenv("WZ"))) wz = (float)atof(e);
    /* fixed camera, so two runs can be compared pixel for pixel */
    float yaw0 = 38.0f, elev0 = 16.0f, distk = 1.55f;
    if ((e = getenv("YAW")))  yaw0  = (float)atof(e);
    if ((e = getenv("ELEV"))) elev0 = (float)atof(e);
    if ((e = getenv("DIST"))) distk = (float)atof(e);

    printf("=== CMR2 native viewer -- Steam Deck, x86_64, SDL3/Vulkan ===\n");
    printf("[INFO] car  %s\n", car);
    printf("[INFO] c3d  %s\n", c3dpath);

    C3d c;
    if (!c3d_load(&c, c3dpath, wheelplace, wx, wy, wz)) return 1;
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

    /* ---- SDL / GPU ---- */
    int headless = (shot != NULL);
    if (headless) SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    /* (stdout buffering is set at the top of main -- setvbuf is UB this late) */
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) { fprintf(stderr, "[ERR] SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_GPUDevice *dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, NULL);
    if (!dev) { fprintf(stderr, "[ERR] CreateGPUDevice: %s\n", SDL_GetError()); return 1; }
    printf("[OK] GPU device: %s\n", SDL_GetGPUDeviceDriver(dev));

    int W = 1280, H = 720;
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
    SDL_GPUTexture *color = NULL;
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
        .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET });

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
        if (blob) rgba = dds_decode(blob, dl, &w0, &h0);
        if (rgba) nloaded++;
        else {
            nmissing++;
            w0 = h0 = 2; rgba = malloc(16);
            for (int k = 0; k < 16; k++) rgba[k] = 0xff;
            if (!blob) printf("[WARN] tex[%2d] %-14s not in .bfl\n", i, c.texname[i]);
            else       printf("[WARN] tex[%2d] %-14s undecodable -> flat white\n", i, c.texname[i]);
        }
        tw[i] = w0; th[i] = h0; got[i] = rgba ? 1 : 0;
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
    size_t vsz = 0, fsz = 0;
    snprintf(sp, sizeof sp, "%s%s", sbase ? sbase : "./", "car.vert.spv");
    void *vs = SDL_LoadFile(sp, &vsz);
    snprintf(sp, sizeof sp, "%s%s", sbase ? sbase : "./", "car.frag.spv");
    void *fs = SDL_LoadFile(sp, &fsz);
    if (!vs || !fs) { fprintf(stderr, "[ERR] shaders (car.vert.spv/car.frag.spv) not next to the binary\n"); return 1; }
    SDL_GPUShader *vsh = SDL_CreateGPUShader(dev, &(SDL_GPUShaderCreateInfo){
        .code_size = vsz, .code = vs, .entrypoint = "main",
        .format = SDL_GPU_SHADERFORMAT_SPIRV, .stage = SDL_GPU_SHADERSTAGE_VERTEX,
        .num_uniform_buffers = 1 });
    SDL_GPUShader *fsh = SDL_CreateGPUShader(dev, &(SDL_GPUShaderCreateInfo){
        .code_size = fsz, .code = fs, .entrypoint = "main",
        .format = SDL_GPU_SHADERFORMAT_SPIRV, .stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .num_samplers = 1 });
    if (!vsh || !fsh) { fprintf(stderr, "[ERR] shader: %s\n", SDL_GetError()); return 1; }

    SDL_GPUVertexBufferDescription vbd = { .slot = 0, .pitch = VSTRIDE,
                                           .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX };
    SDL_GPUVertexAttribute attrs[4] = {
        { .location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,      .offset = 0  },
        { .location = 1, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,      .offset = 12 },
        { .location = 2, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, .offset = 24 },
        { .location = 3, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,      .offset = 28 },
    };
    SDL_GPUGraphicsPipeline *pipe[2][2];
    for (int tp = 0; tp < 2; tp++) {
        for (int wf = 0; wf < 2; wf++) {
            SDL_GPUColorTargetBlendState blend = {0};
            if (tp) {
                blend.enable_blend = true;
                blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
                blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
                blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
                blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
                blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
                blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            }
            SDL_GPUColorTargetDescription ctd = {
                .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
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
            pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
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

    /* ---- geometry upload ---- */
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
        SDL_GPUTexture *tgt = color;
        if (win) {
            if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, win, &tgt, NULL, NULL)) {
                fprintf(stderr, "[ERR] swapchain: %s\n", SDL_GetError());
                SDL_SubmitGPUCommandBuffer(cmd);
                break;
            }
            if (!tgt) { SDL_SubmitGPUCommandBuffer(cmd); continue; }
            SDL_GetWindowSizeInPixels(win, &W, &H);
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
        float ubo[24];
        memcpy(ubo, mvp, 64);
        ubo[16] = -0.45f; ubo[17] = 0.85f; ubo[18] = 0.62f; ubo[19] = 0.0f;
        ubo[20] = ex; ubo[21] = ey; ubo[22] = ez; ubo[23] = 0.0f;

        SDL_GPUColorTargetInfo ct = {0};
        ct.texture = tgt;
        ct.load_op = SDL_GPU_LOADOP_CLEAR;
        ct.store_op = SDL_GPU_STOREOP_STORE;
        ct.clear_color = (SDL_FColor){ 0.055f, 0.06f, 0.075f, 1.0f };
        SDL_GPUDepthStencilTargetInfo dti = {0};
        dti.texture = depth;
        dti.clear_depth = 1.0f;
        dti.load_op = SDL_GPU_LOADOP_CLEAR;
        dti.store_op = SDL_GPU_STOREOP_DONT_CARE;
        SDL_GPURenderPass *rp = SDL_BeginGPURenderPass(cmd, &ct, 1, &dti);
        SDL_BindGPUVertexBuffers(rp, 0, &(SDL_GPUBufferBinding){ .buffer = vb, .offset = 0 }, 1);
        SDL_BindGPUIndexBuffer(rp, &(SDL_GPUBufferBinding){ .buffer = ib, .offset = 0 },
                               SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_PushGPUVertexUniformData(cmd, 0, ubo, sizeof ubo);

        int drawn = 0;
        for (int pass = 0; pass < 2; pass++) {
            SDL_BindGPUGraphicsPipeline(rp, pipe[pass][wire]);
            for (int i = 0; i < c.nparts; i++) {
                C3dPart *P = &c.part[i];
                if (!P->built) continue;
                if (!!P->transparent != pass) continue;
                int ti = P->tex;
                if (ti < 0 || ti >= NT || !tex[ti]) continue;
                SDL_GPUTextureSamplerBinding b = { .texture = tex[ti], .sampler = samp };
                SDL_BindGPUFragmentSamplers(rp, 0, &b, 1);
                SDL_DrawGPUIndexedPrimitives(rp, P->built, 1, P->istart, 0, 0);
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
            printf("[OK] %d part draws, %d/%d pixels lit (%.1f%%)\n",
                   drawn, lit, total, 100.0 * lit / total);
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

    if (win) { SDL_ReleaseWindowFromGPUDevice(dev, win); SDL_DestroyWindow(win); }
    SDL_DestroyGPUDevice(dev);
    SDL_Quit();
    printf("[DONE] %d frames\n", frames);
    return 0;
}
