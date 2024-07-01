#include <ovarray.h>
#include <ovbase.h>
#include <ovprintf.h>
#include <ovthreads.h>
#include <ovutil/win32.h>

#include "common.h"

#include "image.h"
#include "onnx.h"
#include "session.h"

#include <math.h>
#include <stdio.h>
#include <time.h>

#include <commdlg.h>
#include <dwmapi.h>
#include <shobjidl.h>

enum {
  model_group_id = 0x1001,
  model_preset_combo_box_id = 0x1002,
  model_rgb_label_id = 0x1003,
  model_rgb_combo_box_id = 0x1004,
  model_alpha_label_id = 0x1005,
  model_alpha_combo_box_id = 0x1006,
  input_image_label_id = 0x1101,
  input_image_edit_id = 0x1102,
  input_image_browse_button_id = 0x1103,
  device_label_id = 0x1201,
  device_combo_box_id = 0x1202,
  start_button_id = 0x2001,
  save_button_id = 0x2002,
  progress_id = 0x2003,
  progress_description_id = 0x2004,

  WM_THREAD_COMPLETE = WM_USER + 1,
  WM_THREAD_COMPLETE_SUCCESS = 0,
  WM_THREAD_COMPLETE_FAILURE = 1,
  WM_THREAD_COMPLETE_ABORTED = 2,
  WM_UPDATE_PROGRESS = WM_USER + 2,
  WM_CHANGE_STATE = WM_USER + 3,
};

static SR_CHAR_T const g_model_group_label_text[] = SR_TSTR("使用するモデル");
static SR_CHAR_T const g_model_rgb_label_text[] = SR_TSTR("RGB");
static SR_CHAR_T const g_model_alpha_label_text[] = SR_TSTR("Alpha");
static SR_CHAR_T const g_input_image_label_text[] = SR_TSTR("入力画像");
static SR_CHAR_T const g_input_image_browse_button_text[] = SR_TSTR("参照...");
static SR_CHAR_T const g_device_label_text[] = SR_TSTR("実行環境");
static SR_CHAR_T const g_start_button_text[] = SR_TSTR("処理開始");
static SR_CHAR_T const g_abort_button_text[] = SR_TSTR("中止");
static SR_CHAR_T const g_save_button_text[] = SR_TSTR("保存...");

static struct model {
  SR_CHAR_T const *name;
  SR_CHAR_T const *path;
  SR_CHAR_T const *rgb_description;
  SR_CHAR_T const *alpha_description;
} const g_models[] = {
    {
        SR_TSTR("realesr-general-x4v3"),
        SR_TSTR("models/realesrgan/realesr-general-x4v3.onnx"),
        SR_TSTR("Real-ESRGAN - 一般向け"),
        SR_TSTR("Real-ESRGAN - 一般向け"),
    },
    {
        SR_TSTR("realesr-general-wdn-x4v3"),
        SR_TSTR("models/realesrgan/realesr-general-wdn-x4v3.onnx"),
        SR_TSTR("Real-ESRGAN - 一般向け（ノイズ除去あり）"),
        SR_TSTR("Real-ESRGAN - 一般向け（ノイズ除去あり）"),
    },
    {
        SR_TSTR("RealESRGAN_x4plus_anime_6B"),
        SR_TSTR("models/realesrgan/RealESRGAN_x4plus_anime_6B.onnx"),
        SR_TSTR("Real-ESRGAN - アニメ・イラスト向け"),
        SR_TSTR("Real-ESRGAN - アニメ・イラスト向け"),
    },
    {
        SR_TSTR("realesr-animevideov3"),
        SR_TSTR("models/realesrgan/realesr-animevideov3.onnx"),
        SR_TSTR("Real-ESRGAN - アニメ・速度重視"),
        SR_TSTR("[非推奨] Real-ESRGAN - アニメ・速度重視"),
    },
    {
        SR_TSTR("up4x-latest-no-denoise"),
        SR_TSTR("models/realcugan/up4x-latest-no-denoise.onnx"),
        SR_TSTR("Real-CUGAN - アニメ・イラスト向け"),
        SR_TSTR("[非推奨] Real-CUGAN - アニメ・イラスト向け"),
    },
    {
        SR_TSTR("up4x-latest-conservative"),
        SR_TSTR("models/realcugan/up4x-latest-conservative.onnx"),
        SR_TSTR("Real-CUGAN - アニメ・イラスト向け（ノイズ除去あり）"),
        SR_TSTR("[非推奨] Real-CUGAN - アニメ・イラスト向け（ノイズ除去あり）"),
    },
    {
        SR_TSTR("up4x-latest-denoise3x"),
        SR_TSTR("models/realcugan/up4x-latest-denoise3x.onnx"),
        SR_TSTR("Real-CUGAN - アニメ・イラスト向け（強いノイズ除去あり）"),
        SR_TSTR("[非推奨] Real-CUGAN - アニメ・イラスト向け（強いノイズ除去あり）"),
    },
};

static struct model_preset {
  SR_CHAR_T const *name;
  size_t rgb_index;
  size_t alpha_index;
} const g_model_presets[] = {
    {SR_TSTR("（プリセットから選ぶ）"), 0, 0},
    {SR_TSTR("写真 - イラストに比べて柔らかい出力になりやすい"), 0, 0},
    {SR_TSTR("イラスト - 線などがハッキリするがグラデや細かい表現が失われることがある"), 2, 0},
    {SR_TSTR("イラスト２ - ディティールが残りやすいが出力品質が安定しないことがある"), 4, 0},
};

static struct provider {
  struct session_provider provider;
  SR_CHAR_T description[256];
} *g_providers = NULL;

static mtx_t g_mtx;
static thrd_t g_thrd;
static struct session *g_session = NULL;
static SR_CHAR_T *g_source_image_path = NULL;
static uint8_t *g_source_image = NULL;
static size_t g_source_width = 0;
static size_t g_source_height = 0;
static uint8_t *g_destination_image = NULL;
static size_t g_active_provider_index = (size_t)-1;
static size_t g_rgb_model_index = sizeof(g_models) / sizeof(g_models[0]);
static size_t g_alpha_model_index = sizeof(g_models) / sizeof(g_models[0]);

static HFONT g_font = NULL;
static HWND g_window = NULL;
static HWND g_model_group = NULL;
static HWND g_model_preset_combo_box = NULL;
static HWND g_model_rgb_label = NULL;
static HWND g_model_rgb_combo_box = NULL;
static HWND g_model_alpha_label = NULL;
static HWND g_model_alpha_combo_box = NULL;
static HWND g_input_image_label = NULL;
static HWND g_input_image_edit = NULL;
static HWND g_input_image_browse_button = NULL;
static HWND g_device_label = NULL;
static HWND g_device_combo_box = NULL;
static HWND g_progress = NULL;
static HWND g_progress_description = NULL;
static HWND g_start_button = NULL;
static HWND g_save_button = NULL;
static HWND g_image_preview = NULL;
static ITaskbarList3 *g_taskbar_list = NULL;

static enum sr_state {
  sr_ready,
  sr_processing,
  sr_completed,
  sr_aborting,
  sr_closing,
} g_state = sr_ready;

static enum sr_state inline get_state(void) {
  enum sr_state state;
  mtx_lock(&g_mtx);
  state = g_state;
  mtx_unlock(&g_mtx);
  return state;
}

static enum sr_state inline set_state(enum sr_state const state) {
  mtx_lock(&g_mtx);
  g_state = state;
  mtx_unlock(&g_mtx);
  return state;
}

static LRESULT CALLBACK image_preview_proc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam) {
  static bool dragging = false;
  static LPARAM dragging_pos = 0;
  static int dragging_scroll_x = 0;
  static int dragging_scroll_y = 0;
  switch (msg) {
  case WM_HSCROLL: {
    SCROLLINFO si = {
        .cbSize = sizeof(si),
        .fMask = SIF_ALL,
    };
    GetScrollInfo(window, SB_HORZ, &si);
    switch (LOWORD(wparam)) {
    case SB_TOP:
      si.nPos = si.nMin;
      break;
    case SB_BOTTOM:
      si.nPos = si.nMax;
      break;
    case SB_LINEUP:
      si.nPos -= 400;
      break;
    case SB_LINEDOWN:
      si.nPos += 400;
      break;
    case SB_PAGEUP:
      si.nPos -= si.nPage;
      break;
    case SB_PAGEDOWN:
      si.nPos += si.nPage;
      break;
    case SB_THUMBTRACK:
      si.nPos = si.nTrackPos;
      break;
    case SB_THUMBPOSITION:
      si.nPos = HIWORD(wparam);
      break;
    }
    si.fMask = SIF_POS;
    SetScrollInfo(window, SB_HORZ, &si, TRUE);
    InvalidateRect(window, NULL, TRUE);
  } break;
  case WM_VSCROLL: {
    SCROLLINFO si = {
        .cbSize = sizeof(si),
        .fMask = SIF_ALL,
    };
    GetScrollInfo(window, SB_VERT, &si);
    switch (LOWORD(wparam)) {
    case SB_TOP:
      si.nPos = si.nMin;
      break;
    case SB_BOTTOM:
      si.nPos = si.nMax;
      break;
    case SB_LINEUP:
      si.nPos -= 400;
      break;
    case SB_LINEDOWN:
      si.nPos += 400;
      break;
    case SB_PAGEUP:
      si.nPos -= si.nPage;
      break;
    case SB_PAGEDOWN:
      si.nPos += si.nPage;
      break;
    case SB_THUMBTRACK:
      si.nPos = si.nTrackPos;
      break;
    case SB_THUMBPOSITION:
      si.nPos = HIWORD(wparam);
      break;
    }
    si.fMask = SIF_POS;
    SetScrollInfo(window, SB_VERT, &si, TRUE);
    InvalidateRect(window, NULL, TRUE);
  } break;
  case WM_LBUTTONDOWN:
    dragging = true;
    dragging_pos = lparam;
    dragging_scroll_x = GetScrollPos(window, SB_HORZ);
    dragging_scroll_y = GetScrollPos(window, SB_VERT);
    SetCapture(window);
    break;
  case WM_LBUTTONUP:
    dragging = false;
    ReleaseCapture();
    break;
  case WM_MOUSEMOVE:
    if (dragging) {
      SetScrollPos(window, SB_HORZ, dragging_scroll_x - ((int16_t)(LOWORD(lparam)) - (int16_t)(LOWORD(dragging_pos))), TRUE);
      SetScrollPos(window, SB_VERT, dragging_scroll_y - ((int16_t)(HIWORD(lparam)) - (int16_t)(HIWORD(dragging_pos))), TRUE);
      InvalidateRect(window, NULL, TRUE);
    }
    break;
  case WM_PAINT: {
    int ofsx = GetScrollPos(window, SB_HORZ);
    int ymin, ymax, ofsy = GetScrollPos(window, SB_VERT);
    GetScrollRange(window, SB_VERT, &ymin, &ymax);
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(window, &ps);
    RECT r;
    GetClientRect(window, &r);
    if (g_destination_image) {
      mtx_lock(&g_mtx);
      SetDIBitsToDevice(hdc,
                        0,
                        0,
                        (DWORD)(r.right - r.left),
                        (DWORD)(r.bottom - r.top),
                        ofsx,
                        ymax - r.bottom + r.top - ofsy,
                        0,
                        (DWORD)g_source_height * 4,
                        g_destination_image,
                        (BITMAPINFO *)&(BITMAPV4HEADER){
                            .bV4Size = sizeof(BITMAPV4HEADER),
                            .bV4Width = (LONG)g_source_width * 4,
                            .bV4Height = -(LONG)g_source_height * 4,
                            .bV4Planes = 1,
                            .bV4BitCount = 32,
                            .bV4V4Compression = BI_BITFIELDS,
                            .bV4RedMask = 0x000000FF,
                            .bV4GreenMask = 0x0000FF00,
                            .bV4BlueMask = 0x00FF0000,
                            .bV4AlphaMask = 0xFF000000,
                        },
                        DIB_RGB_COLORS);
      mtx_unlock(&g_mtx);
    }
    EndPaint(window, &ps);
    return 0;
  }
  case WM_DESTROY:
    break;
  case WM_ERASEBKGND:
    return 1;
  }
  return DefWindowProcW(window, msg, wparam, lparam);
}

static inline int imax(int const a, int const b) { return a > b ? a : b; }

static void update_preview(void) {
  RECT r;
  GetClientRect(g_image_preview, &r);
  SetScrollInfo(g_image_preview,
                SB_HORZ,
                &(SCROLLINFO){.fMask = SIF_RANGE | SIF_PAGE | SIF_DISABLENOSCROLL,
                              .nMin = 0,
                              .nMax = (int)g_source_width * 4,
                              .nPage = (UINT)(r.right - r.left)},
                TRUE);
  SetScrollInfo(g_image_preview,
                SB_VERT,
                &(SCROLLINFO){.fMask = SIF_RANGE | SIF_PAGE | SIF_DISABLENOSCROLL,
                              .nMin = 0,
                              .nMax = (int)g_source_height * 4,
                              .nPage = (UINT)(r.bottom - r.top)},
                TRUE);
}

static void on_dpi_changed(void) {
  HFONT font = NULL;

  HDC hdc = GetDC(g_window);
  int const dpi = (int)(GetDpiForWindow(g_window));
  {
    NONCLIENTMETRICSW ncm = {
        .cbSize = sizeof(ncm),
    };
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
      ncm.lfMessageFont.lfHeight = -MulDiv(9, dpi, 72);
      font = CreateFontIndirectW(&ncm.lfMessageFont);
    }
    if (!font) {
      font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    }
  }

  HFONT old_font = SelectObject(hdc, font);

  SendMessageW(g_window, WM_SETFONT, (WPARAM)font, 0);
  SendMessageW(g_model_group, WM_SETFONT, (WPARAM)font, 0);
  SendMessageW(g_model_preset_combo_box, WM_SETFONT, (WPARAM)font, 0);
  SendMessageW(g_model_rgb_label, WM_SETFONT, (WPARAM)font, 0);
  SendMessageW(g_model_rgb_combo_box, WM_SETFONT, (WPARAM)font, 0);
  SendMessageW(g_model_alpha_combo_box, WM_SETFONT, (WPARAM)font, 0);
  SendMessageW(g_model_alpha_label, WM_SETFONT, (WPARAM)font, 0);
  SendMessageW(g_input_image_label, WM_SETFONT, (WPARAM)font, 0);
  SendMessageW(g_input_image_edit, WM_SETFONT, (WPARAM)font, 0);
  SendMessageW(g_input_image_browse_button, WM_SETFONT, (WPARAM)font, 0);
  SendMessageW(g_device_label, WM_SETFONT, (WPARAM)font, 0);
  SendMessageW(g_device_combo_box, WM_SETFONT, (WPARAM)font, 0);
  SendMessageW(g_progress_description, WM_SETFONT, (WPARAM)font, 0);
  SendMessageW(g_start_button, WM_SETFONT, (WPARAM)font, 0);
  SendMessageW(g_save_button, WM_SETFONT, (WPARAM)font, 0);

  if (g_font) {
    DeleteObject(g_font);
  }
  g_font = font;

  TEXTMETRICW tm;
  GetTextMetricsW(hdc, &tm);

  int client_width;
  int client_height;
  int combo_box_height;
  int button_width;
  int model_group_label_width;
  int model_label_max_width;
  int model_combo_box_width;
  int right_section_label_width;
  {
    RECT r;
    SIZE sz1, sz2;

    GetClientRect(g_window, &r);
    client_width = r.right - r.left;
    client_height = r.bottom - r.top;

    GetWindowRect(g_model_preset_combo_box, &r);
    combo_box_height = r.bottom - r.top;

    GetTextExtentPoint32W(hdc, g_model_group_label_text, (int)SR_STRLEN(g_model_group_label_text), &sz1);
    model_group_label_width = sz1.cx;

    GetTextExtentPoint32W(hdc, g_input_image_label_text, (int)SR_STRLEN(g_input_image_label_text), &sz1);
    GetTextExtentPoint32W(hdc, g_device_label_text, (int)SR_STRLEN(g_device_label_text), &sz2);
    right_section_label_width = imax(sz1.cx, sz2.cx);

    GetTextExtentPoint32W(hdc, g_save_button_text, (int)SR_STRLEN(g_save_button_text), &sz1);
    GetTextExtentPoint32W(hdc, g_start_button_text, (int)SR_STRLEN(g_start_button_text), &sz2);
    button_width = imax(sz1.cx, sz2.cx);
    GetTextExtentPoint32W(hdc, g_input_image_browse_button_text, (int)SR_STRLEN(g_input_image_browse_button_text), &sz1);
    button_width = imax(button_width, sz1.cx) + MulDiv(48, dpi, 96);

    GetTextExtentPoint32W(hdc, g_model_rgb_label_text, (int)SR_STRLEN(g_model_rgb_label_text), &sz1);
    GetTextExtentPoint32W(hdc, g_model_alpha_label_text, (int)SR_STRLEN(g_model_alpha_label_text), &sz2);
    model_label_max_width = imax(sz1.cx, sz2.cx);

    model_combo_box_width = 0;
    for (size_t i = 0; i < sizeof(g_models) / sizeof(g_models[0]); ++i) {
      GetTextExtentPoint32W(hdc, g_models[i].rgb_description, (int)SR_STRLEN(g_models[i].rgb_description), &sz1);
      GetTextExtentPoint32W(hdc, g_models[i].alpha_description, (int)SR_STRLEN(g_models[i].alpha_description), &sz2);
      model_combo_box_width = imax(model_combo_box_width, imax(sz1.cx, sz2.cx));
    }
    for (size_t i = 0; i < sizeof(g_model_presets) / sizeof(g_model_presets[0]); ++i) {
      GetTextExtentPoint32W(hdc, g_model_presets[i].name, (int)SR_STRLEN(g_model_presets[i].name), &sz1);
      model_combo_box_width = imax(model_combo_box_width, model_group_label_width + sz1.cx);
    }
    model_combo_box_width += MulDiv(16, dpi, 96);
  }

  int const margin = MulDiv(16, dpi, 96);
  int const label_padding = MulDiv(8, dpi, 96);
  int const label_height = tm.tmHeight;

  int const model_group_padding_top = (tm.tmHeight + combo_box_height) / 2 + margin;

  int const model_group_width = margin + model_label_max_width + label_padding + model_combo_box_width + margin;
  int const model_group_height = model_group_padding_top + combo_box_height + margin + combo_box_height + margin;

  SetWindowPos(g_model_group,
               NULL,
               margin,
               margin,
               model_group_width,
               model_group_padding_top + combo_box_height + margin + combo_box_height + margin,
               SWP_NOZORDER | SWP_NOACTIVATE);

  SetWindowPos(g_model_preset_combo_box,
               NULL,
               margin + label_padding + model_group_label_width + margin,
               margin + (label_height - combo_box_height) / 2,
               model_group_width - (margin + model_group_label_width + margin),
               400,
               SWP_NOZORDER | SWP_NOACTIVATE);

  SetWindowPos(g_model_rgb_label,
               NULL,
               margin + margin,
               margin + model_group_padding_top + (combo_box_height - label_height) / 2,
               model_label_max_width,
               label_height,
               SWP_NOZORDER | SWP_NOACTIVATE);
  SetWindowPos(g_model_rgb_combo_box,
               NULL,
               margin + margin + model_label_max_width + label_padding,
               margin + model_group_padding_top,
               model_combo_box_width,
               400,
               SWP_NOZORDER | SWP_NOACTIVATE);

  SetWindowPos(g_model_alpha_label,
               NULL,
               margin + margin,
               margin + model_group_padding_top + combo_box_height + margin + (combo_box_height - label_height) / 2,
               model_label_max_width,
               label_height,
               SWP_NOZORDER | SWP_NOACTIVATE);
  SetWindowPos(g_model_alpha_combo_box,
               NULL,
               margin + margin + model_label_max_width + label_padding,
               margin + model_group_padding_top + combo_box_height + margin,
               model_combo_box_width,
               400,
               SWP_NOZORDER | SWP_NOACTIVATE);

  int const right_width = imax(client_width - margin - model_group_width - margin - margin, button_width * 3);
  SetWindowPos(g_input_image_label,
               NULL,
               margin + model_group_width + margin,
               margin + (combo_box_height - label_height) / 2,
               right_section_label_width,
               label_height,
               SWP_NOZORDER | SWP_NOACTIVATE);
  SetWindowPos(g_input_image_edit,
               NULL,
               margin + model_group_width + margin + right_section_label_width + label_padding,
               margin,
               right_width - right_section_label_width - label_padding - button_width,
               combo_box_height,
               SWP_NOZORDER | SWP_NOACTIVATE);
  SetWindowPos(g_input_image_browse_button,
               NULL,
               margin + model_group_width + margin + right_width - button_width,
               margin,
               button_width,
               combo_box_height,
               SWP_NOZORDER | SWP_NOACTIVATE);

  SetWindowPos(g_device_label,
               NULL,
               margin + model_group_width + margin,
               margin + combo_box_height + margin + (combo_box_height - label_height) / 2,
               right_section_label_width,
               combo_box_height,
               SWP_NOZORDER | SWP_NOACTIVATE);
  SetWindowPos(g_device_combo_box,
               NULL,
               margin + model_group_width + margin + right_section_label_width + label_padding,
               margin + combo_box_height + margin,
               right_width - right_section_label_width - label_padding,
               combo_box_height,
               SWP_NOZORDER | SWP_NOACTIVATE);

  SetWindowPos(g_progress,
               NULL,
               margin + model_group_width + margin,
               margin + model_group_height - combo_box_height,
               right_width - button_width - button_width - label_padding,
               combo_box_height,
               SWP_NOZORDER | SWP_NOACTIVATE);
  SetWindowPos(g_progress_description,
               NULL,
               margin + model_group_width + margin,
               margin + model_group_height - combo_box_height - label_height,
               right_width - button_width - button_width - label_padding,
               label_height,
               SWP_NOZORDER | SWP_NOACTIVATE);

  SetWindowPos(g_start_button,
               NULL,
               margin + model_group_width + margin + right_width - button_width - button_width,
               margin + model_group_height - combo_box_height,
               button_width,
               combo_box_height,
               SWP_NOZORDER | SWP_NOACTIVATE);
  SetWindowPos(g_save_button,
               NULL,
               margin + model_group_width + margin + right_width - button_width,
               margin + model_group_height - combo_box_height,
               button_width,
               combo_box_height,
               SWP_NOZORDER | SWP_NOACTIVATE);

  SetWindowPos(g_image_preview,
               NULL,
               0,
               margin + model_group_height + margin,
               client_width,
               client_height - margin - model_group_height - margin,
               SWP_NOZORDER | SWP_NOACTIVATE);

  update_preview();

  SelectObject(hdc, old_font);
  ReleaseDC(g_window, hdc);
}

static bool enum_dml_device_callback(UINT const device_id, DXGI_ADAPTER_DESC const *const desc, void *const userdata) {
  error *err = (error *)userdata;
  size_t const len = OV_ARRAY_LENGTH(g_providers);
  *err = OV_ARRAY_GROW(&g_providers, len + 1);
  if (efailed(*err)) {
    return false;
  }
  g_providers[len].provider = (struct session_provider){
      .type = PROVIDER_DML,
      .dml =
          {
              .device_id = (int)device_id,
          },
  };
  size_t pos = sr_append(g_providers[len].description, SR_TSTR("DirectML - "));
  g_providers[len].description[pos + sr_append(g_providers[len].description + pos, desc->Description)] = SR_TSTR('\0');

  OV_ARRAY_SET_LENGTH(g_providers, len + 1);
  return true;
}

static bool on_create(void) {
  // DwmSetWindowAttribute(window, 20, &(BOOL){TRUE}, sizeof(BOOL));

  {
    SR_CHAR_T error_msg[256] = {0};
    g_session = session_create(error_msg);
    if (g_session == NULL) {
      MessageBoxW(g_window, error_msg, SR_TSTR("Error"), MB_ICONERROR);
      return false;
    }
  }

  {
    error err = OV_ARRAY_GROW(&g_providers, 4);
    if (efailed(err)) {
      MessageBoxW(g_window, SR_TSTR("failed to allocate memory."), SR_TSTR("Error"), MB_ICONERROR);
      ereport(err);
      return false;
    }
    g_providers[0].provider = (struct session_provider){.type = PROVIDER_CPU};
    g_providers[0].description[sr_append(g_providers[0].description, SR_TSTR("CPU"))] = SR_TSTR('\0');
    OV_ARRAY_SET_LENGTH(g_providers, 1);
    enum_dml_devices(enum_dml_device_callback, &err);
    if (efailed(err)) {
      MessageBoxW(g_window, SR_TSTR("failed to enumerate DML devices."), SR_TSTR("Error"), MB_ICONERROR);
      ereport(err);
      return false;
    }
  }

  HRESULT hr = CoCreateInstance(&CLSID_TaskbarList, NULL, CLSCTX_INPROC_SERVER, &IID_ITaskbarList3, (void **)&g_taskbar_list);
  if (FAILED(hr)) {
    MessageBoxW(g_window, SR_TSTR("failed to create ITaskbarList3."), SR_TSTR("Error"), MB_ICONERROR);
    return false;
  }

  HINSTANCE hinstance = get_hinstance();
  g_model_preset_combo_box = CreateWindowExW(WS_EX_CLIENTEDGE,
                                             L"COMBOBOX",
                                             NULL,
                                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_CLIPSIBLINGS,
                                             0,
                                             0,
                                             1,
                                             1,
                                             g_window,
                                             (HMENU)model_preset_combo_box_id,
                                             hinstance,
                                             NULL);
  g_model_group = CreateWindowExW(0,
                                  L"BUTTON",
                                  g_model_group_label_text,
                                  WS_CHILD | WS_VISIBLE | BS_GROUPBOX | WS_CLIPSIBLINGS,
                                  0,
                                  0,
                                  1,
                                  1,
                                  g_window,
                                  (HMENU)model_group_id,
                                  hinstance,
                                  NULL);

  for (size_t i = 0; i < sizeof(g_model_presets) / sizeof(g_model_presets[0]); ++i) {
    SendMessageW(g_model_preset_combo_box, CB_ADDSTRING, 0, (LPARAM)g_model_presets[i].name);
  }
  SendMessageW(g_model_preset_combo_box, CB_SETCURSEL, 0, 0);

  g_model_rgb_label = CreateWindowExW(0,
                                      L"STATIC",
                                      g_model_rgb_label_text,
                                      WS_CHILD | WS_VISIBLE | SS_RIGHT,
                                      0,
                                      0,
                                      1,
                                      1,
                                      g_window,
                                      (HMENU)model_rgb_label_id,
                                      hinstance,
                                      NULL);
  g_model_rgb_combo_box = CreateWindowExW(WS_EX_CLIENTEDGE,
                                          L"COMBOBOX",
                                          NULL,
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                          0,
                                          0,
                                          1,
                                          1,
                                          g_window,
                                          (HMENU)model_rgb_combo_box_id,
                                          hinstance,
                                          NULL);
  g_model_alpha_label = CreateWindowExW(0,
                                        L"STATIC",
                                        g_model_alpha_label_text,
                                        WS_CHILD | WS_VISIBLE | SS_RIGHT,
                                        0,
                                        0,
                                        1,
                                        1,
                                        g_window,
                                        (HMENU)model_alpha_label_id,
                                        hinstance,
                                        NULL);
  g_model_alpha_combo_box = CreateWindowExW(WS_EX_CLIENTEDGE,
                                            L"COMBOBOX",
                                            NULL,
                                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                            0,
                                            0,
                                            1,
                                            1,
                                            g_window,
                                            (HMENU)model_alpha_combo_box_id,
                                            hinstance,
                                            NULL);

  for (size_t i = 0; i < sizeof(g_models) / sizeof(g_models[0]); ++i) {
    SendMessageW(g_model_rgb_combo_box, CB_ADDSTRING, 0, (LPARAM)g_models[i].rgb_description);
    SendMessageW(g_model_alpha_combo_box, CB_ADDSTRING, 0, (LPARAM)g_models[i].alpha_description);
  }
  SendMessageW(g_model_rgb_combo_box, CB_SETCURSEL, 0, 0);
  SendMessageW(g_model_alpha_combo_box, CB_SETCURSEL, 0, 0);

  g_input_image_label = CreateWindowExW(0,
                                        L"STATIC",
                                        g_input_image_label_text,
                                        WS_CHILD | WS_VISIBLE | SS_RIGHT,
                                        0,
                                        0,
                                        1,
                                        1,
                                        g_window,
                                        (HMENU)input_image_label_id,
                                        hinstance,
                                        NULL);
  g_input_image_edit = CreateWindowExW(WS_EX_CLIENTEDGE,
                                       L"EDIT",
                                       NULL,
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_READONLY,
                                       0,
                                       0,
                                       1,
                                       1,
                                       g_window,
                                       (HMENU)input_image_edit_id,
                                       hinstance,
                                       NULL);
  g_input_image_browse_button = CreateWindowExW(0,
                                                L"BUTTON",
                                                g_input_image_browse_button_text,
                                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                                0,
                                                0,
                                                1,
                                                1,
                                                g_window,
                                                (HMENU)input_image_browse_button_id,
                                                hinstance,
                                                NULL);

  g_device_label = CreateWindowExW(
      0, L"STATIC", g_device_label_text, WS_CHILD | WS_VISIBLE | SS_RIGHT, 0, 0, 1, 1, g_window, (HMENU)device_label_id, hinstance, NULL);
  g_device_combo_box = CreateWindowExW(WS_EX_CLIENTEDGE,
                                       L"COMBOBOX",
                                       NULL,
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                       0,
                                       0,
                                       1,
                                       1,
                                       g_window,
                                       (HMENU)device_combo_box_id,
                                       hinstance,
                                       NULL);

  for (size_t i = 0; i < OV_ARRAY_LENGTH(g_providers); ++i) {
    SendMessageW(g_device_combo_box, CB_ADDSTRING, 0, (LPARAM)g_providers[i].description);
  }
  SendMessageW(g_device_combo_box, CB_SETCURSEL, OV_ARRAY_LENGTH(g_providers) >= 3 ? 1 : 0, 0);

  g_progress = CreateWindowExW(
      0, L"msctls_progress32", NULL, WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 0, 0, 1, 1, g_window, (HMENU)progress_id, hinstance, NULL);
  g_progress_description = CreateWindowExW(
      0, L"STATIC", NULL, WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 1, 1, g_window, (HMENU)progress_description_id, hinstance, NULL);

  g_start_button = CreateWindowExW(0,
                                   L"BUTTON",
                                   g_start_button_text,
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                   0,
                                   0,
                                   1,
                                   1,
                                   g_window,
                                   (HMENU)start_button_id,
                                   hinstance,
                                   NULL);
  g_save_button = CreateWindowExW(0,
                                  L"BUTTON",
                                  g_save_button_text,
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                  0,
                                  0,
                                  1,
                                  1,
                                  g_window,
                                  (HMENU)save_button_id,
                                  hinstance,
                                  NULL);
  EnableWindow(g_save_button, FALSE);

  RegisterClassExW(&(WNDCLASSEXW){
      .cbSize = sizeof(WNDCLASSEXW),
      .style = CS_HREDRAW | CS_VREDRAW,
      .lpfnWndProc = image_preview_proc,
      .hInstance = hinstance,
      .hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512)),
      .hbrBackground = (HBRUSH)(GetStockObject(NULL_BRUSH)),
      .lpszClassName = L"image_preview",
  });
  g_image_preview = CreateWindowExW(WS_EX_CLIENTEDGE,
                                    L"image_preview",
                                    NULL,
                                    WS_CHILD | WS_VISIBLE | WS_HSCROLL | WS_VSCROLL | WS_TABSTOP,
                                    0,
                                    0,
                                    1,
                                    1,
                                    g_window,
                                    NULL,
                                    hinstance,
                                    NULL);

  on_dpi_changed();
  return true;
}

static void on_combo_box_notify(HWND const window, WORD const id, WORD notify, HWND control) {
  (void)window;
  (void)control;
  switch (notify) {
  case CBN_SELCHANGE:
    switch (id) {
    case model_rgb_combo_box_id:
      SendMessageW(g_model_preset_combo_box, CB_SETCURSEL, 0, 0);
      break;
    case model_alpha_combo_box_id:
      SendMessageW(g_model_preset_combo_box, CB_SETCURSEL, 0, 0);
      break;
    case model_preset_combo_box_id: {
      size_t const idx = (size_t)(SendMessageW(g_model_preset_combo_box, CB_GETCURSEL, 0, 0));
      if (idx && idx < sizeof(g_model_presets) / sizeof(g_model_presets[0])) {
        SendMessageW(g_model_rgb_combo_box, CB_SETCURSEL, g_model_presets[idx].rgb_index, 0);
        SendMessageW(g_model_alpha_combo_box, CB_SETCURSEL, g_model_presets[idx].alpha_index, 0);
      }
    } break;
    }
    break;
  }
}

static void select_source_image(HWND const window) {
  IFileDialog *fd = NULL;
  SR_CHAR_T const *msg = NULL;
  PWSTR path = NULL;
  HRESULT hr = CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, &IID_IFileDialog, (LPVOID *)&fd);
  if (FAILED(hr)) {
    msg = SR_TSTR("failed to create file dialog.");
    goto cleanup;
  }
  hr = fd->lpVtbl->SetOptions(fd, FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
  if (FAILED(hr)) {
    msg = SR_TSTR("failed to set options.");
    goto cleanup;
  }
  hr = fd->lpVtbl->SetFileTypes(
      fd,
      1,
      &(COMDLG_FILTERSPEC){L"画像ファイル(*.png;*.jpg;*.jpeg;*.jfif;*.bmp;*.gif;*.tiff)", L"*.png;*.jpg;*.jpeg;*.jfif;*.bmp;*.gif;*.tiff"});
  if (FAILED(hr)) {
    msg = SR_TSTR("failed to set file types.");
    goto cleanup;
  }
  hr = fd->lpVtbl->Show(fd, window);
  if (FAILED(hr)) {
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
      hr = S_OK;
      goto cleanup;
    }
    msg = SR_TSTR("failed to show file dialog.");
    goto cleanup;
  }
  IShellItem *si = NULL;
  hr = fd->lpVtbl->GetResult(fd, &si);
  if (FAILED(hr)) {
    msg = SR_TSTR("failed to get result.");
    goto cleanup;
  }
  hr = si->lpVtbl->GetDisplayName(si, SIGDN_FILESYSPATH, &path);
  if (FAILED(hr)) {
    msg = SR_TSTR("failed to get display name.");
    goto cleanup;
  }
  SetWindowTextW(g_input_image_edit, path);
cleanup:
  if (path) {
    CoTaskMemFree(path);
    path = NULL;
  }
  if (fd) {
    fd->lpVtbl->Release(fd);
    fd = NULL;
  }
  if (FAILED(hr)) {
    SR_CHAR_T buf[512];
    SR_CHAR_T errmsg[256];
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM, NULL, (DWORD)hr, 0, errmsg, sizeof(errmsg) / sizeof(errmsg[0]), NULL);
    ov_snprintf(buf, sizeof(buf) / sizeof(buf[0]), NULL, SR_TSTR("%ls\r\n\r\nError(0x%08x): %ls"), msg, (int)hr, errmsg);
    MessageBoxW(window, buf, NULL, MB_ICONERROR);
  }
}

static SR_CHAR_T *select_destination_image(HWND const window) {
  IFileDialog *fd = NULL;
  SR_CHAR_T const *msg = NULL;
  PWSTR path = NULL;
  HRESULT hr = CoCreateInstance(&CLSID_FileSaveDialog, NULL, CLSCTX_INPROC_SERVER, &IID_IFileDialog, (LPVOID *)&fd);
  if (FAILED(hr)) {
    msg = SR_TSTR("failed to create file dialog.");
    goto cleanup;
  }
  hr = fd->lpVtbl->SetOptions(fd, FOS_PATHMUSTEXIST | FOS_OVERWRITEPROMPT | FOS_FORCEFILESYSTEM | FOS_STRICTFILETYPES);
  if (FAILED(hr)) {
    msg = SR_TSTR("failed to set options.");
    goto cleanup;
  }
  hr = fd->lpVtbl->SetFileTypes(fd,
                                4,
                                (COMDLG_FILTERSPEC[4]){
                                    {L"PNGファイル(*.png)", L"*.png"},
                                    {L"JPEGファイル(*.jpg)", L"*.jpg"},
                                    {L"TGAファイル(*.tga)", L"*.tga"},
                                    {L"Bitmapファイル(*.bmp)", L"*.bmp"},
                                });
  if (FAILED(hr)) {
    msg = SR_TSTR("failed to set file types.");
    goto cleanup;
  }
  hr = fd->lpVtbl->SetDefaultExtension(fd, L"png");
  if (FAILED(hr)) {
    msg = SR_TSTR("failed to set default extension.");
    goto cleanup;
  }
  hr = fd->lpVtbl->Show(fd, window);
  if (FAILED(hr)) {
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
      hr = S_OK;
      goto cleanup;
    }
    msg = SR_TSTR("failed to show file dialog.");
    goto cleanup;
  }
  IShellItem *si = NULL;
  hr = fd->lpVtbl->GetResult(fd, &si);
  if (FAILED(hr)) {
    msg = SR_TSTR("failed to get result.");
    goto cleanup;
  }
  hr = si->lpVtbl->GetDisplayName(si, SIGDN_FILESYSPATH, &path);
  if (FAILED(hr)) {
    msg = SR_TSTR("failed to get display name.");
    goto cleanup;
  }
cleanup:
  if (fd) {
    fd->lpVtbl->Release(fd);
    fd = NULL;
  }
  if (FAILED(hr)) {
    SR_CHAR_T buf[512];
    SR_CHAR_T errmsg[256];
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM, NULL, (DWORD)hr, 0, errmsg, sizeof(errmsg) / sizeof(errmsg[0]), NULL);
    ov_snprintf(buf, sizeof(buf) / sizeof(buf[0]), NULL, SR_TSTR("%ls\r\n\r\nError(0x%08x): %ls"), msg, (int)hr, errmsg);
    MessageBoxW(window, buf, NULL, MB_ICONERROR);
  }
  return path;
}

static void save_image(HWND const window) {
  SR_CHAR_T *filename = NULL;
  SR_CHAR_T const *msg = NULL;
  if (!g_destination_image) {
    msg = SR_TSTR("no image to save.");
    goto cleanup;
  }
  filename = select_destination_image(window);
  if (!filename) {
    return;
  }
  if (!image_save(filename, g_destination_image, g_source_width * 4, g_source_height * 4)) {
    msg = SR_TSTR("failed to save image.");
    goto cleanup;
  }
cleanup:
  if (filename) {
    CoTaskMemFree((LPVOID)filename);
  }
  if (msg) {
    MessageBoxW(window, msg, NULL, MB_ICONERROR);
  }
}

static bool lock_buffer(
    size_t const x, size_t const y, size_t const w, size_t const h, size_t const progress, size_t const total, void *const userdata) {
  (void)x;
  (void)y;
  (void)w;
  (void)h;
  (void)userdata;
  PostMessageW(g_window, WM_UPDATE_PROGRESS, (WPARAM)progress, (LPARAM)total);
  SR_CHAR_T buf[256];
  ov_snprintf(buf, sizeof(buf) / sizeof(buf[0]), NULL, SR_TSTR("処理中... (%d/%d)"), (int)progress, (int)total);
  SetWindowTextW(g_progress_description, buf);
  mtx_lock(&g_mtx);
  if (g_state != sr_processing) {
    mtx_unlock(&g_mtx);
    return false;
  }
  return true;
}

static void unlock_buffer(void *const userdata) {
  (void)userdata;
  mtx_unlock(&g_mtx);
  InvalidateRect(g_image_preview, NULL, TRUE);
}

static int start(void *const userdata) {
  (void)userdata;
  error err = eok();

  PostMessageW(g_window, WM_UPDATE_PROGRESS, 0, 0);

  if (get_state() != sr_processing) {
    goto cleanup;
  }

  // get active execution provider
  struct session_provider *provider = NULL;
  bool provider_changed = false;
  {
    size_t const provider_idx = (size_t)(SendMessageW(g_device_combo_box, CB_GETCURSEL, 0, 0));
    if (provider_idx >= OV_ARRAY_LENGTH(g_providers)) {
      err = emsg_i18nf(err_type_generic, err_unexpected, NULL, "invalid provider index: %d", provider_idx);
      goto cleanup;
    }
    if (provider_idx != g_active_provider_index) {
      provider_changed = true;
      g_active_provider_index = provider_idx;
    }
    provider = &g_providers[provider_idx].provider;
  }

  if (get_state() != sr_processing) {
    goto cleanup;
  }

  // allocate memory for file path
  {
    size_t const len = (size_t)(GetWindowTextLengthW(g_input_image_edit));
    err = OV_ARRAY_GROW(&g_source_image_path, len + 1);
    if (efailed(err)) {
      err = ethru(err);
      goto cleanup;
    }
    GetWindowTextW(g_input_image_edit, g_source_image_path, (int)len + 1);
    g_source_image_path[len] = L'\0';
    OV_ARRAY_SET_LENGTH(g_source_image_path, len);
  }

  if (get_state() != sr_processing) {
    goto cleanup;
  }

  // load image and allocate memory for destination image
  SetWindowTextW(g_progress_description, SR_TSTR("元画像を読み込み中..."));
  {
    if (g_source_image) {
      image_free(g_source_image);
      g_source_image = NULL;
    }
    size_t w, h;
    g_source_image = image_load(g_source_image_path, &w, &h);
    if (g_source_image == NULL) {
      err = emsg_i18nf(err_type_generic, err_fail, NULL, "failed to load image: %ls", g_source_image_path);
      goto cleanup;
    }
    size_t const destination_pixels = w * 4 * 4 * h * 4;
    err = OV_ARRAY_GROW(&g_destination_image, destination_pixels + 32);
    if (efailed(err)) {
      err = ethru(err);
      goto cleanup;
    }
    OV_ARRAY_SET_LENGTH(g_destination_image, destination_pixels + 32);
    g_source_width = w;
    g_source_height = h;
    update_preview();
  }

  if (get_state() != sr_processing) {
    goto cleanup;
  }

  // copy 4x
  mtx_lock(&g_mtx);
  image_nn4x(g_source_image, g_source_width, g_source_height, g_destination_image);
  mtx_unlock(&g_mtx);
  InvalidateRect(g_image_preview, NULL, TRUE);

  if (get_state() != sr_processing) {
    goto cleanup;
  }

  // load rgb model
  SetWindowTextW(g_progress_description, SR_TSTR("RGBモデルを読み込み中..."));
  {
    size_t const rgb_idx = (size_t)(SendMessageW(g_model_rgb_combo_box, CB_GETCURSEL, 0, 0));
    if (rgb_idx >= sizeof(g_models) / sizeof(g_models[0])) {
      err = emsg_i18nf(err_type_generic, err_unexpected, NULL, "invalid model index: %d", rgb_idx);
      goto cleanup;
    }
    if (rgb_idx != g_rgb_model_index || provider_changed) {
      if (!session_load_rgb_model(g_session,
                                  &(struct session_options){
                                      .provider = *provider,
                                      .file =
                                          {
                                              .path = g_models[rgb_idx].path,
                                          },

                                  })) {
        err = emsg_i18nf(err_type_generic,
                         err_fail,
                         NULL,
                         "failed to load RGB model(%1$ls): %2$ls",
                         g_models[rgb_idx].path,
                         session_get_last_error(g_session));
        goto cleanup;
      }
      g_rgb_model_index = rgb_idx;
    }
  }

  if (get_state() != sr_processing) {
    goto cleanup;
  }

  // load alpha model
  SetWindowTextW(g_progress_description, SR_TSTR("Alphaモデルを読み込み中..."));
  {
    size_t const alpha_idx = (size_t)(SendMessageW(g_model_alpha_combo_box, CB_GETCURSEL, 0, 0));
    if (alpha_idx >= sizeof(g_models) / sizeof(g_models[0])) {
      err = emsg_i18nf(err_type_generic, err_unexpected, NULL, "invalid model index: %d", alpha_idx);
      goto cleanup;
    }
    if (alpha_idx != g_alpha_model_index || provider_changed) {
      if (!session_load_alpha_model(g_session,
                                    &(struct session_options){
                                        .provider = *provider,
                                        .file =
                                            {
                                                .path = g_models[alpha_idx].path,
                                            },

                                    })) {
        err = emsg_i18nf(err_type_generic,
                         err_fail,
                         NULL,
                         "failed to load Alpha model(%1$ls): %2$ls",
                         g_models[alpha_idx].path,
                         session_get_last_error(g_session));
        goto cleanup;
      }
      g_alpha_model_index = alpha_idx;
    }
  }

  if (get_state() != sr_processing) {
    goto cleanup;
  }

  // inference
  {
    if (!session_inference(g_session,
                           &(struct session_image){
                               .width = g_source_width,
                               .height = g_source_height,
                               .channels = 4,
                               .source = g_source_image,
                               .destination = g_destination_image,
                               .lock = lock_buffer,
                               .unlock = unlock_buffer,
                           })) {
      err = emsg_i18nf(err_type_generic, err_fail, NULL, "failed to inference: %ls", session_get_last_error(g_session));
      goto cleanup;
    }
  }
  InvalidateRect(g_image_preview, NULL, TRUE);

#if 0
  timespec_get(&end, TIME_UTC);
  printf("elapsed: %f\n", calc_elapsed(&start, &end));
#endif
cleanup:
  SetWindowTextW(g_progress_description, SR_TSTR(""));
  PostMessageW(g_window, WM_UPDATE_PROGRESS, 100, 100);
  if (efailed(err)) {
    ereport(err);
    PostMessageW(g_window, WM_THREAD_COMPLETE, WM_THREAD_COMPLETE_FAILURE, 0);
  } else {
    PostMessageW(g_window, WM_THREAD_COMPLETE, get_state() == sr_processing ? WM_THREAD_COMPLETE_SUCCESS : WM_THREAD_COMPLETE_ABORTED, 0);
  }
  return 0;
}

static void on_button_notify(HWND const window, WORD const id, WORD notify, HWND control) {
  (void)control;
  switch (notify) {
  case BN_CLICKED:
    switch (id) {
    case input_image_browse_button_id:
      select_source_image(window);
      break;
    case start_button_id:
      mtx_lock(&g_mtx);
      if (g_state == sr_processing) {
        g_state = sr_aborting;
        mtx_unlock(&g_mtx);
        SendMessageW(window, WM_CHANGE_STATE, (WPARAM)sr_aborting, 0);
      } else {
        g_state = sr_processing;
        mtx_unlock(&g_mtx);
        SendMessageW(window, WM_CHANGE_STATE, (WPARAM)sr_processing, 0);
        if (thrd_create(&g_thrd, start, (void *)window) == thrd_error) {
          MessageBoxW(window, SR_TSTR("failed to create thread."), SR_TSTR("Error"), MB_ICONERROR);
        }
      }
      break;
    case save_button_id:
      save_image(window);
      break;
    }
    break;
  }
}

static void on_destroy(void) {
  if (g_font) {
    SendMessageW(g_model_rgb_combo_box, WM_SETFONT, (WPARAM)NULL, MAKELPARAM(FALSE, 0));
    DeleteObject(g_font);
    g_font = NULL;
  }
  if (g_destination_image) {
    OV_ARRAY_DESTROY(&g_destination_image);
  }
  if (g_source_image) {
    image_free(g_source_image);
    g_source_image = NULL;
  }
  if (g_source_image_path) {
    OV_ARRAY_DESTROY(&g_source_image_path);
  }
  if (g_taskbar_list) {
    g_taskbar_list->lpVtbl->Release(g_taskbar_list);
    g_taskbar_list = NULL;
  }
  if (g_providers) {
    OV_ARRAY_DESTROY(&g_providers);
  }
  if (g_session) {
    session_destroy(g_session);
    g_session = NULL;
  }
}

static LRESULT CALLBACK window_proc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam) {
  switch (msg) {
  case WM_CREATE:
    g_window = window;
    on_create();
    break;
  case WM_CLOSE:
    if (get_state() != sr_processing) {
      DestroyWindow(window);
      return 0;
    }
    if (MessageBoxW(window, L"Are you sure you want to exit?", L"Confirm", MB_ICONQUESTION | MB_YESNO) == IDNO) {
      return 0;
    }
    mtx_lock(&g_mtx);
    if (g_state == sr_processing) {
      g_state = sr_closing;
      mtx_unlock(&g_mtx);
      return 0;
    }
    mtx_unlock(&g_mtx);
    DestroyWindow(window);
    return 0;
  case WM_DESTROY:
    on_destroy();
    PostQuitMessage(0);
    break;
  case WM_DPICHANGED:
    on_dpi_changed();
    break;
  case WM_COMMAND:
    if (lparam) {
      HWND const ctl = (HWND)lparam;
      if (ctl == g_model_preset_combo_box || ctl == g_model_rgb_combo_box || ctl == g_model_alpha_combo_box) {
        on_combo_box_notify(window, LOWORD(wparam), HIWORD(wparam), ctl);
      } else if (ctl == g_input_image_browse_button || ctl == g_start_button || ctl == g_save_button) {
        on_button_notify(window, LOWORD(wparam), HIWORD(wparam), ctl);
      }
    } else {
      // Menu or Accelerator
    }
    break;
  case WM_SIZE:
    on_dpi_changed();
    break;
  case WM_THREAD_COMPLETE:
    thrd_detach(g_thrd);
    if (get_state() == sr_closing) {
      DestroyWindow(window);
    } else {
      SendMessageW(window, WM_CHANGE_STATE, (WPARAM)(wparam == 0 ? sr_completed : sr_ready), 0);
    }
    break;
  case WM_UPDATE_PROGRESS:
    if (wparam == 0 && lparam == 0) {
      // SetWindowLongPtrW(g_progress, GWL_STYLE, GetWindowLongPtrW(g_progress, GWL_STYLE) | PBS_MARQUEE);
      // SendMessageW(g_progress, PBM_SETMARQUEE, 1, 0);
      g_taskbar_list->lpVtbl->SetProgressState(g_taskbar_list, g_window, TBPF_INDETERMINATE);
    } else if ((int)wparam == (int)lparam) {
      SendMessageW(g_progress, PBM_SETPOS, 0, 0);
      g_taskbar_list->lpVtbl->SetProgressState(g_taskbar_list, g_window, TBPF_NOPROGRESS);
    } else {
      if (wparam == 0) {
        // SendMessageW(g_progress, PBM_SETMARQUEE, 0, 0);
        // SetWindowLongPtrW(g_progress, GWL_STYLE, GetWindowLongPtrW(g_progress, GWL_STYLE) & ~PBS_MARQUEE);
        SendMessageW(g_progress, PBM_SETRANGE32, 0, lparam);
        g_taskbar_list->lpVtbl->SetProgressState(g_taskbar_list, g_window, TBPF_NORMAL);
      }
      SendMessageW(g_progress, PBM_SETPOS, wparam, 0);
      g_taskbar_list->lpVtbl->SetProgressValue(g_taskbar_list, g_window, (ULONGLONG)wparam, (ULONGLONG)lparam);
    }
    break;
  case WM_CHANGE_STATE: {
    enum sr_state const s = set_state((enum sr_state)wparam);
    EnableWindow(g_model_group, s == sr_ready || s == sr_completed);
    EnableWindow(g_model_preset_combo_box, s == sr_ready || s == sr_completed);
    EnableWindow(g_model_rgb_label, s == sr_ready || s == sr_completed);
    EnableWindow(g_model_rgb_combo_box, s == sr_ready || s == sr_completed);
    EnableWindow(g_model_alpha_label, s == sr_ready || s == sr_completed);
    EnableWindow(g_model_alpha_combo_box, s == sr_ready || s == sr_completed);
    EnableWindow(g_input_image_label, s == sr_ready || s == sr_completed);
    EnableWindow(g_input_image_edit, s == sr_ready || s == sr_completed);
    EnableWindow(g_input_image_browse_button, s == sr_ready || s == sr_completed);
    EnableWindow(g_device_label, s == sr_ready || s == sr_completed);
    EnableWindow(g_device_combo_box, s == sr_ready || s == sr_completed);
    EnableWindow(g_progress, s == sr_processing);
    EnableWindow(g_progress_description, s == sr_processing);
    EnableWindow(g_start_button, s == sr_ready || s == sr_processing || s == sr_completed);
    SetWindowTextW(g_start_button, s == sr_processing ? g_abort_button_text : g_start_button_text);
    EnableWindow(g_save_button, s == sr_completed);

  } break;
  default:
    return DefWindowProcW(window, msg, wparam, lparam);
  }
  return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
  (void)hPrevInstance;
  (void)lpCmdLine;
  ov_init();

  CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

  set_hinstance(hInstance);
  mtx_init(&g_mtx, mtx_plain);

  ATOM atom = 0;

  g_ort = OrtGetApiBase()->GetApi(ORT_API_VERSION);
  if (!g_ort) {
    MessageBoxW(NULL, L"failed to get onnxruntime api.", L"Error", MB_ICONERROR);
    goto cleanup;
  }

  static wchar_t const window_class_name[] = L"sr";
  atom = RegisterClassExW(&(WNDCLASSEXW){
      .cbSize = sizeof(WNDCLASSEXW),
      .style = CS_HREDRAW | CS_VREDRAW,
      .lpfnWndProc = window_proc,
      .hInstance = hInstance,
      .hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512)),
      .hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1),
      .lpszClassName = window_class_name,
  });
  if (!atom) {
    MessageBoxW(NULL, L"failed to register window class.", L"Error", MB_ICONERROR);
    goto cleanup;
  }

  g_window = CreateWindowExW(0,
                             window_class_name,
                             L"SR",
                             WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT,
                             CW_USEDEFAULT,
                             CW_USEDEFAULT,
                             CW_USEDEFAULT,
                             NULL,
                             NULL,
                             hInstance,
                             NULL);
  if (g_window == NULL) {
    MessageBoxW(NULL, L"failed to create window.", L"Error", MB_ICONERROR);
    goto cleanup;
  }

  ShowWindow(g_window, nCmdShow);
  UpdateWindow(g_window);

  MSG msg;
  while (GetMessageW(&msg, NULL, 0, 0)) {
    if (!IsDialogMessageW(g_window, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
cleanup:
  if (g_window) {
    DestroyWindow(g_window);
    g_window = NULL;
  }
  if (atom) {
    UnregisterClassW(window_class_name, hInstance);
    atom = 0;
  }
  mtx_destroy(&g_mtx);
  CoUninitialize();
  ov_exit();
  return 0;
}
