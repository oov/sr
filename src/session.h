#pragma once

#include <ovthreads.h>

#include "common.h"
#include "onnx.h"

enum session_provider_type {
  PROVIDER_CPU,
  PROVIDER_DML,
};

struct session_provider {
  enum session_provider_type type;
  union {
    struct cpu {
      int does_not_have_any_options;
    } cpu;
    struct dml {
      int device_id;
    } dml;
  };
};

struct session_options {
  struct session_provider provider;
  union {
    struct file {
      SR_CHAR_T const *const path;
      size_t unused;
    } file;
    struct memory {
      void *ptr;
      size_t const len;
    } memory;
  };
};

struct session_image {
  size_t width;
  size_t height;
  size_t channels;
  uint8_t *source;      // width * height * channels
  uint8_t *destination; // (width * 4) * (height * 4) * channels
  void *userdata;
  bool (*lock)(
      size_t const x, size_t const y, size_t const w, size_t const h, size_t const progress, size_t const total, void *const userdata);
  void (*unlock)(void *const userdata);
};

struct session;

struct session *session_create(SR_CHAR_T error_msg[256]);
void session_destroy(struct session *const session);
SR_CHAR_T const *session_get_last_error(struct session const *const session);
bool session_load_rgb_model(struct session *const session, struct session_options const *const opts);
bool session_load_alpha_model(struct session *const session, struct session_options const *const opts);
bool session_inference(struct session *const session, struct session_image *const image);
