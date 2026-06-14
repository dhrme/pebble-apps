#include <pebble.h>

static Window *s_window;
static Layer  *s_canvas;

// ---- state received from the phone (pkjs) ----
static bool    s_have_data = false;
static char    s_err[40]   = "";
static int     s_count     = 0;
static int     s_now_m     = 0;   // current price  * 1000  (EUR/kWh)
static int     s_min_m     = 0;   // window minimum * 1000
static int     s_max_m     = 0;   // window maximum * 1000
static int     s_now_hour  = 0;   // local hour 0..23
static int     s_start_hour= 0;   // local hour of series[0]
static int     s_now_index = 0;   // index of the current-hour bar (past bars before it)
static int     s_best_start= 0;   // index into series of cheapest block
static int     s_best_len  = 0;   // length of cheapest block (hours)
static int     s_best_avg_m= 0;   // cheapest-block average * 1000
static int16_t s_series[24];      // hourly prices * 1000, series[0] = now

// Format price*1000 (milli-euro/kWh = tenths of a cent) into "13.0" (ct/kWh).
static void fmt_ct(int m, char *buf, size_t n) {
  int neg = m < 0;
  int a = neg ? -m : m;
  int whole = a / 10;                      // whole cents
  int tenth = a % 10;                      // tenths of a cent
  snprintf(buf, n, "%s%d.%d", neg ? "-" : "", whole, tenth);
}

// Map a price (*1000) to a colour: green (cheap) -> yellow -> red (expensive).
static GColor price_color(int v_m) {
  int lo = s_min_m, hi = s_max_m;
  int t = (hi > lo) ? ((v_m - lo) * 255) / (hi - lo) : 128;
  if (t < 0) t = 0;
  if (t > 255) t = 255;
  int r, g;
  if (t < 128) { r = (t * 255) / 128; g = 200; }
  else { r = 255; g = 200 - ((t - 128) * 200) / 127; }
  return GColorFromRGB(r, g, 0);
}

static int bar_height(int v_m, int gh) {
  int t = (s_max_m > s_min_m) ? ((v_m - s_min_m) * 100) / (s_max_m - s_min_m) : 50;
  if (t < 0) t = 0;
  if (t > 100) t = 100;
  return 5 + (t * (gh - 5)) / 100;        // at least 5px so flat days stay visible
}

static void canvas_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  if (!s_have_data) {
    const char *msg = s_err[0] ? s_err : "Loading…";
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, msg, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
      GRect(4, 92, b.size.w - 8, 40), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    return;
  }

  // ---- current price (big, colour-coded) ----
  char price_buf[16];
  fmt_ct(s_now_m, price_buf, sizeof price_buf);
  graphics_context_set_text_color(ctx, price_color(s_now_m));
  graphics_draw_text(ctx, price_buf, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD),
    GRect(0, 2, b.size.w, 48), GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  char sub[32];
  snprintf(sub, sizeof sub, "ct/kWh   now %02d:00", s_now_hour);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, sub, fonts_get_system_font(FONT_KEY_GOTHIC_18),
    GRect(0, 50, b.size.w, 22), GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  // ---- forward-24h bar graph ----
  int gx = 6, gw = b.size.w - 12;          // 188 on emery
  int gtop = 78, gbot = 166, gh = gbot - gtop;
  int n = s_count > 0 ? s_count : 1;
  int bw = gw / n;
  if (bw < 2) bw = 2;
  int fillw = bw - 1 < 1 ? 1 : bw - 1;

  graphics_context_set_stroke_color(ctx, GColorLightGray);
  graphics_draw_line(ctx, GPoint(gx, gbot), GPoint(gx + gw, gbot));

  for (int i = 0; i < s_count; i++) {
    int bh = bar_height(s_series[i], gh);
    int x = gx + i * bw;
    GColor bc = (i < s_now_index) ? GColorLightGray : price_color(s_series[i]);
    graphics_context_set_fill_color(ctx, bc);
    graphics_fill_rect(ctx, GRect(x, gbot - bh, fillw, bh), 0, GCornerNone);
  }

  // outline the "now" bar
  if (s_count > s_now_index) {
    int bh = bar_height(s_series[s_now_index], gh);
    int nx = gx + s_now_index * bw;
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_draw_rect(ctx, GRect(nx, gbot - bh, fillw, bh));
  }

  // green marker under the cheapest block
  if (s_best_len > 0) {
    int x0 = gx + s_best_start * bw;
    int x1 = gx + (s_best_start + s_best_len) * bw - 1;
    graphics_context_set_fill_color(ctx, GColorGreen);
    graphics_fill_rect(ctx, GRect(x0, gbot + 1, x1 - x0, 3), 0, GCornerNone);
  }

  // axis labels: relative offsets from now (now, +6, +12, +18) + total at right
  graphics_context_set_text_color(ctx, GColorDarkGray);
  graphics_context_set_stroke_color(ctx, GColorDarkGray);
  GFont axf = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  int rels[] = {0, 6, 12, 18};
  for (int k = 0; k < 4; k++) {
    int off = rels[k];
    int i = s_now_index + off;
    if (i >= s_count) break;
    if (off > 0 && i > s_count - 1 - 3) continue;   // leave room for the right-end label
    int tx = gx + i * bw + fillw / 2;
    graphics_draw_line(ctx, GPoint(tx, gbot), GPoint(tx, gbot + 3));
    char lab[8];
    if (off == 0) {
      strncpy(lab, "now", sizeof lab);
    } else {
      snprintf(lab, sizeof lab, "+%d", off);
    }
    graphics_draw_text(ctx, lab, axf,
      GRect(tx - 16, gbot + 2, 32, 16), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  }
  char rlab[12];
  snprintf(rlab, sizeof rlab, "+%dh", s_count - 1 - s_now_index);
  graphics_draw_text(ctx, rlab, axf,
    GRect(gx + gw - 48, gbot + 2, 48, 16), GTextOverflowModeFill, GTextAlignmentRight, NULL);

  // ---- footer: cheapest block ----
  graphics_context_set_text_color(ctx, GColorBlack);
  int bs = (s_start_hour + s_best_start) % 24;
  int be = (s_start_hour + s_best_start + s_best_len) % 24;
  char f1[40];
  snprintf(f1, sizeof f1, "Best %dh   %02d:00-%02d:00", s_best_len, bs, be);
  graphics_draw_text(ctx, f1, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
    GRect(4, 188, b.size.w - 8, 22), GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  char avg_buf[16];
  fmt_ct(s_best_avg_m, avg_buf, sizeof avg_buf);
  char f2[28];
  snprintf(f2, sizeof f2, "avg %s ct/kWh", avg_buf);
  graphics_draw_text(ctx, f2, fonts_get_system_font(FONT_KEY_GOTHIC_18),
    GRect(4, 208, b.size.w - 8, 20), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

// ---- AppMessage ----
static void in_received(DictionaryIterator *iter, void *context) {
  Tuple *err = dict_find(iter, MESSAGE_KEY_Err);
  if (err) {
    strncpy(s_err, err->value->cstring, sizeof s_err - 1);
    s_err[sizeof s_err - 1] = '\0';
    s_have_data = false;
    layer_mark_dirty(s_canvas);
    return;
  }

  Tuple *series = dict_find(iter, MESSAGE_KEY_Series);
  Tuple *count  = dict_find(iter, MESSAGE_KEY_Count);
  if (!series || !count) return;

  s_count = count->value->int32;
  if (s_count > 24) s_count = 24;
  int npairs = series->length / 2;
  if (npairs < s_count) s_count = npairs;
  uint8_t *d = series->value->data;
  for (int i = 0; i < s_count; i++) {
    s_series[i] = (int16_t)(d[i * 2] | (d[i * 2 + 1] << 8));
  }

  Tuple *t;
  if ((t = dict_find(iter, MESSAGE_KEY_NowPriceM))) s_now_m      = t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_MinM)))      s_min_m      = t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_MaxM)))      s_max_m      = t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_NowHour)))   s_now_hour   = t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_StartHour))) s_start_hour = t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_NowIndex)))  s_now_index  = t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_BestStart))) s_best_start = t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_BestLen)))   s_best_len   = t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_BestAvgM)))  s_best_avg_m = t->value->int32;

  s_err[0] = '\0';
  s_have_data = true;
  layer_mark_dirty(s_canvas);
}

static void request_refresh(void) {
  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) == APP_MSG_OK) {
    dict_write_uint8(out, MESSAGE_KEY_Refresh, 1);
    app_message_outbox_send();
  }
}

static void select_click(ClickRecognizerRef recognizer, void *context) {
  s_have_data = false;
  s_err[0] = '\0';
  layer_mark_dirty(s_canvas);
  request_refresh();
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *window) {
  layer_destroy(s_canvas);
}

static void init(void) {
  s_window = window_create();
  window_set_background_color(s_window, GColorWhite);
  window_set_click_config_provider(s_window, click_config_provider);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  app_message_register_inbox_received(in_received);
  app_message_open(512, 64);
  window_stack_push(s_window, true);
}

static void deinit(void) {
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
