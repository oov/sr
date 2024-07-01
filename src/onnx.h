#pragma once

#ifdef __GNUC__
#  ifndef __has_warning
#    define __has_warning(x) 0
#  endif
#  pragma GCC diagnostic push
#  if __has_warning("-Wreserved-identifier")
#    pragma GCC diagnostic ignored "-Wreserved-identifier"
#  endif
#  if __has_warning("-Wunknown-pragmas")
#    pragma GCC diagnostic ignored "-Wunknown-pragmas"
#  endif
#  define _Frees_ptr_opt_
#endif // __GNUC__

#include <dml_provider_factory.h>
#include <onnxruntime_c_api.h>

#ifdef __GNUC__
#  pragma GCC diagnostic pop
#endif // __GNUC__

#ifdef _WIN32
#  include <dxgi.h>
OrtStatus *enum_dml_devices(bool (*callback)(UINT const device_id, DXGI_ADAPTER_DESC const *const desc, void *const userdata),
                            void *const userdata);
#endif // _WIN32

extern OrtApi const *g_ort;

bool print_dimensions(OrtTensorTypeAndShapeInfo const *const type_info);
void print_names(OrtSession *session);
void print_tensor_dimensions(OrtValue const *const tensor);
