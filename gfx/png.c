#include <stdio.h>
#include <dos/dos.h>
#include <proto/dos.h>

#include "std/debug.h"
#include "std/memory.h"
#include "gfx/png.h"
#include "tinf/tinf.h"

#define PNG_ID0 MAKE_ID(0x89, 0x50, 0x4e, 0x47)
#define PNG_ID1 MAKE_ID(0x0d, 0x0a, 0x1a, 0x0a)

#define PNG_IEND MAKE_ID('I', 'E', 'N', 'D')
#define PNG_IHDR MAKE_ID('I', 'H', 'D', 'R')
#define PNG_IDAT MAKE_ID('I', 'D', 'A', 'T')
#define PNG_PLTE MAKE_ID('P', 'L', 'T', 'E')
#define PNG_tRNS MAKE_ID('t', 'R', 'N', 'S')

static inline int GetPixelWidth(PngT *png) {
  int pixelWidth = 1;

  if (png->ihdr.colour_type == PNG_TRUECOLOR)
    pixelWidth = 3;
  else if (png->ihdr.colour_type == PNG_GRAYSCALE_ALPHA)
    pixelWidth = 2;
  else if (png->ihdr.colour_type == PNG_TRUECOLOR_ALPHA)
    pixelWidth = 4;

  return pixelWidth;
}

static inline int PaethPredictor(uint8_t a, uint8_t b, uint8_t c) {
  int p = a + b - c;
  int pa = abs(p - a);
  int pb = abs(p - b);
  int pc = abs(p - c);

  if (pa <= pb && pa <= pc)
    return a;

  if (pb <= pc)
   return b;

  return c;
}

static void ReconstructImage(uint8_t *pixels, uint8_t *encoded,
                             int width, int height, int pixelWidth)
{
  int row = width * pixelWidth;
  int i, j, k;

  for (i = 0; i < height; i++) {
    uint8_t method = *encoded++;

    for (k = 0; k < pixelWidth; k++) {
      pixels[0] = encoded[0];
      if (method == 2 || method == 4) /* Up & Paeth */
        pixels[0] += pixels[-row];
      else if (method == 3) /* Average */
        pixels[0] += pixels[-row] / 2;

      for (j = pixelWidth; j < row; j += pixelWidth) {
        pixels[j] = encoded[j];
        if (method == 1) /* Sub */
          pixels[j] += pixels[j - pixelWidth];
        else if (method == 2) /* Up */
          pixels[j] += pixels[j - row];
        else if (method == 3) /* Average */
          pixels[j] += (pixels[j - pixelWidth] + pixels[j - row]) / 2;
        else if (method == 4) /* Paeth */
          pixels[j] += PaethPredictor(pixels[j - pixelWidth],
                                      pixels[j - row],
                                      pixels[j - row - pixelWidth]);
      }

      pixels++;
      encoded++;
    }

    pixels += row - pixelWidth;
    encoded += row - pixelWidth;
  }
}

/* Collapse multiple IDAT chunks into single one. */
static void MergeIDATs(PngT *png) {
  if (png->idat.next) {
    uint32_t length, i;
    uint8_t *data;
    IdatT *idat;

    for (idat = &png->idat, length = 0; idat; idat = idat->next)
      length += idat->length;

    LOG("Merged chunk length: %ld.", length);

    data = MemNew(length);

    for (idat = &png->idat, i = 0; idat;) {
      IdatT *next = idat->next;

      MemCopy(data + i, idat->data, idat->length);
      i += idat->length;

      MemUnref(idat->data);
      if (idat != &png->idat)
        MemUnref(idat);

      idat = next;
    }

    png->idat.data = data;
    png->idat.length = length;
    png->idat.next = NULL;
  }
}

uint8_t *PngDecodeImage(PngT *png) {
  if (png->ihdr.interlace_method != 0) {
    LOG("Interlaced PNG not supported.");
  } else if (png->ihdr.bit_depth != 8) {
    LOG("Non 8-bit components not supported.");
  } else {
    unsigned int pixelWidth = GetPixelWidth(png);
    unsigned int length = png->ihdr.width * png->ihdr.height * pixelWidth;
    unsigned int dstLength = length + png->ihdr.height;
    uint8_t *pixels = MemNew(length);
    uint8_t *encoded = MemNew(dstLength);

    MergeIDATs(png);

    LOG("Uncompressing the image.");

    ASSERT(tinf_uncompress(encoded, &dstLength,
                           png->idat.data + 2, png->idat.length - 2) == TINF_OK,
           "Decompression failed.");
    ASSERT(length + png->ihdr.height == dstLength, "Decompressed data size differs.");

    LOG("Decoding pixels.");

    ReconstructImage(pixels, encoded, png->ihdr.width, png->ihdr.height, pixelWidth);

    MemUnref(encoded);

    return pixels;
  }

  return NULL;
}

typedef struct {
  uint32_t length;
  uint32_t id;
} PngChunkT;

static bool ReadPNG(PngT *png, int fd) {
  uint32_t id[2];
  bool error = false;
  PngChunkT chunk;

  memset(png, 0, sizeof(PngT));

  if (Read(fd, id, 8) != 8)
    return false;

  if (id[0] != PNG_ID0 || id[1] != PNG_ID1)
    return false;

  memset(&chunk, 0, sizeof(chunk));

  while (chunk.id != PNG_IEND && !error) {
    unsigned int their_crc, my_crc;
    unsigned char *ptr;

    if (Read(fd, &chunk, 8) != 8)
      return false;

    my_crc = tinf_crc32(0, (void *)&chunk.id, 4);

    ptr = MemNew(chunk.length);

    if (Read(fd, ptr, chunk.length) != chunk.length)
      return false;

    my_crc = tinf_crc32(my_crc, ptr, chunk.length);

    if (chunk.id == PNG_IHDR) {
      MemCopy(&png->ihdr, ptr, sizeof(IhdrT));
    } else if (chunk.id == PNG_IDAT) {
      if (!png->idat.data) {
        png->idat.length = chunk.length;
        png->idat.data = ptr;
      } else {
        IdatT *idat = &png->idat;

        while (idat->next)
          idat = idat->next;

        idat->next = NewRecord(IdatT);
        idat->next->length = chunk.length;
        idat->next->data = ptr;
      }
      ptr = NULL;
    } else if (chunk.id == PNG_PLTE) {
      png->plte.no_colors = chunk.length / 3;
      png->plte.colors = (RGB *)ptr;
      ptr = NULL;
    } else if (chunk.id == PNG_tRNS) {
      if (png->ihdr.colour_type == PNG_INDEXED) {
        png->trns = MemNew(sizeof(uint32_t) + chunk.length);
        png->trns->type3.length = chunk.length;
        MemCopy(png->trns->type3.alpha, ptr, chunk.length);
      } else {
        png->trns = (TrnsT *)ptr;
        ptr = NULL;
      }
    }

    if (ptr)
      MemUnref(ptr);

    if (Read(fd, &their_crc, 4) != 4)
      return false;

    LOG("%.4s: length: %ld, crc: %s",
        (char *)&chunk.id, chunk.length, their_crc == my_crc ? "ok" : "bad");

    if (their_crc != my_crc)
      return false;
  }

  return !error;
}

static void DeletePng(PngT *png) {
  IdatT *idat = &png->idat;

  do {
    IdatT *next = idat->next;

    MemUnref(idat->data);
    if (idat != &png->idat)
      MemUnref(idat);

    idat = next;
  } while (idat);

  if (png->plte.colors)
    MemUnref(png->plte.colors);

  if (png->trns)
    MemUnref(png->trns);
}

TYPEDECL(PngT, (FreeFuncT)DeletePng);

PngT *PngLoadFromFile(const char *path) {
  BPTR fd;

  if ((fd = Open(path, MODE_OLDFILE))) {
    PngT *png = NewInstance(PngT);

    if (ReadPNG(png, fd)) {
      LOG("Loaded '%s' file.", path);
    } else {
      MemUnref(png);
      png = NULL;
    }

    Close(fd);
    return png;
  }

  return NULL;
}

#if 0
PixBufT *PngToPixBuf(PngT *png) {
}

PaletteT *PngToPalette(PngT *png) {
}
#endif