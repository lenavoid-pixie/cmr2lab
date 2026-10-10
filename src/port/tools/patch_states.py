#!/usr/bin/env python3
"""patch_states.py -- make the frame look like the game, using the game's own states.

Round 2 of the "pretty" work. Round 1 (patch_textures.py) fixed WHICH texture is
bound, from the file. This round fixes HOW it is blended, tested and lit, and
gives the frame a real presentation:

  * ALPHA TEST, the game's own: Graphics_SwitchAlphaBlendAndTest sets
    ALPHATESTENABLE and ALPHAREF to 0x80 when blending is off and to 1 when it is
    on, with ALPHAFUNC = GREATER (D3DRS_ALPHAFUNC 0x19 = 7 in the device init at
    Graphics.cpp:1888-1902). So: opaque runs discard alpha <= 0.5, blended runs
    discard alpha <= 1/255. Glass and light fluff stop being drawn as solid slabs.
  * BLEND, the game's own pair: (5,6) = SRCALPHA / INVSRCALPHA
    (Graphics.cpp:6774, Sprite.cpp:561).
  * CULL: selectable, because the right answer is measurable, not guessable.
  * MSAA, so the silhouette is not a staircase.
  * A BACKDROP: mine, not the game's -- see the frame note. CMR2 has no garage.
  * The shading inputs are printed every run, so a frame can never be mistaken
    for a stage render: ambient, light direction, gain, cull, samples.

usage: patch_states.py [--revert]
"""
import os
import shutil
import sys

SRC = 'src/cmr2deck.c'
BAK = 'src/cmr2deck.c.bak-pre-states'

CONFIG_BLOCK = r'''
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
    float ambient = getenv("AMB")  ? (float)atof(getenv("AMB"))  : 0.30f;
    float gain    = getenv("GAIN") ? (float)atof(getenv("GAIN")) : 0.78f;
    float ldir[3] = { -0.45f, 0.85f, 0.62f };
    if (getenv("LDIR")) sscanf(getenv("LDIR"), "%f,%f,%f", &ldir[0], &ldir[1], &ldir[2]);
    { float l = sqrtf(ldir[0]*ldir[0] + ldir[1]*ldir[1] + ldir[2]*ldir[2]);
      if (l < 1e-6f) { ldir[0] = 0; ldir[1] = 1; ldir[2] = 0; l = 1; }
      ldir[0] /= l; ldir[1] /= l; ldir[2] /= l; }
    int   vshade  = getenv("VSHADE") ? 1 : 0;   /* vertex colour as diffuse term */
    int   use_bg  = getenv("BG") ? (atoi(getenv("BG")) != 0) : 1;
    float alpharef_alpha = 1.0f / 255.0f;       /* ALPHAREF 1   + D3DCMP_GREATER */
    float alpharef_solid = 128.0f / 255.0f;     /* ALPHAREF 128 + D3DCMP_GREATER */
'''

TARGETS_BLOCK = r'''    /* ---- render targets. MSAA is mine: the game rendered into a plain 32-bit
     * surface, so this is presentation, not fidelity -- it is printed as such. */
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
        ci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        ci.width = W; ci.height = H; ci.layer_count_or_depth = 1; ci.num_levels = 1;
        ci.sample_count = scount;
        ci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        color_ms = SDL_CreateGPUTexture(dev, &ci);
    }
    printf("[CFG] samples=%d cull=%s ambient=%.2f gain=%.2f light=(%.2f,%.2f,%.2f) "
           "vshade=%d backdrop=%d\n   (ambient+light direction are MINE; alpha test, "
           "alpha ref 0x80/1 and the SRCALPHA/INVSRCALPHA blend pair are the game's)\n",
           samples, cullenv ? cullenv : "back", ambient, gain, ldir[0], ldir[1], ldir[2],
           vshade, use_bg);

'''

SHADERS_BLOCK = r'''    /* ---- shaders ---- */
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
    if (use_bg) {
        SDL_GPUShader *bv = SDL_CreateGPUShader(dev, &(SDL_GPUShaderCreateInfo){
            .code_size = sblen[2], .code = sblob[2], .entrypoint = "main",
            .format = SDL_GPU_SHADERFORMAT_SPIRV, .stage = SDL_GPU_SHADERSTAGE_VERTEX });
        SDL_GPUShader *bf = SDL_CreateGPUShader(dev, &(SDL_GPUShaderCreateInfo){
            .code_size = sblen[3], .code = sblob[3], .entrypoint = "main",
            .format = SDL_GPU_SHADERFORMAT_SPIRV, .stage = SDL_GPU_SHADERSTAGE_FRAGMENT });
        if (!bv || !bf) { fprintf(stderr, "[ERR] backdrop shader: %s\n", SDL_GetError()); return 1; }
        SDL_GPUColorTargetDescription ctd = { .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, .blend_state = {0} };
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

'''

PASS_BLOCK = r'''        /* ---- render pass. The MSAA texture is where the car is drawn; the
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

        int drawn = 0, skipped = 0;
        for (int pass = 0; pass < 2; pass++) {
            /* ALPHAREF, from Graphics_SwitchAlphaBlendAndTest: 1 while blending,
             * 0x80 while not, with ALPHAFUNC = D3DCMP_GREATER either way. */
            ubo[24] = ambient; ubo[25] = gain;
            ubo[26] = pass ? alpharef_alpha : alpharef_solid;
            ubo[27] = vshade ? 1.0f : 0.0f;
            SDL_PushGPUFragmentUniformData(cmd, 0, ubo, sizeof ubo);
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
        }
        SDL_EndGPURenderPass(rp);'''


def main():
    if '--revert' in sys.argv:
        shutil.copyfile(BAK, SRC)
        print('reverted')
        return
    s = open(SRC).read()

    def cut(start_marker, end_marker, new, label):
        i = s.index(start_marker)
        j = s.index(end_marker, i) + len(end_marker)   # the end marker is REPLACED
        return s[:i] + new + s[j:]

    # 1. config block, right before the SDL init
    anchor = "    /* ---- SDL / GPU ---- */"
    assert s.count(anchor) == 1
    s = s.replace(anchor, CONFIG_BLOCK + anchor, 1)

    # 2. render targets + MSAA
    s = cut("    SDL_GPUTexture *color = NULL;",
            "    SDL_GPUSampler *samp = SDL_CreateGPUSampler(dev, &(SDL_GPUSamplerCreateInfo){",
            TARGETS_BLOCK + "    SDL_GPUSampler *samp = SDL_CreateGPUSampler(dev, &(SDL_GPUSamplerCreateInfo){",
            'targets')

    # 3. shaders + pipelines (includes the backdrop pipeline)
    s = cut("    /* ---- shaders ---- */", "    /* ---- geometry upload ---- */", SHADERS_BLOCK, 'shaders')

    # 4. the render pass: resolve, backdrop, per-pass alpha ref, fragment uniforms
    s = cut("        SDL_GPUColorTargetInfo ct = {0};", "        SDL_EndGPURenderPass(rp);", PASS_BLOCK, 'pass')

    # 5. the UBO grows from 24 to 28 floats (params at [24..27])
    old = "        float ubo[24];"
    assert s.count(old) == 1
    s = s.replace(old, "        float ubo[28];   /* mvp, light, eye, params */", 1)
    old = """        ubo[20] = ex; ubo[21] = ey; ubo[22] = ez; ubo[23] = 0.0f;"""
    assert s.count(old) == 1
    s = s.replace(old, """        ubo[20] = ex; ubo[21] = ey; ubo[22] = ez; ubo[23] = 0.0f;
        ubo[24] = ambient; ubo[25] = gain;
        ubo[26] = alpharef_solid; ubo[27] = vshade ? 1.0f : 0.0f;""", 1)

    # 6. the swapchain texture is now the resolve target, not the render target
    old = """        SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(dev);
        if (!cmd) break;
        SDL_GPUTexture *tgt = color;
        if (win) {
            if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, win, &tgt, NULL, NULL)) {
                fprintf(stderr, "[ERR] swapchain: %s\\n", SDL_GetError());
                SDL_SubmitGPUCommandBuffer(cmd);
                break;
            }
            if (!tgt) { SDL_SubmitGPUCommandBuffer(cmd); continue; }
            SDL_GetWindowSizeInPixels(win, &W, &H);
        }"""
    assert s.count(old) == 1, s.count(old)
    s = s.replace(old, """        SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(dev);
        if (!cmd) break;
        SDL_GPUTexture *sw = NULL;
        if (win) {
            if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, win, &sw, NULL, NULL)) {
                fprintf(stderr, "[ERR] swapchain: %s\\n", SDL_GetError());
                SDL_SubmitGPUCommandBuffer(cmd);
                break;
            }
            if (!sw) { SDL_SubmitGPUCommandBuffer(cmd); continue; }
            /* a resize invalidates the MSAA and depth textures */
            Uint32 nw = 0, nh = 0;
            SDL_GetWindowSizeInPixels(win, &nw, &nh);
            if ((int)nw != W || (int)nh != H) { W = (int)nw; H = (int)nh; }
        }""", 1)

    # 6b. NOCAR=1 draws the backdrop and the alpha pass and nothing else. It is
    #     the control frame: "what the car covers" is only measurable against a
    #     frame that has no car in it.
    old = "        int drawn = 0, skipped = 0;"
    assert s.count(old) == 1
    s = s.replace(old, "        int nocar = getenv(\"NOCAR\") ? 1 : 0;\n"
                       "        if (nocar) { /* control frame: backdrop only */ }\n" + old, 1)
    old = "                if (!R->icount) continue;"
    assert s.count(old) == 1
    s = s.replace(old, "                if (!R->icount || nocar) continue;", 1)

    # 7. a window resize has to rebuild the MSAA and depth textures, or the
    #    next render pass targets a stale size
    old = """            if ((int)nw != W || (int)nh != H) { W = (int)nw; H = (int)nh; }"""
    assert s.count(old) == 1
    s = s.replace(old, """            if ((int)nw != W || (int)nh != H) {
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
                        .type = SDL_GPU_TEXTURETYPE_2D, .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                        .width = W, .height = H, .layer_count_or_depth = 1, .num_levels = 1,
                        .sample_count = (SDL_GPUSampleCount)samples,
                        .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET });
                }
            }""", 1)

    if not os.path.exists(BAK):
        shutil.copyfile(SRC, BAK)
    open(SRC, 'w').write(s)
    print(f'patched {SRC}; backup {BAK}')


main()
