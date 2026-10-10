#!/usr/bin/env python3
"""patch_pretty.py -- the presentation round: two lights, a cast shadow, framing.

Round 3 of the "pretty" work, after patch_textures.py (which texture, from the
file) and patch_states.py (the game's alpha test, alpha ref and blend pair,
MSAA, backdrop).

What is INVENTED here, and therefore labelled in the frame note:
  * the ambient value, the key light direction/gain and the fill light,
  * the ground shadow,
  * the backdrop and the camera framing.
What is NOT invented: every texture, every vertex, every triangle, the culling
direction (measured: CULL=front goes dark, see the round's checkpoint), the
alpha test, the alpha ref, the blend pair.

The shadow is a planar projection of the car's own geometry onto y = 0 along the
key light direction -- one extra draw of the same index buffer with a different
MVP. CMR2 does not do this; it has its own shadow-mesh system.

usage: patch_pretty.py [--revert]
"""
import os
import shutil
import sys

SRC = 'src/cmr2deck.c'
BAK = 'src/cmr2deck.c.bak-pre-pretty'

CFG_OLD = '''    float ambient = getenv("AMB")  ? (float)atof(getenv("AMB"))  : 0.30f;
    float gain    = getenv("GAIN") ? (float)atof(getenv("GAIN")) : 0.78f;
    float ldir[3] = { -0.45f, 0.85f, 0.62f };
    if (getenv("LDIR")) sscanf(getenv("LDIR"), "%f,%f,%f", &ldir[0], &ldir[1], &ldir[2]);
    { float l = sqrtf(ldir[0]*ldir[0] + ldir[1]*ldir[1] + ldir[2]*ldir[2]);
      if (l < 1e-6f) { ldir[0] = 0; ldir[1] = 1; ldir[2] = 0; l = 1; }
      ldir[0] /= l; ldir[1] /= l; ldir[2] /= l; }'''

CFG_NEW = '''    float ambient = getenv("AMB")  ? (float)atof(getenv("AMB"))  : 0.40f;
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
    int   shadow  = getenv("SHADOW") ? (atoi(getenv("SHADOW")) != 0) : 1;'''

UBO_OLD = '''        float ubo[28];   /* mvp, light, eye, params */
        memcpy(ubo, mvp, 64);
        ubo[16] = -0.45f; ubo[17] = 0.85f; ubo[18] = 0.62f; ubo[19] = 0.0f;
        ubo[20] = ex; ubo[21] = ey; ubo[22] = ez; ubo[23] = 0.0f;
        ubo[24] = ambient; ubo[25] = gain;
        ubo[26] = alpharef_solid; ubo[27] = vshade ? 1.0f : 0.0f;'''

UBO_NEW = '''        /* ---- the ground shadow matrix: the same geometry, flattened onto
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
        ubo[30] = alpharef_solid; ubo[31] = vshade ? 1.0f : 0.0f;'''

PIPE_OLD = '''            pci.target_info.has_depth_stencil_target = true;
            pipe[tp][wf] = SDL_CreateGPUGraphicsPipeline(dev, &pci);
            if (!pipe[tp][wf]) { fprintf(stderr, "[ERR] pipeline: %s\\n", SDL_GetError()); return 1; }
        }
    }'''

PIPE_NEW = '''            pci.target_info.has_depth_stencil_target = true;
            pipe[tp][wf] = SDL_CreateGPUGraphicsPipeline(dev, &pci);
            if (!pipe[tp][wf]) { fprintf(stderr, "[ERR] pipeline: %s\\n", SDL_GetError()); return 1; }
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
        SDL_GPUColorTargetDescription ctd = { .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                              .blend_state = sb };
        /* the shadow needs its own fragment stage: the car's fragment shader
         * takes a sampler, and a pipeline with a sampler must have one bound at
         * every draw or SDL aborts ("Missing fragment sampler binding!"). */
        snprintf(sp, sizeof sp, "%s%s", sbase ? sbase : "./", "shadow.frag.spv");
        size_t shsz = 0;
        void *shblob = SDL_LoadFile(sp, &shsz);
        if (!shblob) { fprintf(stderr, "[ERR] shadow.frag.spv not next to the binary\\n"); return 1; }
        SDL_GPUShader *sfsh = SDL_CreateGPUShader(dev, &(SDL_GPUShaderCreateInfo){
            .code_size = shsz, .code = shblob, .entrypoint = "main",
            .format = SDL_GPU_SHADERFORMAT_SPIRV, .stage = SDL_GPU_SHADERSTAGE_FRAGMENT });
        if (!sfsh) { fprintf(stderr, "[ERR] shadow fragment shader: %s\\n", SDL_GetError()); return 1; }
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
        if (!shpipe) { fprintf(stderr, "[ERR] shadow pipeline: %s\\n", SDL_GetError()); return 1; }
    }'''

DRAW_OLD = '''        int nocar = getenv("NOCAR") ? 1 : 0;
        if (nocar) { /* control frame: backdrop only */ }
        int drawn = 0, skipped = 0;'''

DRAW_NEW = '''        int nocar = getenv("NOCAR") ? 1 : 0;
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
        }'''

CFG_PRINT_OLD = r'''    printf("[CFG] samples=%d cull=%s ambient=%.2f gain=%.2f light=(%.2f,%.2f,%.2f) "
           "vshade=%d backdrop=%d\n   (ambient+light direction are MINE; alpha test, "
           "alpha ref 0x80/1 and the SRCALPHA/INVSRCALPHA blend pair are the game's)\n",
           samples, cullenv ? cullenv : "back", ambient, gain, ldir[0], ldir[1], ldir[2],
           vshade, use_bg);'''

CFG_PRINT_NEW = r'''    printf("[CFG] samples=%d cull=%s ambient=%.2f key=%.2f fill=%.2f "
           "light=(%.2f,%.2f,%.2f) fill_dir=(%.2f,%.2f,%.2f) vshade=%d backdrop=%d "
           "castshadow=%d\n   (the ambient, both lights, the shadow, the backdrop and the "
           "framing are MINE. The alpha test, the alpha ref 0x80/1, the "
           "SRCALPHA/INVSRCALPHA blend pair and every texture/vertex/triangle are the "
           "game's)\n",
           samples, cullenv ? cullenv : "back", ambient, gain, fillg,
           ldir[0], ldir[1], ldir[2], ldir2[0], ldir2[1], ldir2[2],
           vshade, use_bg, shadow);'''


def main():
    if '--revert' in sys.argv:
        shutil.copyfile(BAK, SRC)
        print('reverted')
        return
    s = open(SRC).read()

    # 0. the shadow count, declared where the draw counts are
    old = "        int nocar = getenv(\"NOCAR\") ? 1 : 0;"
    assert s.count(old) == 1
    s = s.replace(old, "        int shadow_draws = 0;\n" + old, 1)

    # 1. config: the second light and the shadow switch
    assert s.count(CFG_OLD) == 1
    s = s.replace(CFG_OLD, CFG_NEW, 1)

    # 2. the shadow pipeline, created with the other pipelines
    assert s.count(PIPE_OLD) == 1
    s = s.replace(PIPE_OLD, PIPE_NEW, 1)

    # 3. the UBO: 32 floats, light2, params moved to [28..31]
    assert s.count(UBO_OLD) == 1
    s = s.replace(UBO_OLD, UBO_NEW, 1)

    # 4. the shadow draw, before the car's two passes
    assert s.count(DRAW_OLD) == 1
    s = s.replace(DRAW_OLD, DRAW_NEW, 1)

    # 5. the per-pass fragment uniform params move to [28..31]
    old = '''            ubo[24] = ambient; ubo[25] = gain;
            ubo[26] = pass ? alpharef_alpha : alpharef_solid;
            ubo[27] = vshade ? 1.0f : 0.0f;'''
    assert s.count(old) == 1
    s = s.replace(old, '''            ubo[28] = ambient; ubo[29] = 1.0f;
            ubo[30] = pass ? alpharef_alpha : alpharef_solid;
            ubo[31] = vshade ? 1.0f : 0.0f;''', 1)

    # 5b. (removed: step 3 replaces the whole UBO block, including the
    #     initial push, so there is nothing left to move here)

    # 6. the config print, now that light2 exists
    assert s.count(CFG_PRINT_OLD) == 1
    s = s.replace(CFG_PRINT_OLD, CFG_PRINT_NEW, 1)

    # 7. report the shadow draws with the run draws
    old = '''        printf("[OK] %d texture-run draws (%d runs refused: no texture), "'''
    assert s.count(old) == 1
    s = s.replace(old, '''        printf("[OK] %d shadow draws, %d texture-run draws (%d runs refused: no texture), "''', 1)
    old = '''                   drawn, skipped, lit, total, 100.0 * lit / total);'''
    assert s.count(old) == 1
    s = s.replace(old, '''                   shadow_draws, drawn, skipped, lit, total, 100.0 * lit / total);''', 1)

    # 8. FRAMING. The pre-existing default was a small car in a big frame
    #    (dist 1.55 x the car's longest side, 42 degree fov). The hero frame
    #    wants the car to fill the width, so 1.30 x and a slightly lower eye.
    old = "    float yaw0 = 38.0f, elev0 = 16.0f, distk = 1.55f;"
    assert s.count(old) == 1
    s = s.replace(old, "    float yaw0 = 38.0f, elev0 = 14.0f, distk = 1.30f;", 1)

    # 9. the free() at the end must not lose smvp/S: they are stack arrays, fine
    if not os.path.exists(BAK):
        shutil.copyfile(SRC, BAK)
    open(SRC, 'w').write(s)
    print(f'patched {SRC}; backup {BAK}')


main()
