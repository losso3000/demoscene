#include <proto/graphics.h>

#include "console.h"
#include "hardware.h"
#include "interrupts.h"
#include "coplist.h"
#include "keyboard.h"

static BitmapT *screen;
static CopListT *cp;
static TextFontT *topaz8;
static ConsoleT console;

void Load() {
  screen = NewBitmap(640, 256, 1, FALSE);
  cp = NewCopList(100);

  {
    struct TextAttr textattr = { "topaz.font", 8, FS_NORMAL, FPF_ROMFONT };
    topaz8 = OpenFont(&textattr);
  }

  ConsoleInit(&console, screen, topaz8);
}

void Kill() {
  CloseFont(topaz8);
  DeleteCopList(cp);
  DeleteBitmap(screen);
}

__interrupt_handler void IntLevel2Handler() {
  if (custom->intreqr & INTF_PORTS)
    KeyboardIntHandler();

  custom->intreq = INTF_PORTS;
}

void Main() {
  APTR OldIntLevel2;

  CopInit(cp);
  CopMakePlayfield(cp, screen);
  CopMakeDispWin(cp, 0x81, 0x2c, screen->width / 2, screen->height);
  CopSetRGB(cp, 0, 0x000);
  CopSetRGB(cp, 1, 0xfff);
  CopEnd(cp);

  CopListActivate(cp);

  custom->dmacon = DMAF_SETCLR | DMAF_RASTER | DMAF_MASTER;

  /* CIA interrupt initialization. */
  OldIntLevel2 = InterruptVector->IntLevel2;
  InterruptVector->IntLevel2 = IntLevel2Handler;

  custom->intena = INTF_SETCLR | INTF_PORTS | INTF_INTEN;

  /* Disable all CIA-A interrupts. */
  ciaa->ciaicr = (UBYTE)(~CIAICRF_SETCLR);
  /* Enable keyboard interrupt.
   * The keyboard is attached to CIA-A serial port. */
  ciaa->ciaicr = CIAICRF_SETCLR | CIAICRF_SP;

  ConsolePutStr(&console, "Press ESC key to exit!\n");

  while (1) {
    if (KeyCode == KEY_ESCAPE)
      break;

    if (KeyChar && !(KeyMod & MOD_PRESSED)) {
      ConsolePutChar(&console, KeyChar);
      KeyChar = 0;
    }
  }

  /* CIA interrupt release. */
  custom->intena = INTF_PORTS;
  InterruptVector->IntLevel2 = OldIntLevel2;
}