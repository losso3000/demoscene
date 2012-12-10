#include "hardware.h"

volatile struct Custom* const custom = (APTR)0xdff000;
volatile struct CIA* const ciaa = (APTR)0xbfe001;
volatile struct CIA* const ciab = (APTR)0xbfd000;

void WaitMouse() {
  while (ciaa->ciapra & CIAF_GAMEPORT0);
}

void WaitBlitter() {
  while (custom->dmaconr & DMAF_BLTDONE);
}

void WaitVBlank() {
  for (;;) {
    ULONG vpos = (*(volatile ULONG *)&custom->vposr) >> 8;

    if ((vpos & 0x1ff) == 312)
      break;
  }
}
