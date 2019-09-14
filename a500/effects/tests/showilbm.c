#include "startup.h"
#include "hardware.h"
#include "coplist.h"
#include "gfx.h"
#include "ilbm.h"

static BitmapT *bitmap;
static CopListT *cp;

static void Load() {
  bitmap = LoadILBMCustom("data/test.ilbm", BM_KEEP_PACKED|BM_LOAD_PALETTE);
  cp = NewCopList(100);
}

static void UnLoad() {
  DeleteCopList(cp);
  DeletePalette(bitmap->palette);
  DeleteBitmap(bitmap);
}

static void Init() {
  short w = bitmap->width;
  short h = bitmap->height;
  short xs = X((320 - w) / 2);
  short ys = Y((256 - h) / 2);

  {
    int lines = ReadLineCounter();
    BitmapUnpack(bitmap, BM_DISPLAYABLE);
    lines = ReadLineCounter() - lines;
    Log("Bitmap unpacking took %d raster lines.\n", lines);
  }

  CopInit(cp);
  CopSetupGfxSimple(cp, MODE_LORES, bitmap->depth, xs, ys, w, h);
  CopSetupBitplanes(cp, NULL, bitmap, bitmap->depth);
  CopLoadPal(cp, bitmap->palette, 0);
  CopEnd(cp);

  CopListActivate(cp);
  EnableDMA(DMAF_RASTER);
}

EffectT Effect = { Load, UnLoad, Init, NULL, NULL };
