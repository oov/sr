#include "session.h"

#include "image.h"

#include <ovprintf.h>
#include <ovthreads.h>

enum {
  tile_size = 128,
  batch_size = 1,
};

static OrtValue *create_tensor(FLOAT_TYPE **const data,
                               OrtAllocator *const allocator,
                               size_t const batch_size,
                               size_t const channels,
                               size_t const width,
                               size_t const height) {
  OrtStatus *st = NULL;
  OrtValue *tensor = NULL;
  st = g_ort->CreateTensorAsOrtValue(allocator,
                                     (int64_t[4]){
                                         (int64_t)batch_size,
                                         (int64_t)channels,
                                         (int64_t)height,
                                         (int64_t)width,
                                     },
                                     4,
                                     TENSOR_TYPE,
                                     &tensor);
  if (st != NULL) {
    return NULL;
  }
  st = g_ort->GetTensorMutableData(tensor, (void **)data);
  if (st != NULL) {
    g_ort->ReleaseValue(tensor);
    return NULL;
  }
  return tensor;
}

struct session {
  OrtEnv *env;
  OrtSession *rgb_session;
  OrtSession *alpha_session;
  OrtValue *input_rgb_tensors[2];
  OrtValue *output_rgb_tensors[2];
  OrtValue *input_alpha_tensors[2];
  OrtValue *output_alpha_tensors[2];
  FLOAT_TYPE *input_rgb_tensors_data[2];
  FLOAT_TYPE *output_rgb_tensors_data[2];
  FLOAT_TYPE *input_alpha_tensors_data[2];
  FLOAT_TYPE *output_alpha_tensors_data[2];
  mtx_t mtx;
  cnd_t cnd;
  SR_CHAR_T last_error[256];
};

struct session *session_create(SR_CHAR_T error_msg[256]) {
  struct session *session = NULL;
  OrtAllocator *allocator = NULL;
  OrtStatus *st = NULL;
  SR_CHAR_T const *msg = NULL;

  session = malloc(sizeof(struct session));
  if (session == NULL) {
    msg = SR_TSTR("failed to create session.");
    st = g_ort->CreateStatus(ORT_FAIL, "out of memory.");
    goto cleanup;
  }
  memset(session, 0, sizeof(struct session));

  mtx_init(&session->mtx, mtx_plain);
  cnd_init(&session->cnd);

  st = g_ort->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "sr", &session->env);
  if (st != NULL) {
    msg = SR_TSTR("failed to create environment.");
    goto cleanup;
  }

  st = g_ort->GetAllocatorWithDefaultOptions(&allocator);
  if (st != NULL) {
    msg = SR_TSTR("failed to get default allocator.");
    goto cleanup;
  }

  for (size_t i = 0; i < 2; ++i) {
    session->input_rgb_tensors[i] = create_tensor(&session->input_rgb_tensors_data[i], allocator, batch_size, 3, tile_size, tile_size);
    if (session->input_rgb_tensors[i] == NULL) {
      msg = SR_TSTR("failed to input rgb tensor.");
      goto cleanup;
    }
    session->input_alpha_tensors[i] = create_tensor(&session->input_alpha_tensors_data[i], allocator, batch_size, 3, tile_size, tile_size);
    if (session->input_alpha_tensors[i] == NULL) {
      msg = SR_TSTR("failed to input alpha tensor.");
      goto cleanup;
    }
    session->output_rgb_tensors[i] =
        create_tensor(&session->output_rgb_tensors_data[i], allocator, batch_size, 3, tile_size * 4, tile_size * 4);
    if (session->output_rgb_tensors[i] == NULL) {
      msg = SR_TSTR("failed to output rgb tensor.");
      goto cleanup;
    }
    session->output_alpha_tensors[i] =
        create_tensor(&session->output_alpha_tensors_data[i], allocator, batch_size, 3, tile_size * 4, tile_size * 4);
    if (session->output_alpha_tensors[i] == NULL) {
      msg = SR_TSTR("failed to output alpha tensor.");
      goto cleanup;
    }
  }

cleanup:
  if (st != NULL) {
    ov_snprintf(error_msg, 256, NULL, SR_TSTR("%ls: %hs(%d)"), msg, g_ort->GetErrorMessage(st), g_ort->GetErrorCode(st));
    g_ort->ReleaseStatus(st);
    if (session != NULL) {
      session_destroy(session);
      session = NULL;
    }
  }
  return session;
}

void session_destroy(struct session *const session) {
  if (session == NULL) {
    return;
  }
  for (size_t i = 0; i < 2; ++i) {
    if (session->output_alpha_tensors[i] != NULL) {
      g_ort->ReleaseValue(session->output_alpha_tensors[i]);
      session->output_alpha_tensors[i] = NULL;
      session->output_alpha_tensors_data[i] = NULL;
    }
    if (session->output_rgb_tensors[i] != NULL) {
      g_ort->ReleaseValue(session->output_rgb_tensors[i]);
      session->output_rgb_tensors[i] = NULL;
      session->output_rgb_tensors_data[i] = NULL;
    }
    if (session->input_alpha_tensors[i] != NULL) {
      g_ort->ReleaseValue(session->input_alpha_tensors[i]);
      session->input_alpha_tensors[i] = NULL;
      session->input_alpha_tensors_data[i] = NULL;
    }
    if (session->input_rgb_tensors[i] != NULL) {
      g_ort->ReleaseValue(session->input_rgb_tensors[i]);
      session->input_rgb_tensors[i] = NULL;
      session->input_rgb_tensors_data[i] = NULL;
    }
  }
  if (session->alpha_session != NULL) {
    g_ort->ReleaseSession(session->alpha_session);
    session->alpha_session = NULL;
  }
  if (session->rgb_session != NULL) {
    g_ort->ReleaseSession(session->rgb_session);
    session->rgb_session = NULL;
  }
  if (session->env) {
    g_ort->ReleaseEnv(session->env);
    session->env = NULL;
  }
  cnd_destroy(&session->cnd);
  mtx_destroy(&session->mtx);
  free(session);
}

SR_CHAR_T const *session_get_last_error(struct session const *const session) {
  if (session == NULL || session->last_error[0] == SR_TSTR('\0')) {
    return NULL;
  }
  return session->last_error;
}

static OrtStatus *append_dml_related_options(OrtSessionOptions *session_options, int const device_id) {
  OrtDmlApi const *dml = NULL;
  OrtStatus *st = g_ort->GetExecutionProviderApi("DML", ORT_API_VERSION, (void const **)&dml);
  if (st != NULL) {
    return st;
  }
  st = dml->SessionOptionsAppendExecutionProvider_DML(session_options, device_id);
  if (st != NULL) {
    return st;
  }
  st = g_ort->DisableMemPattern(session_options);
  if (st != NULL) {
    return st;
  }
  st = g_ort->SetSessionExecutionMode(session_options, ORT_SEQUENTIAL);
  if (st != NULL) {
    return st;
  }
  return NULL;
}

static OrtSession *load_model(struct session_options const *const opts, OrtEnv *const env, SR_CHAR_T error_msg[256]) {
  OrtSessionOptions *session_options = NULL;
  OrtSession *sess = NULL;
  OrtStatus *st = NULL;
  SR_CHAR_T const *msg = NULL;

  st = g_ort->CreateSessionOptions(&session_options);
  if (st != NULL) {
    msg = SR_TSTR("failed to create session options.");
    goto cleanup;
  }

  switch (opts->provider.type) {
  case PROVIDER_CPU:
    break; // do nothing
  case PROVIDER_DML:
    st = append_dml_related_options(session_options, opts->provider.dml.device_id);
    if (st != NULL) {
      msg = SR_TSTR("failed to append DirectML related options.");
      goto cleanup;
    }
    break;
#if 0
  default:
    st = g_ort->CreateStatus(ORT_FAIL, "unknown provider.");
    msg = SR_TSTR("failed to initialize provider.");
    goto cleanup;
#endif
  }

  st = g_ort->AddFreeDimensionOverrideByName(session_options, "batch_size", (int64_t)batch_size);
  if (st != NULL) {
    msg = SR_TSTR("failed to add batch size override.");
    goto cleanup;
  }
  st = g_ort->AddFreeDimensionOverrideByName(session_options, "height", (int64_t)tile_size);
  if (st != NULL) {
    msg = SR_TSTR("failed to add height override.");
    goto cleanup;
  }
  st = g_ort->AddFreeDimensionOverrideByName(session_options, "width", (int64_t)tile_size);
  if (st != NULL) {
    msg = SR_TSTR("failed to add width override.");
    goto cleanup;
  }

  if (opts->memory.len) {
    st = g_ort->CreateSessionFromArray(env, opts->memory.ptr, opts->memory.len, session_options, &sess);
  } else {
    st = g_ort->CreateSession(env, opts->file.path, session_options, &sess);
  }
  if (st != NULL) {
    if (g_ort->GetErrorCode(st) == ORT_NO_SUCHFILE) {
      msg = SR_TSTR("failed to laod model.");
    } else {
      msg = SR_TSTR("failed to create session.");
    }
    goto cleanup;
  }
cleanup:
  if (st != NULL) {
    ov_snprintf(error_msg, 256, NULL, SR_TSTR("%ls: %hs(%d)"), msg, g_ort->GetErrorMessage(st), g_ort->GetErrorCode(st));
    g_ort->ReleaseStatus(st);
    if (sess != NULL) {
      g_ort->ReleaseSession(sess);
    }
  }
  if (session_options != NULL) {
    g_ort->ReleaseSessionOptions(session_options);
  }
  return sess;
}

bool session_load_rgb_model(struct session *const session, struct session_options const *const opts) {
  OrtSession *sess = NULL;
  if (session == NULL) {
    session->last_error[sr_append(session->last_error, SR_TSTR("session is NULL."))] = SR_TSTR('\0');
    return false;
  }
  sess = load_model(opts, session->env, session->last_error);
  if (sess == NULL) {
    return false;
  }
  if (session->rgb_session != NULL) {
    g_ort->ReleaseSession(session->rgb_session);
  }
  session->rgb_session = sess;
  return true;
}

bool session_load_alpha_model(struct session *const session, struct session_options const *const opts) {
  OrtSession *sess = NULL;
  if (session == NULL) {
    session->last_error[sr_append(session->last_error, SR_TSTR("session is NULL."))] = SR_TSTR('\0');
    return false;
  }
  sess = load_model(opts, session->env, session->last_error);
  if (sess == NULL) {
    return false;
  }
  if (session->alpha_session != NULL) {
    g_ort->ReleaseSession(session->alpha_session);
  }
  session->alpha_session = sess;
  return true;
}

struct async_context {
  struct session *session;
  size_t n;
  OrtStatus *status;
};

static void async_callback(void *user_data, OrtValue **outputs, size_t num_outputs, OrtStatus *status) {
  (void)outputs;
  (void)num_outputs;
  struct async_context *ctx = (struct async_context *)user_data;
  if (status != NULL) {
    if (ctx->status == NULL) {
      ctx->status = status;
    } else {
      g_ort->ReleaseStatus(status);
    }
  }
  mtx_lock(&ctx->session->mtx);
  ++ctx->n;
  cnd_signal(&ctx->session->cnd);
  mtx_unlock(&ctx->session->mtx);
}

static inline void swap_tensor_and_data(OrtValue **const a, OrtValue **const b, FLOAT_TYPE **const c, FLOAT_TYPE **const d) {
  OrtValue *tmp1 = *a;
  FLOAT_TYPE *tmp2 = *c;
  *a = *b;
  *b = tmp1;
  *c = *d;
  *d = tmp2;
}

struct position {
  size_t x;
  size_t y;
};

static inline void swap_position(struct position *const a, struct position *const b) {
  struct position tmp = *a;
  *a = *b;
  *b = tmp;
}

bool session_inference(struct session *const session, struct session_image *const image) {
  if (session == NULL) {
    session->last_error[sr_append(session->last_error, SR_TSTR("session is NULL"))] = SR_TSTR('\0');
    return false;
  }
  if (session->rgb_session == NULL || session->alpha_session == NULL) {
    session->last_error[sr_append(session->last_error, SR_TSTR("model is not loaded"))] = SR_TSTR('\0');
    return false;
  }

  OrtSession *const session_rgb = session->rgb_session;
  OrtSession *const session_alpha = session->alpha_session;

  uint8_t const *const source = image->source;
  size_t const source_width = image->width;
  size_t const source_height = image->height;
  uint8_t *destination = image->destination;
  if (source == NULL || source_width == 0 || source_height == 0 || destination == NULL) {
    session->last_error[sr_append(session->last_error, SR_TSTR("invalid parameter"))] = SR_TSTR('\0');
    return false;
  }

  OrtStatus *st = NULL;
  SR_CHAR_T const *msg = NULL;
  size_t running = 0;

  OrtValue *input_rgb_tensors[2] = {session->input_rgb_tensors[0], session->input_rgb_tensors[1]};
  OrtValue *output_rgb_tensors[2] = {session->output_rgb_tensors[0], session->output_rgb_tensors[1]};
  OrtValue *input_alpha_tensors[2] = {session->input_alpha_tensors[0], session->input_alpha_tensors[1]};
  OrtValue *output_alpha_tensors[2] = {session->output_alpha_tensors[0], session->output_alpha_tensors[1]};
  FLOAT_TYPE *input_rgb_tensors_data[2] = {session->input_rgb_tensors_data[0], session->input_rgb_tensors_data[1]};
  FLOAT_TYPE *output_rgb_tensors_data[2] = {session->output_rgb_tensors_data[0], session->output_rgb_tensors_data[1]};
  FLOAT_TYPE *input_alpha_tensors_data[2] = {session->input_alpha_tensors_data[0], session->input_alpha_tensors_data[1]};
  FLOAT_TYPE *output_alpha_tensors_data[2] = {session->output_alpha_tensors_data[0], session->output_alpha_tensors_data[1]};

  struct async_context ctx = {session, 0, NULL};

  size_t const overlap = 8;
  size_t const num_tiles_x = (source_width + tile_size - overlap - 1) / (tile_size - overlap);
  size_t const num_tiles_y = (source_height + tile_size - overlap - 1) / (tile_size - overlap);
  size_t const num_tiles = num_tiles_x * num_tiles_y;

  struct position target[batch_size * 2] = {0};
  size_t completed = 0, processed = 0, processing = 0;
  size_t y = 0, x = 0;
  while (completed < num_tiles) {
    if (processed > 0) {
      size_t const n = processed - completed;
      for (size_t i = 0; i < n; ++i) {
        if (!image->lock(target[i].x * 4, target[i].y * 4, tile_size * 4, tile_size * 4, completed + i, num_tiles, image->userdata)) {
          st = g_ort->CreateStatus(ORT_OK, "aborted by user");
          msg = SR_TSTR("interrupted");
          goto cleanup;
        }
        chw_to_hwc(output_rgb_tensors_data[0] + i * 3 * tile_size * 4 * tile_size * 4,
                   output_alpha_tensors_data[0] + i * 3 * tile_size * 4 * tile_size * 4,
                   tile_size * 4,
                   destination,
                   source_width * 4,
                   source_height * 4,
                   target[i].x * 4,
                   target[i].y * 4,
                   overlap * 4);
        image->unlock(image->userdata);
      }
      completed = processed;
    }

    size_t n = 0;
    while (y < source_height && x < source_width && n < batch_size) {
      hwc_to_chw(source,
                 source_width,
                 source_height,
                 x,
                 y,
                 tile_size,
                 input_rgb_tensors_data[0] + n * 3 * tile_size * tile_size,
                 input_alpha_tensors_data[0] + n * 3 * tile_size * tile_size);
      target[n].x = x;
      target[n].y = y;
      ++n;
      x += tile_size - overlap;
      if (x >= source_width) {
        x = 0;
        y += tile_size - overlap;
      }
    }

    if (processed != processing) {
      mtx_lock(&session->mtx);
      while (ctx.n < running) {
        cnd_wait(&session->cnd, &session->mtx);
      }
      ctx.n = 0;
      mtx_unlock(&session->mtx);
      if (ctx.status != NULL) {
        st = ctx.status;
        ctx.status = NULL;
        msg = SR_TSTR("failed to run session");
        goto cleanup;
      }
    }
    processed = processing;
    processing += n;
    running = 0;

    if (n) {
      st = g_ort->RunAsync(session_rgb,
                           NULL,
                           (const char *const[]){"input"},
                           (OrtValue const *const[]){input_rgb_tensors[0]},
                           1,
                           (const char *const[]){"output"},
                           1,
                           (OrtValue *[]){output_rgb_tensors[0]},
                           async_callback,
                           &ctx);
      if (st != NULL) {
        msg = SR_TSTR("failed to run session for RGB");
        goto cleanup;
      }
      ++running;
      st = g_ort->RunAsync(session_alpha,
                           NULL,
                           (const char *const[]){"input"},
                           (OrtValue const *const[]){input_alpha_tensors[0]},
                           1,
                           (const char *const[]){"output"},
                           1,
                           (OrtValue *[]){output_alpha_tensors[0]},
                           async_callback,
                           &ctx);
      if (st != NULL) {
        msg = SR_TSTR("failed to run session for Alpha");
        goto cleanup;
      }
      ++running;
    }

    swap_tensor_and_data(&input_rgb_tensors[0], &input_rgb_tensors[1], &input_rgb_tensors_data[0], &input_rgb_tensors_data[1]);
    swap_tensor_and_data(&output_rgb_tensors[0], &output_rgb_tensors[1], &output_rgb_tensors_data[0], &output_rgb_tensors_data[1]);
    swap_tensor_and_data(&input_alpha_tensors[0], &input_alpha_tensors[1], &input_alpha_tensors_data[0], &input_alpha_tensors_data[1]);
    swap_tensor_and_data(&output_alpha_tensors[0], &output_alpha_tensors[1], &output_alpha_tensors_data[0], &output_alpha_tensors_data[1]);
    for (size_t i = 0; i < batch_size; ++i) {
      swap_position(&target[i], &target[batch_size + i]);
    }
  }
cleanup:
  if (running) {
    mtx_lock(&session->mtx);
    while (ctx.n < running) {
      cnd_wait(&session->cnd, &session->mtx);
    }
    mtx_unlock(&session->mtx);
    if (ctx.status != NULL) {
      if (st == NULL) {
        st = ctx.status;
      } else {
        g_ort->ReleaseStatus(ctx.status);
      }
    }
  }
  if (st != NULL) {
    OrtErrorCode const code = g_ort->GetErrorCode(st);
    ov_snprintf(session->last_error, 256, NULL, SR_TSTR("%ls: %hs(%d)"), msg, g_ort->GetErrorMessage(st), code);
    g_ort->ReleaseStatus(st);
    return code == ORT_OK;
  }
  return true;
}
