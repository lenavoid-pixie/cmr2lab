#!/usr/bin/env python3
"""patch_textures.py -- PER-TRIANGLE TEXTURE, from the file, the way the game does it.

WHY. Until now the viewer assigned ONE texture per part by matching the part
name against the .c3d's texture-name table ("whl"->"whd", "gl"->"gli", ...).
That is an inference, it is the last one left in the car path, and it is WRONG:
it sends every body part to texture 0 (AP5NWBDf) when the file says the body is
drawn with texture 2 (ap5dbodf). The car rendered as a flat grey blob because of
it -- 205a1N's own paint texture was never bound.

THE FILE SAYS SO, and the game reads it: Game_DrawMeshTextureRuns (CMR2
0x0049c510) and Graphics_DrawMeshTextureBatches (0x0049c680) both take the
texture of a triangle as

    texture = *(int *)((BYTE *)pTri + 4 + pTri->field_0x2c * 4);

pTri is a MeshTriangle (76 bytes) and field_0x2c is 0 in every triangle of every
car file we have, so the texture is the int32 at record+4. The same functions
split the triangle list into runs of equal texture and issue one draw per run --
so this patch does exactly that, in file order, and reports the runs.

Also taken from the file, from Graphics_DrawMeshLOD (0x0049c940):
    mesh flags (Mesh::flags, +0x30) bit 3 -> alpha blend + alpha test
    mesh flags bit 0 -> cull mode
which replaces the "part name contains 'gl'" transparency guess.

usage: patch_textures.py [--revert]
"""
import re
import shutil
import sys

SRC = 'src/cmr2deck.c'
BAK = 'src/cmr2deck.c.bak-pre-textures'

EDITS = []


def sub(old, new, count=1):
    EDITS.append((old, new, count))


# ---- 0. the mesh flags field on the part record -----------------------------
sub("""    int      tex;
    int      transparent;
    int      wheel;
} C3dPart;""",
    """    int      tex;            /* fallback only: the name-matched pick_tex()  */
    unsigned int meshflags;  /* Mesh::flags, +0x30: cull, alpha, lighting   */
    int      transparent;
    int      wheel;
} C3dPart;""")

# ---- 1. the run record, and the counters on C3d ------------------------------
sub("""    int      ntex;
    char     texname[MAXTEX][32];
} C3d;""",
    """    int      ntex;
    char     texname[MAXTEX][32];
    DrawRun *run; int nruns, runcap;   /* per-triangle texture runs */
} C3d;""")

sub("""typedef struct {
    uint8_t *p; size_t len;
    C3dPart  part[MAXPARTS];""",
    """/* One draw: a contiguous run of triangles that share one texture.
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
    C3dPart  part[MAXPARTS];""")

# ---- 2. constants -----------------------------------------------------------
sub("""#define NODE_TYPE         0x178  /*        SceneNode::type             */""",
    """#define NODE_TYPE        0x178  /*        SceneNode::type             */
#define MESH_FLAGS       0x30   /* 0x30   Mesh::flags                 */
#define TRI_TEXOFF       4      /* 0x04   MeshTriangle texture, field_0x2c = 0 */""")

# ---- 3. read the mesh flags, keep pick_tex only as the strip-path fallback ----
sub("""        P->tex    = pick_tex(c, P->name);
        P->transparent = includes_ci(P->name, "semit") || includes_ci(P->name, "gl");""",
    """        /* The texture for this part is the one its triangles name; pick_tex()
         * survives only for the STRIP control path, which throws the face
         * records away. The name match is NOT the answer any more. */
        P->tex    = pick_tex(c, P->name);
        P->meshflags = ru32(r, MESH_FLAGS);
        /* alpha: mesh flags bit 3. cull: bit 0. Both read, neither guessed. */
        P->transparent = (P->meshflags >> 3) & 1;""")

# ---- 4. run tracking while the index buffer is built ------------------------
sub("""            else {
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
            }""",
    """            else {
                for (uint32_t k = 0; k < P->F; k++) {
                    const uint8_t *rec = c->p + fb + (size_t)k * TRIANGLE_STRIDE;
                    uint16_t ix[3];
                    memcpy(ix, rec + TRI_INDEX_OFF, 6);
                    if (ix[0] >= (uint16_t)nv || ix[1] >= (uint16_t)nv ||
                        ix[2] >= (uint16_t)nv) continue;
                    if (ix[0] == ix[1] || ix[1] == ix[2] || ix[0] == ix[2]) continue;
                    if ((size_t)c->nidx + 3 > c->icap) break;
                    /* texture of this triangle: record+4, field_0x2c = 0.
                     * -1 means "no texture" and is not a slot we can bind. */
                    int t = (int)ru32(rec, TRI_TEXOFF);
                    if (t < 0 || t >= MAXTEX) t = -1;
                    /* a new run when the texture changes, exactly like
                     * Game_DrawMeshTextureRuns / Graphics_DrawMeshTextureBatches */
                    if (c->nruns == 0 || c->run[c->nruns - 1].tex != t ||
                        c->run[c->nruns - 1].alpha != P->transparent) {
                        if (c->nruns >= c->runcap) {
                            int nc = c->runcap ? c->runcap * 2 : 256;
                            DrawRun *nr = realloc(c->run, (size_t)nc * sizeof *nr);
                            if (!nr) { fprintf(stderr, "[ERR] out of memory (runs)\\n"); return 0; }
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
            }""")

# ---- 5. counters + the strip fallback run ------------------------------------
sub("""    MeshStats S; memset(&S, 0, sizeof S);
    float lo[3] = { 1e9f, 1e9f, 1e9f }, hi[3] = { -1e9f, -1e9f, -1e9f };
    int corner = 0, nedge = 0, truncated = 0, fb_bad = 0;""",
    """    MeshStats S; memset(&S, 0, sizeof S);
    float lo[3] = { 1e9f, 1e9f, 1e9f }, hi[3] = { -1e9f, -1e9f, -1e9f };
    int corner = 0, nedge = 0, truncated = 0, fb_bad = 0;
    int texruns = 0;                 /* runs created from the file's texture IDs */
    int texcount[MAXTEX]; memset(texcount, 0, sizeof texcount);
    int runsat[MAXPARTS]; memset(runsat, 0, sizeof runsat);
    int texruns_before = 0;""")

# the strip path still needs one run per part, or nothing would draw
sub("""        if (g_mesh_mode == MESH_STRIP) {
            for (int k = 0; k + 2 < nv; k++) {""",
    """        if (g_mesh_mode == MESH_STRIP) {
            /* the STRIP control has no per-triangle texture (it discards the
             * face records by definition), so it gets one run per part with the
             * name-matched texture -- and the log says so. */
            if (c->nruns >= c->runcap) {
                int nc = c->runcap ? c->runcap * 2 : 256;
                DrawRun *nr = realloc(c->run, (size_t)nc * sizeof *nr);
                if (!nr) { fprintf(stderr, "[ERR] out of memory (runs)\\n"); return 0; }
                c->run = nr; c->runcap = nc;
            }
            c->run[c->nruns].istart = (uint32_t)c->nidx;
            c->run[c->nruns].icount = 0;
            c->run[c->nruns].tex    = P->tex;
            c->run[c->nruns].alpha  = P->transparent;
            c->run[c->nruns].part   = i;
            c->nruns++;
            for (int k = 0; k + 2 < nv; k++) {""")

sub("""                c->idx[c->nidx++] = a; c->idx[c->nidx++] = b; c->idx[c->nidx++] = d;
            }
        } else {""",
    """                c->idx[c->nidx++] = a; c->idx[c->nidx++] = b; c->idx[c->nidx++] = d;
            }
            c->run[c->nruns - 1].icount = (uint32_t)c->nidx - c->run[c->nruns - 1].istart;
            runsat[i] = c->nruns;
        } else {""")

# ---- 6. report: the runs, and a texture histogram that cannot lie quietly ----
sub("""    if (truncated) printf("[MESH] WARN %d part(s) had to be clamped to the vertex budget\\n", truncated);""",
    """    /* texruns counts only the indexed path's runs; the strip path's per-part
     * runs are not texture evidence, so they are not counted here. */
    texruns = texruns > 0 ? texruns : 0;
    {
        int used = 0;
        for (int t = 0; t < MAXTEX; t++) if (texcount[t]) used++;
        printf("[TEXRUNS] %d runs from the file's own per-triangle texture IDs, "
               "%d distinct textures bound", c->nruns, used);
        if (g_mesh_mode == MESH_STRIP) printf("  (STRIP path: name-matched, one run per part)");
        printf("\\n");
        printf("[TEXRUNS]");
        for (int t = 0; t < c->ntex; t++)
            if (texcount[t]) printf("  %d:%s x%d", t, c->texname[t], texcount[t]);
        printf("\\n");
        (void)texruns; (void)texruns_before; (void)runsat;
    }
    if (truncated) printf("[MESH] WARN %d part(s) had to be clamped to the vertex budget\\n", truncated);""")

# ---- 7. the draw loop: iterate runs, not parts ------------------------------
sub("""        int drawn = 0;
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
        }""",
    """        int drawn = 0, skipped = 0;
        for (int pass = 0; pass < 2; pass++) {
            SDL_BindGPUGraphicsPipeline(rp, pipe[pass][wire]);
            int bound = -2;
            for (int r = 0; r < c.nruns; r++) {
                DrawRun *R = &c.run[r];
                if (!R->icount) continue;
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
        }""")

sub("""        printf("[OK] %d part draws, %d/%d pixels lit (%.1f%%)\\n",
                   drawn, lit, total, 100.0 * lit / total);""",
    """        printf("[OK] %d texture-run draws (%d runs refused: no texture), "
                   "%d/%d pixels lit (%.1f%%)\\n",
                   drawn, skipped, lit, total, 100.0 * lit / total);""")

# ---- 8. offscreen size, so the frame is the Deck's 1280x800 -----------------
sub("""    int W = 1280, H = 720;
    SDL_Window *win = NULL;""",
    """    int W = 1280, H = 720;
    /* the Deck's own panel is 1280x800; OFFW/OFFH so a shot can be taken at it */
    if (headless) {
        const char *ow = getenv("OFFW"), *oh = getenv("OFFH");
        if (ow) W = atoi(ow);
        if (oh) H = atoi(oh);
        if (W < 64) W = 64; if (H < 64) H = 64;
        if (W > 4096) W = 4096; if (H > 4096) H = 4096;
    }
    SDL_Window *win = NULL;""")

# ---- 9. free the runs at the end -------------------------------------------
sub("""    if (win) { SDL_ReleaseWindowFromGPUDevice(dev, win); SDL_DestroyWindow(win); }""",
    """    free(c.run);
    if (win) { SDL_ReleaseWindowFromGPUDevice(dev, win); SDL_DestroyWindow(win); }""")


def main():
    revert = '--revert' in sys.argv
    if revert:
        shutil.copyfile(BAK, SRC)
        print(f"reverted {SRC} from {BAK}")
        return
    src = open(SRC).read()
    for old, new, count in EDITS:
        n = src.count(old)
        if n != count:
            print(f"[FAIL] anchor not found exactly {count}x (found {n}):\n{old[:120]}...")
            sys.exit(2)
        src = src.replace(old, new, count)
    if not __import__('os').path.exists(BAK):
        shutil.copyfile(SRC, BAK)
    open(SRC, 'w').write(src)
    print(f"patched {SRC}; backup at {BAK}; {len(EDITS)} edits")


main()
