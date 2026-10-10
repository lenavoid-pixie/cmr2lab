#!/usr/bin/env python3
"""patch_swapfmt.py -- draw into the swapchain's OWN format, so a window works.

Found by running the viewer in a window instead of offscreen:

  Assertion failure at SDL_BeginGPURenderPass
    '!"Resolve texture must have the same format as its corresponding color target!"'

The offscreen path renders into an R8G8B8A8_UNORM texture, which is what every
pipeline was created for. A Deck swapchain is not that format, so the MSAA resolve
had two different formats on the two sides of it. Offscreen was fine, a window was
a core dump -- and the window is the only way Miami can *look* at this without me.

Fix: ask the swapchain for its format before any pipeline exists, use it for the
MSAA texture, the resolve and every colour target description, and print it.

usage: patch_swapfmt.py [--revert]
"""
import os
import shutil
import sys

SRC = 'src/cmr2deck.c'
BAK = 'src/cmr2deck.c.bak-pre-swapfmt'

ANCHOR = """    /* ---- render targets. MSAA is mine: the game rendered into a plain 32-bit
     * surface, so this is presentation, not fidelity -- it is printed as such. */"""

NEW_ANCHOR = """    /* ---- render targets. MSAA is mine: the game rendered into a plain 32-bit
     * surface, so this is presentation, not fidelity -- it is printed as such.
     * The colour format is the SWAPCHAIN's when there is a window: SDL refuses a
     * resolve whose format does not match its colour target, and a Deck
     * swapchain is not R8G8B8A8_UNORM. Offscreen keeps UNORM, which is the
     * format the game's own 32-bit surface corresponds to. */
    SDL_GPUTextureFormat ctf = win ? SDL_GetGPUSwapchainTextureFormat(dev, win)
                                   : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;"""


def main():
    if '--revert' in sys.argv:
        shutil.copyfile(BAK, SRC)
        print('reverted')
        return
    s = open(SRC).read()
    assert s.count(ANCHOR) == 1
    s = s.replace(ANCHOR, NEW_ANCHOR, 1)

    # the MSAA texture
    old = """    if (samples > 1) {
        SDL_GPUTextureCreateInfo ci = {0};
        ci.type = SDL_GPU_TEXTURETYPE_2D;
        ci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;"""
    assert s.count(old) == 1
    s = s.replace(old, old.replace('ci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;',
                                   'ci.format = ctf;'), 1)

    # the resize path rebuilds it with the same format
    old = """                        .type = SDL_GPU_TEXTURETYPE_2D, .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                        .width = W, .height = H, .layer_count_or_depth = 1, .num_levels = 1,
                        .sample_count = (SDL_GPUSampleCount)samples,"""
    assert s.count(old) == 1
    s = s.replace(old, old.replace('.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,',
                                   '.format = ctf,')
                         .replace('.sample_count = (SDL_GPUSampleCount)samples,',
                                  '.sample_count = scount,'), 1)
    # (that cast was a second copy of the SDL_GPUSampleCount bug: SDL_GPU_SAMPLECOUNT_1
    #  is 0, so casting 4 to the enum asks for 8x. Only the resize path still had it.)

    # every colour target description: the car pipes, the backdrop, the shadow
    old = """            SDL_GPUColorTargetDescription ctd = {
                .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                .blend_state = blend,
            };"""
    assert s.count(old) == 1
    s = s.replace(old, old.replace('.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,',
                                   '.format = ctf,'), 1)
    old = "        SDL_GPUColorTargetDescription ctd = { .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, .blend_state = {0} };"
    assert s.count(old) == 1
    s = s.replace(old, "        SDL_GPUColorTargetDescription ctd = { .format = ctf, .blend_state = {0} };", 1)
    old = """        SDL_GPUColorTargetDescription ctd = { .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                              .blend_state = sb };"""
    assert s.count(old) == 1
    s = s.replace(old, """        SDL_GPUColorTargetDescription ctd = { .format = ctf, .blend_state = sb };""", 1)

    # and say it out loud
    old = '''    printf("[CFG] samples=%d cull=%s'''
    assert s.count(old) == 1
    s = s.replace(old, '''    printf("[CFG] colour target format %d (0=UNORM, 1=BGRA8, 2=SRGB...), samples=%d cull=%s'''
                     .replace('%d (0=UNORM, 1=BGRA8, 2=SRGB...)', '%d'),
                1)
    old = '''           samples, cullenv ? cullenv : "back", ambient, gain, fillg,'''
    assert s.count(old) == 1
    s = s.replace(old, '''           (int)ctf, samples, cullenv ? cullenv : "back", ambient, gain, fillg,''', 1)

    if not os.path.exists(BAK):
        shutil.copyfile(SRC, BAK)
    open(SRC, 'w').write(s)
    print(f'patched {SRC}; backup {BAK}')


main()
