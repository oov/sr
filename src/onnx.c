#include "onnx.h"

#include "common.h"

#include <ovbase.h>
#include <stdio.h>

OrtApi const *g_ort = NULL;

static OrtStatus *HRESULT_to_OrtStatus(HRESULT const hr) {
  OrtStatus *st = NULL;
  char *msg = NULL;
  FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                 NULL,
                 (DWORD)hr,
                 MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
                 (LPSTR)&msg,
                 0,
                 NULL);
  st = g_ort->CreateStatus(ORT_FAIL, msg);
  LocalFree(msg);
  return st;
}

OrtStatus *enum_dml_devices(bool (*callback)(UINT const device_id, DXGI_ADAPTER_DESC const *const desc, void *const userdata),
                            void *const userdata) {
  OrtStatus *st = NULL;
  IDXGIFactory *factory = NULL;
  IDXGIAdapter *adapter = NULL;
  DXGI_ADAPTER_DESC desc;
  HRESULT hr = CreateDXGIFactory(&IID_IDXGIFactory, (void **)&factory);
  if (FAILED(hr)) {
    st = HRESULT_to_OrtStatus(hr);
    goto cleanup;
  }
  for (UINT idx = 0;; ++idx) {
    hr = factory->lpVtbl->EnumAdapters(factory, idx, &adapter);
    if (hr == DXGI_ERROR_NOT_FOUND) {
      break;
    }
    if (FAILED(hr)) {
      st = HRESULT_to_OrtStatus(hr);
      goto cleanup;
    }
    hr = adapter->lpVtbl->GetDesc(adapter, &desc);
    if (FAILED(hr)) {
      st = HRESULT_to_OrtStatus(hr);
      goto cleanup;
    }
    if (!callback(idx, &desc, userdata)) {
      break;
    }
    adapter->lpVtbl->Release(adapter);
    adapter = NULL;
  }
cleanup:
  if (adapter != NULL) {
    adapter->lpVtbl->Release(adapter);
  }
  if (factory != NULL) {
    factory->lpVtbl->Release(factory);
  }
  return st;
}

bool print_dimensions(OrtTensorTypeAndShapeInfo const *const type_info) {
  int64_t *dims = NULL;
  OrtStatus *st = NULL;
  bool r = false;

  size_t num_dims;
  st = g_ort->GetDimensionsCount(type_info, &num_dims);
  if (st != NULL) {
    fprintf(stderr, "failed to get dimensions count.\n");
    goto cleanup;
  }
  printf("num_dims: %zu\n", num_dims);
  dims = (int64_t *)malloc(num_dims * sizeof(int64_t));
  if (dims == NULL) {
    fprintf(stderr, "failed to allocate memory for tensor shape.\n");
    goto cleanup;
  }
  st = g_ort->GetDimensions(type_info, dims, num_dims);
  if (st != NULL) {
    fprintf(stderr, "failed to get tensor shape.\n");
    goto cleanup;
  }
  for (size_t i = 0; i < num_dims; ++i) {
    printf("  dim[%zu]: %lld\n", i, dims[i]);
  }
  r = true;
cleanup:
  if (st != NULL) {
    fprintf(stderr, "Error(%d): %s\n", g_ort->GetErrorCode(st), g_ort->GetErrorMessage(st));
    g_ort->ReleaseStatus(st);
  }
  if (dims != NULL) {
    free(dims);
  }
  return r;
}

void print_names(OrtSession *session) {
  OrtAllocator *allocator = NULL;
  OrtTypeInfo *type_info = NULL;
  OrtTensorTypeAndShapeInfo const *type_info2 = NULL;
  OrtStatus *st = NULL;

  st = g_ort->GetAllocatorWithDefaultOptions(&allocator);
  if (st != NULL) {
    fprintf(stderr, "failed to create allocator.\n");
    goto cleanup;
  }

  size_t num_input_nodes;
  st = g_ort->SessionGetInputCount(session, &num_input_nodes);
  if (st != NULL) {
    fprintf(stderr, "failed to get input count.\n");
    goto cleanup;
  }
  printf("num_input_nodes: %zu\n", num_input_nodes);

  for (size_t i = 0; i < num_input_nodes; ++i) {
    char *input_name;
    st = g_ort->SessionGetInputName(session, i, allocator, &input_name);
    if (st != NULL) {
      fprintf(stderr, "failed to get input name.\n");
      goto cleanup;
    }
    printf("  input #%zu: %s\n", i, input_name);
    st = g_ort->AllocatorFree(allocator, input_name);
    if (st != NULL) {
      fprintf(stderr, "failed to free input name.\n");
      goto cleanup;
    }
    input_name = NULL;
    st = g_ort->SessionGetInputTypeInfo(session, i, &type_info);
    if (st != NULL) {
      fprintf(stderr, "failed to get input type info.\n");
      goto cleanup;
    }
    st = g_ort->CastTypeInfoToTensorInfo(type_info, &type_info2);
    if (st != NULL) {
      fprintf(stderr, "failed to cast type info to tensor info.\n");
      goto cleanup;
    }
    print_dimensions(type_info2);
    g_ort->ReleaseTypeInfo(type_info);
    type_info = NULL;
  }

  size_t num_output_nodes;
  st = g_ort->SessionGetOutputCount(session, &num_output_nodes);
  if (st != NULL) {
    fprintf(stderr, "failed to get output count.\n");
    goto cleanup;
  }
  printf("num_output_nodes: %zu\n", num_output_nodes);

  for (size_t i = 0; i < num_output_nodes; ++i) {
    char *output_name;
    st = g_ort->SessionGetOutputName(session, i, allocator, &output_name);
    if (st != NULL) {
      fprintf(stderr, "failed to get output name.\n");
      goto cleanup;
    }
    printf("  output #%zu: %s\n", i, output_name);
    st = g_ort->AllocatorFree(allocator, output_name);
    if (st != NULL) {
      fprintf(stderr, "failed to free output name.\n");
      goto cleanup;
    }
    output_name = NULL;
    st = g_ort->SessionGetOutputTypeInfo(session, i, &type_info);
    if (st != NULL) {
      fprintf(stderr, "failed to get output type info.\n");
      goto cleanup;
    }
    st = g_ort->CastTypeInfoToTensorInfo(type_info, &type_info2);
    if (st != NULL) {
      fprintf(stderr, "failed to cast type info to tensor info.\n");
      goto cleanup;
    }
    print_dimensions(type_info2);
    g_ort->ReleaseTypeInfo(type_info);
    type_info = NULL;
  }

cleanup:
  if (st != NULL) {
    fprintf(stderr, "Error(%d): %s\n", g_ort->GetErrorCode(st), g_ort->GetErrorMessage(st));
    g_ort->ReleaseStatus(st);
  }
  if (type_info != NULL) {
    g_ort->ReleaseTypeInfo(type_info);
  }
}

void print_tensor_dimensions(OrtValue const *const tensor) {
  OrtTensorTypeAndShapeInfo *type_info = NULL;
  OrtStatus *st = NULL;

  st = g_ort->GetTensorTypeAndShape(tensor, &type_info);
  if (st != NULL) {
    fprintf(stderr, "failed to get tensor type and shape.\n");
    goto cleanup;
  }
  print_dimensions(type_info);
cleanup:
  if (st != NULL) {
    fprintf(stderr, "Error(%d): %s\n", g_ort->GetErrorCode(st), g_ort->GetErrorMessage(st));
    g_ort->ReleaseStatus(st);
  }
  if (type_info != NULL) {
    g_ort->ReleaseTensorTypeAndShapeInfo(type_info);
  }
}
