#pragma once

#include "common.h"

uint8_t *image_load(SR_CHAR_T const *const path, size_t *const width, size_t *const height);
void image_free(uint8_t *const data);
bool image_save(SR_CHAR_T const *const path, uint8_t const *const data, size_t const width, size_t const height);

void image_nn4x(uint8_t const *const source, size_t const width, size_t const height, uint8_t *const destination);

void hwc_to_chw16(uint8_t const *const source,
                  size_t const sw,
                  size_t const sh,
                  size_t const sx,
                  size_t const sy,
                  size_t const tile_size,
                  uint16_t *const pixels,
                  uint16_t *const pixels_alpha);
void hwc_to_chw32(uint8_t const *const source,
                  size_t const sw,
                  size_t const sh,
                  size_t const sx,
                  size_t const sy,
                  size_t const tile_size,
                  float *const pixels,
                  float *const pixels_alpha);

#define hwc_to_chw(source, sw, sh, sx, sy, tile_size, pixels, pixels_alpha)                                                                \
  _Generic((pixels), uint16_t *: hwc_to_chw16, float *: hwc_to_chw32)(source, sw, sh, sx, sy, tile_size, pixels, pixels_alpha)

void chw_to_hwc16(uint16_t const *const pixels,
                  uint16_t const *const pixels_alpha,
                  size_t const tile_size,
                  uint8_t *const dest,
                  size_t const dw,
                  size_t const dh,
                  size_t const dx,
                  size_t const dy,
                  size_t const overlap);
void chw_to_hwc32(float const *const pixels,
                  float const *const pixels_alpha,
                  size_t const tile_size,
                  uint8_t *const dest,
                  size_t const dw,
                  size_t const dh,
                  size_t const dx,
                  size_t const dy,
                  size_t const overlap);

#define chw_to_hwc(pixels, pixels_alpha, tile_size, dest, dw, dh, dx, dy, overlap)                                                         \
  _Generic((pixels), uint16_t const *: chw_to_hwc16, uint16_t *: chw_to_hwc16, float const *: chw_to_hwc32, float *: chw_to_hwc32)(        \
      pixels, pixels_alpha, tile_size, dest, dw, dh, dx, dy, overlap)
