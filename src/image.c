#include "image.h"

#include <stdio.h>

#ifdef __GNUC__
#  ifndef __has_warning
#    define __has_warning(x) 0
#  endif
#  pragma GCC diagnostic push
#  if __has_warning("-Wcast-qual")
#    pragma GCC diagnostic ignored "-Wcast-qual"
#  endif
#  if __has_warning("-Wcast-align")
#    pragma GCC diagnostic ignored "-Wcast-align"
#  endif
#  if __has_warning("-Wdouble-promotion")
#    pragma GCC diagnostic ignored "-Wdouble-promotion"
#  endif
#  if __has_warning("-Wsign-conversion")
#    pragma GCC diagnostic ignored "-Wsign-conversion"
#  endif
#  if __has_warning("-Wimplicit-int-conversion")
#    pragma GCC diagnostic ignored "-Wimplicit-int-conversion"
#  endif
#  if __has_warning("-Wimplicit-fallthrough")
#    pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#  endif
#  if __has_warning("-Wdisabled-macro-expansion")
#    pragma GCC diagnostic ignored "-Wdisabled-macro-expansion"
#  endif
#  if __has_warning("-Wextra-semi-stmt")
#    pragma GCC diagnostic ignored "-Wextra-semi-stmt"
#  endif
#  if __has_warning("-Wreserved-macro-identifier")
#    pragma GCC diagnostic ignored "-Wreserved-macro-identifier"
#  endif
#  if __has_warning("-Wmissing-prototypes")
#    pragma GCC diagnostic ignored "-Wmissing-prototypes"
#  endif
#  if __has_warning("-Wcomma")
#    pragma GCC diagnostic ignored "-Wcomma"
#  endif
#  if __has_warning("-Wcovered-switch-default")
#    pragma GCC diagnostic ignored "-Wcovered-switch-default"
#  endif
#  if __has_warning("-Wtautological-value-range-compare")
#    pragma GCC diagnostic ignored "-Wtautological-value-range-compare"
#  endif
#  if __has_warning("-Wshorten-64-to-32")
#    pragma GCC diagnostic ignored "-Wshorten-64-to-32"
#  endif
#  if __has_warning("-Wimplicit-float-conversion")
#    pragma GCC diagnostic ignored "-Wimplicit-float-conversion"
#  endif
#  if __has_warning("-Wfloat-conversion")
#    pragma GCC diagnostic ignored "-Wfloat-conversion"
#  endif
#endif // __GNUC__

#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "miniz.c"
#include "miniz.h"
#include "spng.c"
#include "spng.h"

#ifdef __GNUC__
#  pragma GCC diagnostic pop
#endif // __GNUC__

static int file_read(void *user, char *data, int size) { return (int)(fread(data, 1, (size_t)size, (FILE *)user)); }
static void file_skip(void *user, int n) { fseek((FILE *)user, n, SEEK_CUR); }
static int file_eof(void *user) { return feof((FILE *)user); }

uint8_t *image_load(SR_CHAR_T const *const path, size_t *const width, size_t *const height) {
  uint8_t *data = NULL;
#ifdef _WIN32
  FILE *f = _wfopen(path, L"rb");
#else
  FILE *f = fopen(path, "rb");
#endif
  if (!f) {
    goto cleanup;
  }
  int w, h;
  data = stbi_load_from_callbacks(
      &(stbi_io_callbacks){
          .read = file_read,
          .skip = file_skip,
          .eof = file_eof,
      },
      f,
      &w,
      &h,
      NULL,
      4);
  if (data) {
    *width = (size_t)w;
    *height = (size_t)h;
  }
cleanup:
  if (f) {
    fclose(f);
    f = NULL;
  }
  return data;
}

void image_free(uint8_t *data) {
  if (data) {
    stbi_image_free(data);
  }
}

struct spng_context {
  stbi_write_func *func;
  void *context;
};

static int file_write_spng(spng_ctx *ctx, void *user, void *dst_src, size_t length) {
  (void)ctx;
  struct spng_context *const spctx = (struct spng_context *)user;
  spctx->func(spctx->context, dst_src, (int)length);
  return 0;
}

static bool image_save_spng(stbi_write_func *func, void *context, uint8_t const *const data, size_t const width, size_t const height) {
  bool r = false;
  struct spng_context spctx = {.func = func, .context = context};
  spng_ctx *ctx = spng_ctx_new(SPNG_CTX_ENCODER);
  spng_set_png_stream(ctx, file_write_spng, &spctx);
  spng_set_ihdr(ctx,
                &(struct spng_ihdr){
                    .width = (uint32_t)width,
                    .height = (uint32_t)height,
                    .color_type = SPNG_COLOR_TYPE_TRUECOLOR_ALPHA,
                    .bit_depth = 8,
                });
  r = spng_encode_image(ctx, data, width * 4 * height, SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE) == 0;
  spng_ctx_free(ctx);
  return r;
}

static void file_write(void *context, void *data, int size) { fwrite(data, 1, (size_t)size, (FILE *)context); }

static SR_CHAR_T inline to_lower(SR_CHAR_T const c) {
  if (c >= SR_TSTR('A') && c <= SR_TSTR('Z')) {
    return c + SR_TSTR('a') - SR_TSTR('A');
  }
  return c;
}

static bool match(SR_CHAR_T const *const s, SR_CHAR_T const *const lower_str) {
  if (!s) {
    return false;
  }
  size_t const slen = SR_STRLEN(s);
  if (slen != SR_STRLEN(lower_str)) {
    return false;
  }
  for (size_t i = 0; i < slen; ++i) {
    if (to_lower(s[i]) != lower_str[i]) {
      return false;
    }
  }
  return true;
}

bool image_save(SR_CHAR_T const *const path, uint8_t const *const data, size_t const width, size_t const height) {
  enum {
    png,
    jpg,
    bmp,
    tga,
  } type;
  SR_CHAR_T const *const ext = SR_STRRCHR(path, SR_TSTR('.'));
  if (!ext || match(ext, SR_TSTR(".png"))) {
    type = png;
  } else if (match(ext, SR_TSTR(".jpg")) || match(ext, SR_TSTR(".jpeg")) || match(ext, SR_TSTR(".jfif"))) {
    type = jpg;
  } else if (match(ext, SR_TSTR(".bmp"))) {
    type = bmp;
  } else if (match(ext, SR_TSTR(".tga"))) {
    type = tga;
  } else {
    return false;
  }

  bool r = false;
#ifdef _WIN32
  FILE *f = _wfopen(path, L"wb");
#else
  FILE *f = fopen(path, "wb");
#endif
  if (!f) {
    goto cleanup;
  }

  switch (type) {
  case png:
    r = image_save_spng(file_write, f, data, width, height);
    break;
  case jpg:
    r = stbi_write_jpg_to_func(file_write, f, (int)width, (int)height, 4, data, 100) != 0;
    break;
  case bmp:
    r = stbi_write_bmp_to_func(file_write, f, (int)width, (int)height, 4, data) != 0;
    break;
  case tga:
    r = stbi_write_tga_to_func(file_write, f, (int)width, (int)height, 4, data) != 0;
    break;
  }

cleanup:
  if (f) {
    fclose(f);
    f = NULL;
  }
  return r;
}

void image_nn4x(uint8_t const *const source, size_t const width, size_t const height, uint8_t *const destination) {
  size_t const width4 = width * 4;
  size_t const width44 = width * 4 * 4;
  uint8_t const *s = source;
  uint8_t *d = destination;
  uint8_t p[4];
  for (size_t y = 0; y < height; ++y) {
    for (size_t x = 0; x < width4; x += 4) {
      size_t const si = x;
      size_t const di = x * 4;
      memcpy(p, &s[si], 4);
      memcpy(&d[di], p, 4);
      memcpy(&d[di + 4], p, 4);
      memcpy(&d[di + 8], p, 4);
      memcpy(&d[di + 12], p, 4);
    }
    d += width44;
    memcpy(d, d - width44, width44);
    d += width44;
    memcpy(d, d - width44, width44);
    d += width44;
    memcpy(d, d - width44, width44);
    d += width44;
    s += width4;
  }
}

// https://stackoverflow.com/a/60047308
static uint32_t inline as_uint32(float const x) { return *(uint32_t const *)&x; }
static float inline as_float(uint32_t const x) { return *(float const *)&x; }
static float half_to_float(uint16_t const x) {     // IEEE-754 16-bit floating-point format (without infinity): 1-5-10, exp-15, +-131008.0,
                                                   // +-6.1035156E-5, +-5.9604645E-8, 3.311 digits
  uint32_t const e = (uint32_t)(x & 0x7C00) >> 10; // exponent
  uint32_t const m = (uint32_t)(x & 0x03FF) << 13; // mantissa
  uint32_t const v = as_uint32((float)m) >> 23;    // evil log2 bit hack to count leading zeros in denormalized format
  return as_float((uint32_t)(x & 0x8000) << 16 | (e != 0) * ((e + 112) << 23 | m) |
                  ((e == 0) & (m != 0)) * ((v - 37) << 23 | ((m << (150 - v)) & 0x007FE000))); // sign : normalized : denormalized
}
static uint16_t float_to_half(float const x) {  // IEEE-754 16-bit floating-point format (without infinity): 1-5-10, exp-15, +-131008.0,
                                                // +-6.1035156E-5, +-5.9604645E-8, 3.311 digits
  uint32_t const b = as_uint32(x) + 0x00001000; // round-to-nearest-even: add last bit after truncated mantissa
  uint32_t const e = (b & 0x7F800000) >> 23;    // exponent
  uint32_t const m =
      b & 0x007FFFFF; // mantissa; in line below: 0x007FF000 = 0x00800000-0x00001000 = decimal indicator flag - initial rounding
  return (uint16_t)((b & 0x80000000) >> 16 | (e > 112) * ((((e - 112) << 10) & 0x7C00) | m >> 13) |
                    ((e < 113) & (e > 101)) * ((((0x007FF000 + m) >> (125 - e)) + 1) >> 1) |
                    (e > 143) * 0x7FFF); // sign : normalized : denormalized : saturate
}

static float inline u8tof32(uint8_t const x) {
  static float const divider = 1.f / 255.f;
  return (float)(x)*divider;
}

void hwc_to_chw16(uint8_t const *const source,
                  size_t const sw,
                  size_t const sh,
                  size_t const sx,
                  size_t const sy,
                  size_t const tile_size,
                  uint16_t *const pixels,
                  uint16_t *const pixels_alpha) {
  size_t const plane = tile_size * tile_size;
  size_t const w = (sx + tile_size < sw) ? tile_size : sw - sx;
  size_t const h = (sy + tile_size < sh) ? tile_size : sh - sy;
  for (size_t y = 0; y < h; ++y) {
    size_t const sl = (sy + y) * sw * 4;
    size_t const dl = y * tile_size;
    for (size_t x = 0; x < w; ++x) {
      size_t const si = sl + (sx + x) * 4;
      size_t const di = dl + x;
      uint16_t const r = float_to_half(u8tof32(source[si + 0]));
      uint16_t const g = float_to_half(u8tof32(source[si + 1]));
      uint16_t const b = float_to_half(u8tof32(source[si + 2]));
      uint16_t const a = float_to_half(u8tof32(source[si + 3]));
      pixels[di + 0 * plane] = r;
      pixels[di + 1 * plane] = g;
      pixels[di + 2 * plane] = b;
      pixels_alpha[di + 0 * plane] = a;
      pixels_alpha[di + 1 * plane] = a;
      pixels_alpha[di + 2 * plane] = a;
    }
  }
  // fill the rest with zeros
  if (w < tile_size || h < tile_size) {
    for (size_t y = h; y < tile_size; ++y) {
      size_t const dl = y * tile_size;
      for (size_t x = w; x < tile_size; ++x) {
        size_t const di = dl + x;
        pixels[di + 0 * plane] = 0.f;
        pixels[di + 1 * plane] = 0.f;
        pixels[di + 2 * plane] = 0.f;
        pixels_alpha[di + 0 * plane] = 0.f;
        pixels_alpha[di + 1 * plane] = 0.f;
        pixels_alpha[di + 2 * plane] = 0.f;
      }
    }
  }
}

void hwc_to_chw32(uint8_t const *const source,
                  size_t const sw,
                  size_t const sh,
                  size_t const sx,
                  size_t const sy,
                  size_t const tile_size,
                  float *const pixels,
                  float *const pixels_alpha) {
  size_t const plane = tile_size * tile_size;
  size_t const w = (sx + tile_size < sw) ? tile_size : sw - sx;
  size_t const h = (sy + tile_size < sh) ? tile_size : sh - sy;
  for (size_t y = 0; y < h; ++y) {
    size_t const sl = (sy + y) * sw * 4;
    size_t const dl = y * tile_size;
    for (size_t x = 0; x < w; ++x) {
      size_t const si = sl + (sx + x) * 4;
      size_t const di = dl + x;
      float const r = u8tof32(source[si + 0]);
      float const g = u8tof32(source[si + 1]);
      float const b = u8tof32(source[si + 2]);
      float const a = u8tof32(source[si + 3]);
      pixels[di + 0 * plane] = r;
      pixels[di + 1 * plane] = g;
      pixels[di + 2 * plane] = b;
      pixels_alpha[di + 0 * plane] = a;
      pixels_alpha[di + 1 * plane] = a;
      pixels_alpha[di + 2 * plane] = a;
    }
  }
  // fill the rest with zeros
  if (w < tile_size || h < tile_size) {
    for (size_t y = h; y < tile_size; ++y) {
      size_t const dl = y * tile_size;
      for (size_t x = w; x < tile_size; ++x) {
        size_t const di = dl + x;
        pixels[di + 0 * plane] = 0.f;
        pixels[di + 1 * plane] = 0.f;
        pixels[di + 2 * plane] = 0.f;
        pixels_alpha[di + 0 * plane] = 0.f;
        pixels_alpha[di + 1 * plane] = 0.f;
        pixels_alpha[di + 2 * plane] = 0.f;
      }
    }
  }
}

static float inline clamp255(float const f) {
  float const t = f < 0.f ? 0.f : f;
  return t > 255.f ? 255.f : t;
}

static uint8_t inline f32tou8(float const x) { return (uint8_t)(clamp255(x * 255.f + .5f)); }

static inline uint8_t muldiv255(uint8_t const a, uint8_t const b) { // (a * b) / 255
  uint_fast32_t const v = (uint_fast32_t)a * (uint_fast32_t)b + 128;
  return (uint8_t)((v + (v >> 8)) >> 8);
}

static uint8_t blend(uint8_t const a, uint8_t const b, uint8_t const alpha) { return muldiv255(a, 255 - alpha) + muldiv255(b, alpha); }
static inline size_t szmin(size_t const a, size_t const b) { return a < b ? a : b; }

void chw_to_hwc16(uint16_t const *const pixels,
                  uint16_t const *const pixels_alpha,
                  size_t const tile_size,
                  uint8_t *const dest,
                  size_t const dw,
                  size_t const dh,
                  size_t const dx,
                  size_t const dy,
                  size_t const overlap) {
  size_t const plane = tile_size * tile_size;
  size_t const w = (dx + tile_size < dw) ? tile_size : dw - dx;
  size_t const h = (dy + tile_size < dh) ? tile_size : dh - dy;
  for (size_t y = 0; y < h; ++y) {
    size_t const sl = y * tile_size;
    size_t const dl = (dy + y) * dw * 4;
    for (size_t x = 0; x < w; ++x) {
      size_t const si = sl + x;
      size_t const di = dl + (dx + x) * 4;
      bool const is_overlap = (dx && x < overlap) || (dy && y < overlap);
      if (is_overlap) {
        uint8_t b;
        if (!dx) {
          b = (uint8_t)((y * 255) / overlap);
        } else if (!dy) {
          b = (uint8_t)((x * 255) / overlap);
        } else {
          b = (uint8_t)((szmin(x, y) * 255) / overlap);
        }
        dest[di + 0] = blend(dest[di + 0], f32tou8(half_to_float(pixels[si + 0 * plane])), b);
        dest[di + 1] = blend(dest[di + 1], f32tou8(half_to_float(pixels[si + 1 * plane])), b);
        dest[di + 2] = blend(dest[di + 2], f32tou8(half_to_float(pixels[si + 2 * plane])), b);
        dest[di + 3] = blend(dest[di + 3], f32tou8(half_to_float(pixels_alpha[si + 0 * plane])), b);
      } else {
        dest[di + 0] = f32tou8(half_to_float(pixels[si + 0 * plane]));
        dest[di + 1] = f32tou8(half_to_float(pixels[si + 1 * plane]));
        dest[di + 2] = f32tou8(half_to_float(pixels[si + 2 * plane]));
        dest[di + 3] = f32tou8(half_to_float(pixels_alpha[si + 0 * plane]));
      }
    }
  }
}

void chw_to_hwc32(float const *const pixels,
                  float const *const pixels_alpha,
                  size_t const tile_size,
                  uint8_t *const dest,
                  size_t const dw,
                  size_t const dh,
                  size_t const dx,
                  size_t const dy,
                  size_t const overlap) {
  size_t const plane = tile_size * tile_size;
  size_t const w = (dx + tile_size < dw) ? tile_size : dw - dx;
  size_t const h = (dy + tile_size < dh) ? tile_size : dh - dy;
  for (size_t y = 0; y < h; ++y) {
    size_t const sl = y * tile_size;
    size_t const dl = (dy + y) * dw * 4;
    for (size_t x = 0; x < w; ++x) {
      size_t const si = sl + x;
      size_t const di = dl + (dx + x) * 4;
      bool const is_overlap = (dx && x < overlap) || (dy && y < overlap);
      uint8_t const r = f32tou8(pixels[si + 0 * plane]);
      uint8_t const g = f32tou8(pixels[si + 1 * plane]);
      uint8_t const b = f32tou8(pixels[si + 2 * plane]);
      uint8_t const a = f32tou8(pixels_alpha[si + 0 * plane]);
      if (is_overlap) {
        uint8_t bl;
        if (!dx) {
          bl = (uint8_t)((y * 255) / overlap);
        } else if (!dy) {
          bl = (uint8_t)((x * 255) / overlap);
        } else {
          bl = (uint8_t)((szmin(x, y) * 255) / overlap);
        }
        dest[di + 0] = blend(dest[di + 0], r, bl);
        dest[di + 1] = blend(dest[di + 1], g, bl);
        dest[di + 2] = blend(dest[di + 2], b, bl);
        dest[di + 3] = blend(dest[di + 3], a, bl);
      } else {
        dest[di + 0] = r;
        dest[di + 1] = g;
        dest[di + 2] = b;
        dest[di + 3] = a;
      }
    }
  }
}
