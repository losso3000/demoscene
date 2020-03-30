#include "color.h"
#include "common.h"

u_char colortab[4096];

void InitColorTab(void) {
  short i, j, k;
  u_char *ptr = colortab;

  Log("[Init] Color table.\n");

  for (i = 0; i < 16; i++) {
    for (j = 0; j < 16; j++) {
      short l = j - i;
      int r;
      for (k = 0, r = 0; k < 16; k++, r += l)
        *ptr++ = (i + div16(r, 15)) << 4;
    }
  }
}

/* Each argument must be in range 0-15. */
__regargs u_short ColorTransition(u_short from, u_short to, u_short step) {
  short r = (from & 0xf00) | ((to >> 4) & 0x0f0) | step;
  short g = ((from << 4) & 0xf00) | (to & 0x0f0) | step;
  short b = ((from << 8) & 0xf00) | ((to << 4) & 0x0f0) | step;
  
  return (colortab[r] << 4) | colortab[g] | (colortab[b] >> 4);
}

u_short ColorTransitionRGB(short sr, short sg, short sb, short dr, short dg, short db, u_short step) {
  short r = ((sr & 0xf0) << 4) | (dr & 0xf0) | step;
  short g = ((sg & 0xf0) << 4) | (dg & 0xf0) | step;
  short b = ((sb & 0xf0) << 4) | (db & 0xf0) | step;
  
  return (colortab[r] << 4) | colortab[g] | (colortab[b] >> 4);
}

__regargs u_short ColorIncreaseContrastRGB(short r, short g, short b, u_short step) {
  r &= 0xf0;
  r += colortab[(short)(r | step)];
  if (r > 0xf0) r = 0xf0;

  g &= 0xf0;
  g += colortab[(short)(g | step)];
  if (g > 0xf0) g = 0xf0;

  b &= 0xf0;
  b += colortab[(short)(b | step)];
  if (b > 0xf0) b = 0xf0;

  return (r << 4) | g | (b >> 4);
}

__regargs u_short ColorDecreaseContrastRGB(short r, short g, short b, u_short step) {
  r &= 0xf0;
  r -= colortab[(short)(r | step)];
  if (r < 0) r = 0;

  g &= 0xf0;
  g -= colortab[(short)(g | step)];
  if (g < 0) g = 0;

  b &= 0xf0;
  b -= colortab[(short)(b | step)];
  if (b < 0) b = 0;

  return (r << 4) | g | (b >> 4);
}

ADD2INIT(InitColorTab, 0);