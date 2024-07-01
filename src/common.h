#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#  include <wchar.h>
#  define SR_CHAR_T wchar_t
#  define SR_TSTR(X) L##X
#  define SR_STRLEN(s) wcslen(s)
#  define SR_STRCHR(s, c) wcschr(s, c)
#  define SR_STRRCHR(s, c) wcsrchr(s, c)
#else
#  include <string.h>
#  define SR_CHAR_T char
#  define SR_TSTR(X) X
#  define SR_STRLEN(s) strlen(s)
#  define SR_STRCHR(s, c) strchr(s, c)
#  define SR_STRRCHR(s, c) strrchr(s, c)
#endif

static inline size_t sr_append(SR_CHAR_T *const dst, SR_CHAR_T const *const src) {
  size_t const srclen = SR_STRLEN(src);
  memcpy(dst, src, srclen * sizeof(SR_CHAR_T));
  return srclen;
}

#define USE_HALF 0

#if USE_HALF
#  define TENSOR_TYPE ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16
#  define FLOAT_TYPE uint16_t
#else
#  define TENSOR_TYPE ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT
#  define FLOAT_TYPE float
#endif
