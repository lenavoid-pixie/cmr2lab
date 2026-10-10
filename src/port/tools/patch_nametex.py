#!/usr/bin/env python3
"""patch_nametex.py -- NAMETEX=1: the OLD name-matched texture path, kept as an A/B.

The texture fix in patch_textures.py is the whole reason the car stopped being a
grey blob, and "the texture is different, therefore it is better" is not evidence.
This adds the one knob needed to render the SAME frame both ways so the two can be
diffed with tools/preview.py --diff.

  NAMETEX=1  every triangle of a part uses pick_tex(part name) -- the old guess
  default    every triangle uses the int32 at MeshTriangle+4 (the file's answer)

usage: patch_nametex.py [--revert]
"""
import os
import shutil
import sys

SRC = 'src/cmr2deck.c'
BAK = 'src/cmr2deck.c.bak-pre-nametex'

OLD = """                    /* texture of this triangle: record+4, field_0x2c = 0.
                     * -1 means "no texture" and is not a slot we can bind. */
                    int t = (int)ru32(rec, TRI_TEXOFF);"""

NEW = """                    /* texture of this triangle: record+4, field_0x2c = 0.
                     * -1 means "no texture" and is not a slot we can bind.
                     * NAMETEX=1 is the OLD name-matched guess, kept so the two
                     * paths can be rendered and diffed (tools/preview.py --diff)
                     * instead of argued about. */
                    int t = nametex ? P->tex : (int)ru32(rec, TRI_TEXOFF);"""


def main():
    if '--revert' in sys.argv:
        shutil.copyfile(BAK, SRC)
        print('reverted')
        return
    s = open(SRC).read()
    if 'int nametex' not in s:
        old = "    int texruns = 0;                 /* runs created from the file's texture IDs */"
        assert s.count(old) == 1
        s = s.replace(old, "    int nametex = getenv(\"NAMETEX\") ? 1 : 0;   /* the OLD path, for the A/B */\n" + old, 1)
    assert s.count(OLD) == 1
    s = s.replace(OLD, NEW, 1)
    # say which path produced the runs, in the run report itself
    old = '''        printf("[TEXRUNS] %d runs from the file's own per-triangle texture IDs, "
               "%d distinct textures bound", c->nruns, used);'''
    assert s.count(old) == 1
    s = s.replace(old, '''        printf("[TEXRUNS] %d runs from the %s, %d distinct textures bound", c->nruns,
               nametex ? "PART NAMES (NAMETEX=1, the old guess)" : "file's own per-triangle texture IDs", used);''', 1)
    if not os.path.exists(BAK):
        shutil.copyfile(SRC, BAK)
    open(SRC, 'w').write(s)
    print(f'patched {SRC}; backup {BAK}')


main()
