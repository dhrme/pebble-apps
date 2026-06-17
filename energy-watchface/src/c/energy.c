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

// Price tier of a single value within today's range: 0 cheap / 1 normal / 2 pricey.
static int tier_of(int m) {
  if (s_max_m <= s_min_m) return 1;
  int t = (m - s_min_m) * 100 / (s_max_m - s_min_m);
  if (t < 34) return 0;
  if (t < 67) return 1;
  return 2;
}
static int  price_level(void)   { return tier_of(s_now_m); }
static GColor tier_color(int lv){ return lv == 0 ? GColorGreen : (lv == 1 ? GColorOrange : GColorRed); }

// Big clock, centered up top — the hero.
static void draw_clock(GContext *ctx, GRect b) {
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, s_time, fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49),
    GRect(0, 12, b.size.w, 54), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

// Battery (red only below 7% — the watch lasts days) + date, bottom strip.
static void draw_bottombar(GContext *ctx, GRect b, struct tm *tt) {
  BatteryChargeState bat = battery_state_service_peek();
  int pct = bat.charge_percent;

  int iw = 24, ih = 12, ix = 8, iy = 200;
  graphics_context_set_stroke_color(ctx, GColorLightGray);
  graphics_draw_round_rect(ctx, GRect(ix, iy, iw, ih), 2);
  graphics_context_set_fill_color(ctx, GColorLightGray);
  graphics_fill_rect(ctx, GRect(ix + iw, iy + 3, 2, ih - 6), 0, GCornerNone);
  int fw = ((iw - 4) * pct) / 100;
  if (fw < 1 && pct > 0) fw = 1;
  GColor fc = (pct <= 7) ? GColorRed : GColorGreen;
  graphics_context_set_fill_color(ctx, fc);
  graphics_fill_rect(ctx, GRect(ix + 2, iy + 2, fw, ih - 4), 0, GCornerNone);

  char bs[8];
  snprintf(bs, sizeof bs, "%d%%", pct);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, bs, fonts_get_system_font(FONT_KEY_GOTHIC_14),
    GRect(ix + iw + 8, iy - 3, 44, 18), GTextOverflowModeFill, GTextAlignmentLeft, NULL);

  char date[16];
  strftime(date, sizeof date, "%a %e %b", tt);
  graphics_context_set_text_color(ctx, GColorLightGray);
  graphics_draw_text(ctx, date, fonts_get_system_font(FONT_KEY_GOTHIC_14),
    GRect(b.size.w - 108, iy - 3, 100, 18), GTextOverflowModeFill, GTextAlignmentRight, NULL);
}

static void draw_loading(GContext *ctx, GRect b) {
  const char *msg = s_err[0] ? s_err : "Loading…";
  graphics_context_set_text_color(ctx, GColorLightGray);
  graphics_draw_text(ctx, msg, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
    GRect(8, 110, b.size.w - 16, 60), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

// 180-degree rate dial: one arc segment per hour, colored by its price tier
// (green cheap -> red peak), a bead at the current hour, and the live price in
// the bowl. The day's shape — cheap midday valley, evening peak — reads at a glance.
static void draw_gauge(GContext *ctx, GRect b) {
  const int cx = b.size.w / 2, cy = 150, R = 70, th = 14;
  GRect band = GRect(cx - R, cy - R, 2 * R, 2 * R);
  int n = s_count; if (n < 1) n = 1;

  for (int i = 0; i < n; i++) {
    int a0 = 270 + (i * 180) / n;
    int a1 = 270 + ((i + 1) * 180) / n;
    if (n > 1) a1 -= 1;                       // hairline gap between hours
    graphics_context_set_fill_color(ctx, tier_color(tier_of(s_series[i])));
    graphics_fill_radial(ctx, band, GOvalScaleModeFitCircle, th,
      DEG_TO_TRIGANGLE(a0), DEG_TO_TRIGANGLE(a1));
  }

  // bead at the current hour, on the mid-line of the band
  int an = 270 + ((2 * s_now_index + 1) * 90) / n;
  int br = R - th / 2;
  GPoint bp = gpoint_from_polar(GRect(cx - br, cy - br, 2 * br, 2 * br),
    GOvalScaleModeFitCircle, DEG_TO_TRIGANGLE(an));
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, bp, 6);
  graphics_context_set_fill_color(ctx, tier_color(price_level()));
  graphics_fill_circle(ctx, bp, 3);

  // live price + tier word in the bowl
  int lv = price_level();
  char pb[12];
  fmt_ct(s_now_m, pb, sizeof pb);
  graphics_context_set_text_color(ctx, tier_color(lv));
  graphics_draw_text(ctx, pb, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
    GRect(0, 106, b.size.w, 32), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  const char *word = lv == 0 ? "cheap" : (lv == 1 ? "normal" : "pricey");
  char sub[24];
  snprintf(sub, sizeof sub, "ct/kWh · %s", word);
  graphics_context_set_text_color(ctx, GColorLightGray);
  graphics_draw_text(ctx, sub, fonts_get_system_font(FONT_KEY_GOTHIC_14),
    GRect(0, 138, b.size.w, 18), GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  // hour labels at the two ends of the arc
  char lh[6], rh[6];
  snprintf(lh, sizeof lh, "%02d", s_start_hour % 24);
  snprintf(rh, sizeof rh, "%02d", (s_start_hour + n - 1) % 24);
  graphics_context_set_text_color(ctx, GColorDarkGray);
  graphics_draw_text(ctx, lh, fonts_get_system_font(FONT_KEY_GOTHIC_14),
    GRect(4, 156, 28, 16), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  graphics_draw_text(ctx, rh, fonts_get_system_font(FONT_KEY_GOTHIC_14),
    GRect(b.size.w - 32, 156, 28, 16), GTextOverflowModeFill, GTextAlignmentRight, NULL);
}

static void canvas_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  time_t now = time(NULL);
  struct tm *tt = localtime(&now);
  strftime(s_time, sizeof s_time, clock_is_24h_style() ? "%H:%M" : "%I:%M", tt);

  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  draw_clock(ctx, b);
  draw_bottombar(ctx, b, tt);

  if (!s_have_data) { draw_loading(ctx, b); return; }
  draw_gauge(ctx, b);
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
  window_set_background_color(s_window, GColorBlack);
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
