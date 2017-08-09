#include "sushiboyz.h"
#include "blitter.h"
#include "coplist.h"
#include "memory.h"
#include "fx.h"
#include "random.h"
#include "color.h"
#include "png.h"
#include "ilbm.h"
#include "color.h"
#include "sprite.h"

#define WIDTH 320
#define HEIGHT 256
#define DEPTH 3
#define BGCOL 0x000

#define N 1024
#define SIZE 8
#define TILESIZE (N / SIZE)
#define GAP 24

/* These are selected in such a way so line lenght
 * (HEIGHT - FAR_Y) is divisible by 8. */
#define NEAR_Z (256 + 32)
#define FAR_Z 512
#define FAR_Y (HEIGHT * NEAR_Z / FAR_Z)
#define FAR_W (WIDTH * FAR_Z / 256)

static SpriteT *thunder[20];
static BitmapT *window0, *window1, *thunders;
static PaletteT *palette;
static CopListT *cp0, *cp1;
static PixmapT *texture;
static UWORD tileColor[SIZE * SIZE];
static WORD tileCycle[SIZE * SIZE];
static WORD tileEnergy[SIZE * SIZE];
static BOOL thunders_active = FALSE;

static TrackT *floor_x, *floor_y, *thunders_event;

typedef struct {
  WORD x1, x2, y2;
  WORD pad;
} LineDataT;

static LineDataT *vert;
static WORD *horiz;

static void FloorPrecalc() {
  WORD i;

  for (i = 0; i < N; i++) {
    WORD x = (i - N / 2) * FAR_W / N;
    WORD far_x = WIDTH / 2 + (x * 256) / FAR_Z;
    WORD near_x = WIDTH / 2 + (x * 640) / NEAR_Z;
    WORD near_y = HEIGHT - 1;
    
    if (near_x < 0) {
      near_y = FAR_Y + (0 - far_x) * ((HEIGHT - 1) - FAR_Y) / (near_x - far_x);
      near_x = 0;
    }
    if (near_x > WIDTH - 1) {
      near_y = FAR_Y + ((WIDTH - 1) - far_x) * ((HEIGHT - 1) - FAR_Y) / (near_x - far_x);
      near_x = WIDTH - 1;
    }
    
    vert[i].x1 = far_x;
    vert[i].x2 = near_x;
    vert[i].y2 = near_y;
  }

  for (i = 0; i < N; i++) {
    WORD z = FAR_Z + ((NEAR_Z - FAR_Z) * i) / N;
    
    horiz[i] = HEIGHT * NEAR_Z / z;
  }
}

static void Load() {
  texture = LoadPNG("17_thunders_floor.png", PM_RGB12, MEMF_PUBLIC);
  thunders = LoadILBMCustom("17_thunders.iff", BM_KEEP_PACKED|BM_LOAD_PALETTE);
  palette = thunders->palette;

  /* columns: 1..7 (7) */
  /* rows: 0..7 (8) */

  floor_x = TrackLookup(tracks, "thunders.floor.x");
  floor_y = TrackLookup(tracks, "thunders.floor.y");
  thunders_event = TrackLookup(tracks, "thunders.active");
}

static void Prepare() {
  WORD i;

  BitmapUnpack(thunders, 0);

  for (i = 0; i < thunders->width / 16; i++) {
    WORD xo = X((WIDTH - 32) / 2) + (i & 1 ? 16 : 0);
    WORD yo = Y((HEIGHT - 128) / 2);

    thunder[i] = NewSpriteFromBitmap(128, thunders, i * 16, 0);
    UpdateSprite(thunder[i], xo, yo);
  }

  DeleteBitmap(thunders); thunders = NULL;

  vert = MemAlloc(sizeof(LineDataT) * N, MEMF_PUBLIC);
  horiz = MemAlloc(sizeof(WORD) * N, MEMF_PUBLIC);

  FloorPrecalc();

  for (i = 0; i < SIZE * SIZE; i++)
    tileCycle[i] = random() & SIN_MASK;
}

static void UnLoad() {
  WORD i;

  MemFree(vert);
  MemFree(horiz);

  for (i = 0; i < 20; i++)
    DeleteSprite(thunder[i]);

  DeletePixmap(texture);
  DeletePalette(palette);
  DeleteBitmap(thunders);
}

static void MakeCopperList(CopListT *cp, BitmapT *screen) {
  WORD j;

  CopInit(cp);
  CopSetupGfxSimple(cp, MODE_LORES, DEPTH, X(0), Y(0), WIDTH, HEIGHT);
  CopSetupBitplanes(cp, NULL, screen, DEPTH);
  CopSetupSprites(cp, NULL);
  for (j = 0; j < 8; j++)
    CopSetRGB(cp, j, BGCOL);
  CopEnd(cp);
}

static void Init() {
  static BitmapT recycled0, recycled1;

  window0 = &recycled0;
  window1 = &recycled1;

  InitSharedBitmap(window0, WIDTH, HEIGHT, DEPTH, screen0);
  InitSharedBitmap(window1, WIDTH, HEIGHT, DEPTH, screen1);

  cp0 = NewCopList((HEIGHT - FAR_Y) * 16 + 200);
  cp1 = NewCopList((HEIGHT - FAR_Y) * 16 + 200);

  MakeCopperList(cp0, window0);
  MakeCopperList(cp1, window1);

  custom->dmacon = DMAF_SETCLR | DMAF_BLITTER | DMAF_BLITHOG;
  BitmapClear(window0);
  BitmapClear(window1);

  CopListActivate(cp1);
  custom->dmacon = DMAF_SETCLR | DMAF_RASTER | DMAF_SPRITE;
}

static void Kill() {
  custom->dmacon = DMAF_COPPER | DMAF_RASTER | DMAF_SPRITE | DMAF_BLITTER | DMAF_BLITHOG;

  DeleteCopList(cp0);
  DeleteCopList(cp1);
}

static void DrawLine(APTR data asm("a2"), WORD x1 asm("d2"), WORD y1 asm("d3"), WORD x2 asm("d4"), WORD y2 asm("d5")) {
  UWORD bltcon1 = LINE_ONEDOT;
  APTR start = data;
  WORD dx, dy, derr;

  /* Always draw the line downwards. */
  if (y1 > y2) {
    swapr(x1, x2);
    swapr(y1, y2);
  }

  /* Word containing the first pixel of the line. */
  start += y1 * (WIDTH / 8);
  start += (x1 >> 3) & ~1;

  dx = x2 - x1;
  dy = y2 - y1;

  if (dx < 0) {
    dx = -dx;
    if (dx >= dy) {
      bltcon1 |= AUL | SUD;
    } else {
      bltcon1 |= SUL;
      swapr(dx, dy);
    }
  } else {
    if (dx >= dy) {
      bltcon1 |= SUD;
    } else {
      swapr(dx, dy);
    }
  }

  derr = dy + dy - dx;
  if (derr < 0)
    bltcon1 |= SIGNFLAG;

  {
    UWORD bltcon0 = rorw(x1 & 15, 4) | LINE_EOR;
    UWORD bltamod = derr - dx;
    UWORD bltbmod = dy + dy;
    UWORD bltsize = (dx << 6) + 66;

    WaitBlitter();

    custom->bltcon0 = bltcon0;
    custom->bltcon1 = bltcon1;
    custom->bltamod = bltamod;
    custom->bltbmod = bltbmod;
    custom->bltapt = (APTR)(LONG)derr;
    custom->bltcpt = start;
    custom->bltdpt = data;
    custom->bltsize = bltsize;
  }
}

static __regargs void DrawStripes(WORD xo, WORD kxo) {
  static LineDataT first = { 0, WIDTH - 1, FAR_Y };
  LineDataT *line;
  WORD k;

  /* Setup fast line drawing. */
  WaitBlitter();
  custom->bltafwm = -1;
  custom->bltalwm = -1;
  custom->bltadat = 0x8000;
  custom->bltbdat = 0xffff;
  custom->bltcmod = WIDTH / 8;
  custom->bltdmod = WIDTH / 8;

  /* Draw wide stripes. */
  xo &= TILESIZE - 1;

  line = &first;
  k = SIZE + 1;

  xo += (k - 1) * TILESIZE;
  kxo += k - 1;

  /* Draw from right to left. */ 
  for (; --k >= 0; xo -= TILESIZE, kxo--) {
    LineDataT *left, *right;

    {
      WORD xi = xo + 8 - TILESIZE;
      if (xi < 0)
        xi = 0;
      left = &vert[xi & (N - 1)];
    }

    {
      WORD xi = xo - 8;
      if (xi > N - 1)
        xi = N - 1;
      right = &vert[xi & (N - 1)];
    }
    
    {
      WORD i = DEPTH;
      WORD c = mod16(kxo, 7) + 1;

      while (--i >= 0) {
        APTR plane = window0->planes[i];

        if (c & (1 << i)) {
          DrawLine(plane, left->x1, FAR_Y, left->x2, left->y2);
          DrawLine(plane, right->x1, FAR_Y, right->x2, right->y2);

          if (right->x2 == WIDTH - 1)
            DrawLine(plane, WIDTH - 1, left->y2, WIDTH - 1, right->y2);
        }
      }
    }
  }
}

static __regargs void FillStripes(UWORD plane) {
  APTR bltpt = window0->planes[plane] + (HEIGHT * WIDTH) / 8 - 2;
  UWORD bltsize = ((HEIGHT - FAR_Y - 1) << 6) | (WIDTH >> 4);

  WaitBlitter();

  custom->bltapt = bltpt;
  custom->bltdpt = bltpt;
  custom->bltamod = 0;
  custom->bltdmod = 0;
  custom->bltcon0 = (SRCA | DEST) | A_TO_D;
  custom->bltcon1 = BLITREVERSE | FILL_OR;
  custom->bltafwm = -1;
  custom->bltalwm = -1;
  custom->bltsize = bltsize;
}

void ControlTileColors() {
  UWORD *src = texture->pixels, *dst = tileColor;
  WORD *energy = tileEnergy;
  WORD *cycle = tileCycle;
  WORD n = SIZE * SIZE ;

  while (--n >= 0) {
    WORD f = (SIN(frameCount * 16 + *cycle++) >> 10) - 4;
    WORD e = *energy;
    UWORD c = *src++;
    WORD r, g, b;

    if (e > 0)
      *energy = e - 2;
    energy++;

    f += e;
    if (f > 15)
      f = 15;

    if (f < 0) {
      f = 16 + f;
      /* 000 <-> RGB */
      r = ((c >> 4) & 0x0f0) | f;
      g = (c & 0x0f0) | f;
      b = ((c << 4) & 0x0f0) | f;
    } else {
      /* RGB -> FFF */
      r = (c & 0xf00) | 0x0f0 | f;
      g = ((c << 4) & 0xf00) | 0x0f0 | f;
      b = ((c << 8) & 0xf00) | 0x0f0 | f;
    }

    *dst++ = (colortab[r] << 4) | (UBYTE)(colortab[g] | (colortab[b] >> 4));
  }
}

static __regargs void ColorizeUpperHalf(CopListT *cp, WORD yi, WORD kyo) {
  WORD k;
  WORD y0 = HEIGHT;
  APTR pixels = tileColor;

  yi += (SIZE - 1) * TILESIZE - GAP;

  for (k = SIZE; k >= 0; k--, yi -= TILESIZE) {
    WORD column = ((k + kyo) & (SIZE - 1));
    UWORD *colors = pixels + (column * SIZE + 1) * sizeof(UWORD);

    CopWait(cp, Y(HEIGHT - y0), 0);
    CopSetRGB(cp, 1, *colors++);
    CopSetRGB(cp, 2, *colors++);
    CopSetRGB(cp, 3, *colors++);
    CopSetRGB(cp, 4, *colors++);
    CopSetRGB(cp, 5, *colors++);
    CopSetRGB(cp, 6, *colors++);
    CopSetRGB(cp, 7, *colors++);

    y0 = (yi > 0) ? horiz[yi] : FAR_Y;

    {
      WORD yj = yi + GAP;
      WORD y1;

      if (yj < 0)
        yj = 0;
      y1 = horiz[yj];

      CopWait(cp, Y(HEIGHT - y1), 0);
      CopSetRGB(cp, 1, BGCOL);
      CopSetRGB(cp, 2, BGCOL);
      CopSetRGB(cp, 3, BGCOL);
      CopSetRGB(cp, 4, BGCOL);
      CopSetRGB(cp, 5, BGCOL);
      CopSetRGB(cp, 6, BGCOL);
      CopSetRGB(cp, 7, BGCOL);
    }
  }
}

static __regargs void ColorizeLowerHalf(CopListT *cp, WORD yi, WORD kyo) {
  WORD k;
  WORD y0 = FAR_Y;
  APTR pixels = tileColor;

  for (k = 0; k <= SIZE; k++, yi += TILESIZE) {
    WORD column = ((k + kyo) & (SIZE - 1));
    UWORD *colors = pixels + (column * SIZE + 1) * sizeof(UWORD);
    CopWait(cp, Y(y0), 0);
    CopSetRGB(cp, 1, *colors++);
    CopSetRGB(cp, 2, *colors++);
    CopSetRGB(cp, 3, *colors++);
    CopSetRGB(cp, 4, *colors++);
    CopSetRGB(cp, 5, *colors++);
    CopSetRGB(cp, 6, *colors++);
    CopSetRGB(cp, 7, *colors++);

    y0 = (yi < N) ? horiz[yi] : HEIGHT;

    {
      WORD yj = yi - GAP;
      WORD y1;

      if (yj < 0)
        yj = 0;
      y1 = horiz[yj];

      CopWait(cp, Y(y1), 0);
      CopSetRGB(cp, 1, BGCOL);
      CopSetRGB(cp, 2, BGCOL);
      CopSetRGB(cp, 3, BGCOL);
      CopSetRGB(cp, 4, BGCOL);
      CopSetRGB(cp, 5, BGCOL);
      CopSetRGB(cp, 6, BGCOL);
      CopSetRGB(cp, 7, BGCOL);
    }
  }
}

static void MakeFloorCopperList(WORD yo, WORD kyo, BOOL active) {
  CopListT *cp = cp0;

  CopInit(cp);
  CopSetupGfxSimple(cp, MODE_LORES, DEPTH, X(0), Y(0), WIDTH, HEIGHT);
  {
    APTR *planes = window0->planes;
    CopMove32(cp, bplpt[0], (*planes++) + WIDTH * (HEIGHT - 1) / 8);
    CopMove32(cp, bplpt[1], (*planes++) + WIDTH * (HEIGHT - 1) / 8);
    CopMove32(cp, bplpt[2], (*planes++) + WIDTH * (HEIGHT - 1) / 8);
  }
  CopMove16(cp, bpl1mod, - (WIDTH * 2) / 8);
  CopMove16(cp, bpl2mod, - (WIDTH * 2) / 8);

  CopSetupSprites(cp, NULL);
 
  {
    WORD i = mod16(frameCount, 10) * 2;
    UWORD *thunder0 = thunder[i]->data;
    UWORD *thunder1 = thunder[i+1]->data;
    UWORD *null = NullSprite->data;

    if (active) {
      CopMove32(cp, sprpt[0], thunder0);
      CopMove32(cp, sprpt[1], thunder1);
    } else {
      CopMove32(cp, sprpt[0], null);
      CopMove32(cp, sprpt[1], null);
    }

    CopMove32(cp, sprpt[2], null);
    CopMove32(cp, sprpt[3], null);
    CopMove32(cp, sprpt[4], null);
    CopMove32(cp, sprpt[5], null);
    CopMove32(cp, sprpt[6], null);
    CopMove32(cp, sprpt[7], null);
  }

  /* Clear out the colors. */
  CopSetRGB(cp, 0, BGCOL);
  CopLoadPal(cp, palette, 16);

  FillStripes(1);
  ColorizeUpperHalf(cp, yo, kyo);

  CopWait(cp, Y(HEIGHT / 2), 0);
  CopMove16(cp, bpl1mod, 0);
  CopMove16(cp, bpl2mod, 0);

  FillStripes(2);
  ColorizeLowerHalf(cp, yo, kyo);

  CopEnd(cp);
}

static void Render() {
  // PROFILE_BEGIN(floor);

  BitmapClearArea(window0, (&(Area2D){0, FAR_Y, WIDTH, HEIGHT - FAR_Y}));

  {
    WORD xo = (N / 4) + normfx(SIN(frameCount * 20) * N / 4);
    WORD yo = (N / 4) + normfx(COS(frameCount * 10) * N / 4);
    WORD kxo = 6 - xo * SIZE / N; // left => larger values, right => smaller values
    WORD kyo = 7 - yo * SIZE / N; // to horizon => larger values, to observer => smaller values
    WORD active = TrackValueGet(thunders_event, frameCount);

    kxo = mod16(kxo, 7);
    if (kxo < 0)
      kxo += 7;

    kyo = kyo & 7;

    if (active > 0) {
      if (active == 1)
        thunders_active = TRUE;
      if (active == 2)
        thunders_active = FALSE;
    }

    if (thunders_active) {
      WORD cxo = mod16(kxo - 3, 7);
      WORD cyo = (kyo - 3) & 7;

      if (cxo < 0)
        cxo += 7;

      tileEnergy[cyo * 8 + cxo + 1] = 32;
    }

    DrawStripes(xo, kxo);
    FillStripes(0);
    ControlTileColors();
    MakeFloorCopperList(yo & (TILESIZE - 1), kyo, thunders_active);
  }

  WaitBlitter();

  // PROFILE_END(floor);

  CopListRun(cp0);
  WaitVBlank();
  { CopListT *tmp = cp0; cp0 = cp1; cp1 = tmp; }
  { BitmapT *tmp = window0; window0 = window1; window1 = tmp; }
}

EFFECT(Thunders, Load, UnLoad, Init, Kill, Render, Prepare);
