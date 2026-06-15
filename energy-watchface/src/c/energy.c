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

static char s_time[8];

static void fmt_ct(int m, char *buf, size_t n) {
  int neg = m < 0;
  int a = neg ? -m : m;
  snprintf(buf, n, "%s%d.%d", neg ? "-" : "", a / 10, a % 10);
}

// Coarse low/medium/high band for the current price within today's range.
static int price_level(void) {
  if (s_max_m <= s_min_m) return 1;
  int t = (s_now_m - s_min_m) * 100 / (s_max_m - s_min_m);
  if (t < 34) return 0;
  if (t < 67) return 1;
  return 2;
}
static GColor level_color(int lv) { return lv == 0 ? GColorGreen : (lv == 1 ? GColorOrange : GColorRed); }
static GColor level_tint(int lv)  { return lv == 0 ? GColorMintGreen : (lv == 1 ? GColorPastelYellow : GColorMelon); }

static void draw_loading(GContext *ctx, GRect b, int y) {
  const char *msg = s_err[0] ? s_err : "Loading…";
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, msg, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
    GRect(8, y, b.size.w - 16, 60), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

// Shared top row: date (left) + iPhone-style battery (right).
static void draw_topbar(GContext *ctx, GRect b, struct tm *tt) {
  char date[16];
  strftime(date, sizeof date, "%a %e %b", tt);
  graphics_context_set_text_color(ctx, GColorDarkGray);
  graphics_draw_text(ctx, date, fonts_get_system_font(FONT_KEY_GOTHIC_14),
    GRect(8, 1, 110, 16), GTextOverflowModeFill, GTextAlignmentLeft, NULL);

  BatteryChargeState bat = battery_state_service_peek();
  int pct = bat.charge_percent;
  char bs[8];
  snprintf(bs, sizeof bs, "%d%%", pct);
  int iw = 22, ih = 11, ix = b.size.w - 12 - iw, iy = 3;
  graphics_context_set_text_color(ctx, GColorDarkGray);
  graphics_draw_text(ctx, bs, fonts_get_system_font(FONT_KEY_GOTHIC_14),
    GRect(ix - 64, 1, 60, 16), GTextOverflowModeFill, GTextAlignmentRight, NULL);
  graphics_context_set_stroke_color(ctx, GColorDarkGray);
  graphics_draw_round_rect(ctx, GRect(ix, iy, iw, ih), 2);
  graphics_context_set_fill_color(ctx, GColorDarkGray);
  graphics_fill_rect(ctx, GRect(ix + iw, iy + 3, 2, ih - 6), 0, GCornerNone);
  int fw = ((iw - 4) * pct) / 100;
  if (fw < 1 && pct > 0) fw = 1;
  GColor fc = pct <= 20 ? GColorRed : (pct <= 40 ? GColorOrange : GColorGreen);
  graphics_context_set_fill_color(ctx, fc);
  graphics_fill_rect(ctx, GRect(ix + 2, iy + 2, fw, ih - 4), 0, GCornerNone);
}

static void draw_clock(GContext *ctx, GRect b, int y) {
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, s_time, fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS),
    GRect(0, y, b.size.w, 48), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

// LOW | MED | HIGH split scale; the current band is filled + outlined.
static void draw_level_scale(GContext *ctx, GRect b, int lv) {
  const char *labs[3] = {"LOW", "MED", "HIGH"};
  int y = 96, h = 40, x0 = 16, gap = 4;
  int sw = (b.size.w - 2 * x0 - 2 * gap) / 3;
  for (int i = 0; i < 3; i++) {
    int x = x0 + i * (sw + gap);
    bool active = (i == lv);
    graphics_context_set_fill_color(ctx, active ? level_color(i) : level_tint(i));
    graphics_fill_rect(ctx, GRect(x, y, sw, h), 5, GCornersAll);
    if (active) {
      graphics_context_set_stroke_color(ctx, GColorBlack);
      graphics_draw_round_rect(ctx, GRect(x, y, sw, h), 5);
    }
    graphics_context_set_text_color(ctx, active ? GColorWhite : GColorDarkGray);
    graphics_draw_text(ctx, labs[i], fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
      GRect(x, y + 9, sw, 22), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  }
}

// Current ct + the next cheap window.
static void draw_bottom(GContext *ctx, GRect b) {
  char pb[12], now[28];
  fmt_ct(s_now_m, pb, sizeof pb);
  snprintf(now, sizeof now, "%s ct/kWh now", pb);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, now, fonts_get_system_font(FONT_KEY_GOTHIC_18),
    GRect(4, 156, b.size.w - 8, 22), GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  int hu = s_best_start - s_now_index;
  int bs = (s_start_hour + s_best_start) % 24;
  char l1[28], l2[20];
  if (hu <= 0) { snprintf(l1, sizeof l1, "Cheapest now"); snprintf(l2, sizeof l2, "right now"); }
  else         { snprintf(l1, sizeof l1, "Cheap %02d:00", bs); snprintf(l2, sizeof l2, "in %dh", hu); }
  graphics_context_set_text_color(ctx, GColorDarkGreen);
  graphics_draw_text(ctx, l1, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
    GRect(4, 182, b.size.w - 8, 22), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  graphics_context_set_text_color(ctx, GColorDarkGray);
  graphics_draw_text(ctx, l2, fonts_get_system_font(FONT_KEY_GOTHIC_14),
    GRect(4, 205, b.size.w - 8, 18), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

static void canvas_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  time_t now = time(NULL);
  struct tm *tt = localtime(&now);
  strftime(s_time, sizeof s_time, clock_is_24h_style() ? "%H:%M" : "%I:%M", tt);

  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  draw_topbar(ctx, b, tt);
  draw_clock(ctx, b, 26);

  if (!s_have_data) { draw_loading(ctx, b, 100); return; }

  draw_level_scale(ctx, b, price_level());
  draw_bottom(ctx, b);
}

static void request_refresh(void) {
  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) == APP_MSG_OK) {
    dict_write_uint8(out, MESSAGE_KEY_Refresh, 1);
    app_message_outbox_send();
  }
}

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

static void tick_handler(struct tm *tick_time, TimeUnits units) {
  layer_mark_dirty(s_canvas);
  if (tick_time->tm_min == 0) request_refresh();
}

static void battery_handler(BatteryChargeState charge) {
  layer_mark_dirty(s_canvas);
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
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  app_message_register_inbox_received(in_received);
  app_message_open(512, 64);
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  battery_state_service_subscribe(battery_handler);
  window_stack_push(s_window, true);
  request_refresh();
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
