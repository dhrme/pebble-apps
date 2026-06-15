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

// ---- watchface layout state ----
#define LAYOUT_A 0                 // clock hero + price graph
#define LAYOUT_C 1                 // price-coloured frame + slim graph
#define LAYOUT_E 2                 // minimal: big clock, no graph
#define NUM_LAYOUTS 3
#define PKEY_LAYOUT 1
#define PKEY_TAP    2
static int  s_layout = LAYOUT_A;
static bool s_tap_enabled = true;
static char s_time[8];             // "14:32"

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

// "Loading…" / error placeholder, centred in the energy region below the clock.
static void draw_loading(GContext *ctx, GRect b, int y) {
  const char *msg = s_err[0] ? s_err : "Loading…";
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, msg, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
    GRect(8, y, b.size.w - 16, 60), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

// Forward-24h bar graph in [gx..gx+gw] x [gtop..gbot]; axis labels optional.
static void draw_graph(GContext *ctx, GRect b, int gx, int gw, int gtop, int gbot, bool show_axis) {
  (void)b;
  int gh = gbot - gtop;
  int n  = s_count > 0 ? s_count : 1;
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

  if (!show_axis) return;

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
}

// One-line cheapest-block summary, centred at y, drawn in black.
static void draw_best_line(GContext *ctx, GRect b, int y) {
  if (s_best_len <= 0) return;
  int bs = (s_start_hour + s_best_start) % 24;
  int be = (s_start_hour + s_best_start + s_best_len) % 24;
  char ab[16];
  fmt_ct(s_best_avg_m, ab, sizeof ab);
  char f[44];
  snprintf(f, sizeof f, "Best %dh  %02d-%02d  %s ct", s_best_len, bs, be, ab);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, f, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
    GRect(4, y, b.size.w - 8, 22), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

// ---- Layout A: clock hero, then a price line + full graph + best block ----
static void draw_A(GContext *ctx, GRect b, struct tm *tt) {
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, s_time, fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS),
    GRect(0, -2, b.size.w, 48), GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  char date[24];
  strftime(date, sizeof date, "%a %e %B", tt);
  graphics_context_set_text_color(ctx, GColorDarkGray);
  graphics_draw_text(ctx, date, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
    GRect(0, 48, b.size.w, 22), GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  graphics_context_set_stroke_color(ctx, GColorLightGray);
  graphics_draw_line(ctx, GPoint(8, 74), GPoint(b.size.w - 8, 74));

  if (!s_have_data) { draw_loading(ctx, b, 110); return; }

  char pbuf[16];
  fmt_ct(s_now_m, pbuf, sizeof pbuf);
  char pl[28];
  snprintf(pl, sizeof pl, "%s ct/kWh", pbuf);
  graphics_context_set_text_color(ctx, price_color(s_now_m));
  graphics_draw_text(ctx, pl, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
    GRect(6, 78, b.size.w - 12, 28), GTextOverflowModeFill, GTextAlignmentLeft, NULL);

  char nowlab[16];
  snprintf(nowlab, sizeof nowlab, "now %02d:00", s_now_hour);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, nowlab, fonts_get_system_font(FONT_KEY_GOTHIC_18),
    GRect(6, 84, b.size.w - 12, 22), GTextOverflowModeFill, GTextAlignmentRight, NULL);

  draw_graph(ctx, b, 6, b.size.w - 12, 118, 180, true);
  draw_best_line(ctx, b, 206);
}

// ---- Layout C: screen border tinted by current price + slim graph ----
static void draw_C(GContext *ctx, GRect b, struct tm *tt) {
  GColor frame = s_have_data ? price_color(s_now_m) : GColorWhite;
  graphics_context_set_fill_color(ctx, frame);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(10, 10, b.size.w - 20, b.size.h - 20), 3, GCornersAll);

  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, s_time, fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS),
    GRect(10, 16, b.size.w - 20, 46), GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  char date[24];
  strftime(date, sizeof date, "%a %e %B", tt);
  graphics_context_set_text_color(ctx, GColorDarkGray);
  graphics_draw_text(ctx, date, fonts_get_system_font(FONT_KEY_GOTHIC_18),
    GRect(10, 64, b.size.w - 20, 22), GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  if (!s_have_data) { draw_loading(ctx, b, 100); return; }

  char pbuf[16];
  fmt_ct(s_now_m, pbuf, sizeof pbuf);
  char pl[28];
  snprintf(pl, sizeof pl, "%s ct/kWh", pbuf);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, pl, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
    GRect(10, 90, b.size.w - 20, 28), GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  draw_graph(ctx, b, 18, b.size.w - 36, 128, 172, false);
  draw_best_line(ctx, b, 190);
}

// ---- Layout E: minimal — big clock, full date, price + cheapest, no graph ----
static void draw_E(GContext *ctx, GRect b, struct tm *tt) {
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, s_time, fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS),
    GRect(0, 18, b.size.w, 52), GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  char date[28];
  strftime(date, sizeof date, "%A %e %B", tt);
  graphics_context_set_text_color(ctx, GColorDarkGray);
  graphics_draw_text(ctx, date, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
    GRect(0, 74, b.size.w, 24), GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  graphics_context_set_stroke_color(ctx, GColorLightGray);
  graphics_draw_line(ctx, GPoint(28, 110), GPoint(b.size.w - 28, 110));

  if (!s_have_data) { draw_loading(ctx, b, 124); return; }

  char pbuf[16];
  fmt_ct(s_now_m, pbuf, sizeof pbuf);
  char pl[28];
  snprintf(pl, sizeof pl, "%s ct/kWh", pbuf);
  graphics_context_set_text_color(ctx, price_color(s_now_m));
  graphics_draw_text(ctx, pl, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
    GRect(0, 120, b.size.w, 32), GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  graphics_context_set_text_color(ctx, GColorDarkGray);
  graphics_draw_text(ctx, "right now", fonts_get_system_font(FONT_KEY_GOTHIC_14),
    GRect(0, 152, b.size.w, 18), GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  if (s_best_len > 0) {
    int bs = (s_start_hour + s_best_start) % 24;
    int be = (s_start_hour + s_best_start + s_best_len) % 24;
    char cf[40];
    snprintf(cf, sizeof cf, "Cheapest %02d:00-%02d:00", bs, be);
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, cf, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
      GRect(4, 182, b.size.w - 8, 22), GTextOverflowModeFill, GTextAlignmentCenter, NULL);

    char ab[16];
    fmt_ct(s_best_avg_m, ab, sizeof ab);
    char av[24];
    snprintf(av, sizeof av, "avg %s ct/kWh", ab);
    graphics_context_set_text_color(ctx, GColorDarkGray);
    graphics_draw_text(ctx, av, fonts_get_system_font(FONT_KEY_GOTHIC_18),
      GRect(4, 204, b.size.w - 8, 22), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  }
}

static void canvas_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  time_t now = time(NULL);
  struct tm *tt = localtime(&now);
  strftime(s_time, sizeof s_time, clock_is_24h_style() ? "%H:%M" : "%I:%M", tt);

  switch (s_layout) {
    case LAYOUT_C: draw_C(ctx, b, tt); break;
    case LAYOUT_E: draw_E(ctx, b, tt); break;
    default:       draw_A(ctx, b, tt); break;
  }
}

static void request_refresh(void) {
  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) == APP_MSG_OK) {
    dict_write_uint8(out, MESSAGE_KEY_Refresh, 1);
    app_message_outbox_send();
  }
}

static void tap_handler(AccelAxisType axis, int32_t direction) {
  s_layout = (s_layout + 1) % NUM_LAYOUTS;
  persist_write_int(PKEY_LAYOUT, s_layout);
  layer_mark_dirty(s_canvas);
}

static void update_tap_sub(void) {
  if (s_tap_enabled) accel_tap_service_subscribe(tap_handler);
  else accel_tap_service_unsubscribe();
}

// Clay may deliver a value as an int or a numeric string; accept both.
static int tuple_as_int(Tuple *t) {
  return (t->type == TUPLE_CSTRING) ? atoi(t->value->cstring) : (int)t->value->int32;
}

// ---- AppMessage ----
static void in_received(DictionaryIterator *iter, void *context) {
  Tuple *t;
  bool cfg = false;

  if ((t = dict_find(iter, MESSAGE_KEY_CfgLayout))) {
    int v = tuple_as_int(t);
    if (v >= 0 && v < NUM_LAYOUTS) { s_layout = v; persist_write_int(PKEY_LAYOUT, s_layout); }
    cfg = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_CfgTap))) {
    s_tap_enabled = tuple_as_int(t) != 0;
    persist_write_bool(PKEY_TAP, s_tap_enabled);
    update_tap_sub();
    cfg = true;
  }

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
  if (!series || !count) {
    if (cfg) layer_mark_dirty(s_canvas);
    return;
  }

  s_count = count->value->int32;
  if (s_count > 24) s_count = 24;
  int npairs = series->length / 2;
  if (npairs < s_count) s_count = npairs;
  uint8_t *d = series->value->data;
  for (int i = 0; i < s_count; i++) {
    s_series[i] = (int16_t)(d[i * 2] | (d[i * 2 + 1] << 8));
  }

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
  layer_mark_dirty(s_canvas);                 // redraw the clock each minute
  if (tick_time->tm_min == 0) request_refresh();  // prices update hourly
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
  s_layout = persist_exists(PKEY_LAYOUT) ? persist_read_int(PKEY_LAYOUT) : LAYOUT_A;
  if (s_layout < 0 || s_layout >= NUM_LAYOUTS) s_layout = LAYOUT_A;
  s_tap_enabled = persist_exists(PKEY_TAP) ? persist_read_bool(PKEY_TAP) : true;

  s_window = window_create();
  window_set_background_color(s_window, GColorWhite);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  app_message_register_inbox_received(in_received);
  app_message_open(512, 64);
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  update_tap_sub();
  window_stack_push(s_window, true);
  request_refresh();                          // kick an initial fetch
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  accel_tap_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
