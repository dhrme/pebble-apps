#include <pebble.h>

// Kevinimal — minimal watchface. Time floats centre; data sits in the corners.
// TL: day + DD/MM   TR: 3-block battery   BL: rain%   BR: temp now + lo/hi.
// Theme (colour scheme) is picked in Settings and persisted.

#define W 200
#define PERSIST_THEME 1

// One scheme per theme. Kept in sync with THEMES in src/pkjs/index.js.
typedef struct {
  uint8_t bg;       // background
  uint8_t primary;  // time / day / temp now
  uint8_t sub;      // date / hi-lo / rain
  uint8_t accent;   // rain drop (the single splash of colour)
  uint8_t batt_off; // empty battery block outline
} Theme;

static const Theme THEMES[] = {
  // 0 — Kevin (original dark)
  { GColorBlackARGB8, GColorWhiteARGB8, GColorLightGrayARGB8, GColorPictonBlueARGB8,   GColorDarkGrayARGB8 },
  // 1 — Daylight (light)
  { GColorWhiteARGB8, GColorBlackARGB8, GColorDarkGrayARGB8,  GColorCobaltBlueARGB8,   GColorLightGrayARGB8 },
  // 2 — Amber
  { GColorBlackARGB8, GColorWhiteARGB8, GColorLightGrayARGB8, GColorChromeYellowARGB8, GColorDarkGrayARGB8 },
  // 3 — Mint
  { GColorBlackARGB8, GColorWhiteARGB8, GColorLightGrayARGB8, GColorMintGreenARGB8,    GColorDarkGrayARGB8 },
};
#define N_THEMES (int)(sizeof(THEMES) / sizeof(THEMES[0]))

static int s_theme = 0;
static inline GColor gc(uint8_t argb) { return (GColor){ .argb = argb }; }

static Window *s_window;
static Layer  *s_layer;

static GFont f_time, f_day, f_cur, f_sub;

static char s_time[8];
static char s_day[5];
static char s_date[8];
static char s_cur[8]   = "--°";
static char s_hilo[12] = "-- / --";
static char s_rain[6]  = "--%";
static int  s_batt_pct = 0;

static void upcase(char *s) {
  for (; *s; s++) if (*s >= 'a' && *s <= 'z') *s -= ('a' - 'A');
}

static void update_proc(Layer *layer, GContext *ctx) {
  const Theme *t = &THEMES[s_theme];

  graphics_context_set_fill_color(ctx, gc(t->bg));
  graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);

  // --- TL: day + date ---
  graphics_context_set_text_color(ctx, gc(t->primary));
  graphics_draw_text(ctx, s_day, f_day, GRect(14, 6, 110, 26),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  graphics_context_set_text_color(ctx, gc(t->sub));
  graphics_draw_text(ctx, s_date, f_sub, GRect(14, 36, 110, 20),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  // --- TR: 3-block battery (fills left-to-right) ---
  int filled = s_batt_pct > 66 ? 3 : s_batt_pct > 33 ? 2 : s_batt_pct > 5 ? 1 : 0;
  for (int i = 0; i < 3; i++) {
    GRect r = GRect(139 + i * 17, 14, 13, 13);
    if (i < filled) {
      graphics_context_set_fill_color(ctx, gc(t->primary));
      graphics_fill_rect(ctx, r, 0, GCornerNone);
    } else {
      graphics_context_set_stroke_color(ctx, gc(t->batt_off));
      graphics_context_set_stroke_width(ctx, 1);
      graphics_draw_rect(ctx, r);
    }
  }

  // --- centre: time ---
  graphics_context_set_text_color(ctx, gc(t->primary));
  graphics_draw_text(ctx, s_time, f_time, GRect(0, 78, W, 76),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  // --- BL: rain drop + % (baseline-aligned with the BR lo/hi row) ---
  graphics_context_set_fill_color(ctx, gc(t->accent));
  graphics_fill_circle(ctx, GPoint(20, 214), 4);
  GPathInfo tip = { 3, (GPoint[]){ {20, 204}, {15, 214}, {25, 214} } };
  GPath *drop = gpath_create(&tip);
  gpath_draw_filled(ctx, drop);
  gpath_destroy(drop);
  graphics_context_set_text_color(ctx, gc(t->sub));
  graphics_draw_text(ctx, s_rain, f_sub, GRect(32, 206, 70, 18),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  // --- BR: temp now + lo/hi ---
  graphics_context_set_text_color(ctx, gc(t->primary));
  graphics_draw_text(ctx, s_cur, f_cur, GRect(96, 172, 90, 26),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
  graphics_context_set_text_color(ctx, gc(t->sub));
  graphics_draw_text(ctx, s_hilo, f_sub, GRect(96, 206, 90, 18),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
}

static void tick(struct tm *t, TimeUnits units) {
  strftime(s_time, sizeof(s_time), clock_is_24h_style() ? "%H:%M" : "%l:%M", t);
  strftime(s_day,  sizeof(s_day),  "%a", t);
  strftime(s_date, sizeof(s_date), "%d/%m", t);
  upcase(s_day);
  // %l pads the hour with a leading space in 12h mode — strip it
  if (s_time[0] == ' ') memmove(s_time, s_time + 1, strlen(s_time));
  layer_mark_dirty(s_layer);
}

static void batt(BatteryChargeState c) {
  s_batt_pct = c.charge_percent;
  layer_mark_dirty(s_layer);
}

static void inbox(DictionaryIterator *it, void *context) {
  Tuple *tn = dict_find(it, MESSAGE_KEY_TempNow);
  Tuple *th = dict_find(it, MESSAGE_KEY_TempHi);
  Tuple *tl = dict_find(it, MESSAGE_KEY_TempLo);
  Tuple *rn = dict_find(it, MESSAGE_KEY_Rain);
  Tuple *tm = dict_find(it, MESSAGE_KEY_Theme);
  if (tn) snprintf(s_cur, sizeof(s_cur), "%d°", (int)tn->value->int32);
  if (th && tl)
    snprintf(s_hilo, sizeof(s_hilo), "%d / %d", (int)tl->value->int32, (int)th->value->int32);
  if (rn) snprintf(s_rain, sizeof(s_rain), "%d%%", (int)rn->value->int32);
  if (tm) {
    int n = (int)tm->value->int32;
    if (n >= 0 && n < N_THEMES) {
      s_theme = n;
      persist_write_int(PERSIST_THEME, n);
      window_set_background_color(s_window, gc(THEMES[n].bg));
    }
  }
  layer_mark_dirty(s_layer);
}

static void init(void) {
  if (persist_exists(PERSIST_THEME)) {
    int n = persist_read_int(PERSIST_THEME);
    if (n >= 0 && n < N_THEMES) s_theme = n;
  }

  f_time = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_TIME_64));
  f_day  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DAY_20));
  f_cur  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_CUR_22));
  f_sub  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_SUB_14));

  s_window = window_create();
  window_set_background_color(s_window, gc(THEMES[s_theme].bg));
  Layer *root = window_get_root_layer(s_window);
  s_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_layer, update_proc);
  layer_add_child(root, s_layer);
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick);
  battery_state_service_subscribe(batt);
  batt(battery_state_service_peek());
  time_t now = time(NULL);
  tick(localtime(&now), MINUTE_UNIT);

  app_message_register_inbox_received(inbox);
  app_message_open(256, 64);
}

static void deinit(void) {
  layer_destroy(s_layer);
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
