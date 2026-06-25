#include <pebble.h>

// Bauhaus Blocks watchface — five stacked colour bands, all-left aligned.
// Rows: day / date / big time / battery / temperature (now + hi/lo).
// Theme (band palette) is picked in Settings and persisted.

#define W 200
#define PERSIST_THEME 1

// One palette per theme. Bands are ordered day/date/time/batt/temp; `ink`
// is the text colour drawn over each band; `glyph` paints the battery meter.
typedef struct {
  uint8_t band[5];
  uint8_t ink[5];
  uint8_t glyph;
} Theme;

static const Theme THEMES[] = {
  // 0 — Bauhaus (original)
  { { GColorYellowARGB8, GColorTiffanyBlueARGB8, GColorWhiteARGB8, GColorBlueARGB8, GColorOrangeARGB8 },
    { GColorBlackARGB8,  GColorBlackARGB8,       GColorBlackARGB8, GColorWhiteARGB8, GColorBlackARGB8 },
    GColorWhiteARGB8 },
  // 1 — Noir (greyscale)
  { { GColorWhiteARGB8, GColorLightGrayARGB8, GColorBlackARGB8, GColorDarkGrayARGB8, GColorWhiteARGB8 },
    { GColorBlackARGB8, GColorBlackARGB8,     GColorWhiteARGB8, GColorWhiteARGB8,    GColorBlackARGB8 },
    GColorWhiteARGB8 },
  // 2 — Pop (bold)
  { { GColorFollyARGB8, GColorChromeYellowARGB8, GColorWhiteARGB8, GColorJaegerGreenARGB8, GColorElectricUltramarineARGB8 },
    { GColorWhiteARGB8, GColorBlackARGB8,        GColorBlackARGB8, GColorWhiteARGB8,       GColorWhiteARGB8 },
    GColorWhiteARGB8 },
  // 3 — Earth (muted)
  { { GColorBrassARGB8, GColorWindsorTanARGB8, GColorPastelYellowARGB8, GColorOxfordBlueARGB8, GColorKellyGreenARGB8 },
    { GColorBlackARGB8, GColorWhiteARGB8,      GColorBlackARGB8,        GColorWhiteARGB8,      GColorBlackARGB8 },
    GColorWhiteARGB8 },
};
#define N_THEMES (int)(sizeof(THEMES) / sizeof(THEMES[0]))

static int s_theme = 0;
static inline GColor gc(uint8_t argb) { return (GColor){ .argb = argb }; }

static Window *s_window;
static Layer  *s_layer;

static GFont f_time, f_now, f_day, f_date, f_batt, f_lbl;

static char s_time[8];
static char s_day[12];
static char s_date[16];
static char s_batt[8]  = "--%";
static int  s_batt_pct = 0;

static char s_now[8] = "--°";
static char s_hi[10] = "HI --°";
static char s_lo[10] = "LO --°";

static void upcase(char *s) {
  for (; *s; s++) if (*s >= 'a' && *s <= 'z') *s -= ('a' - 'A');
}

static void draw_band(GContext *ctx, int y, int h, GColor c) {
  graphics_context_set_fill_color(ctx, c);
  graphics_fill_rect(ctx, GRect(0, y, W, h), 0, GCornerNone);
}

static void update_proc(Layer *layer, GContext *ctx) {
  const Theme *t = &THEMES[s_theme];

  // --- colour bands ---
  draw_band(ctx, 0,   30, gc(t->band[0]));   // day
  draw_band(ctx, 30,  28, gc(t->band[1]));   // date
  draw_band(ctx, 58,  82, gc(t->band[2]));   // time
  draw_band(ctx, 140, 40, gc(t->band[3]));   // battery
  draw_band(ctx, 180, 48, gc(t->band[4]));   // temperature

  // --- day ---
  graphics_context_set_text_color(ctx, gc(t->ink[0]));
  graphics_draw_text(ctx, s_day, f_day, GRect(14, 4, 180, 26),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  // --- date ---
  graphics_context_set_text_color(ctx, gc(t->ink[1]));
  graphics_draw_text(ctx, s_date, f_date, GRect(14, 34, 180, 22),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  // --- time ---
  graphics_context_set_text_color(ctx, gc(t->ink[2]));
  graphics_draw_text(ctx, s_time, f_time, GRect(10, 62, 192, 80),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  // --- battery glyph + percent ---
  graphics_context_set_stroke_color(ctx, gc(t->glyph));
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_round_rect(ctx, GRect(14, 152, 50, 16), 2);
  graphics_context_set_fill_color(ctx, gc(t->glyph));
  graphics_fill_rect(ctx, GRect(64, 156, 3, 8), 0, GCornerNone);   // cap
  int fillw = (s_batt_pct * 44) / 100;                             // inner usable ~44px
  graphics_fill_rect(ctx, GRect(17, 155, fillw, 10), 0, GCornerNone);
  graphics_context_set_text_color(ctx, gc(t->ink[3]));
  graphics_draw_text(ctx, s_batt, f_batt, GRect(76, 150, 110, 22),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  // --- temperature: current (big, left) ---
  graphics_context_set_text_color(ctx, gc(t->ink[4]));
  graphics_draw_text(ctx, s_now, f_now, GRect(12, 181, 100, 44),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  // hi (right)
  graphics_draw_text(ctx, s_hi, f_lbl, GRect(96, 184, 90, 20),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
  // lo (right)
  graphics_draw_text(ctx, s_lo, f_lbl, GRect(96, 204, 90, 20),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
}

static void tick(struct tm *t, TimeUnits units) {
  strftime(s_time, sizeof(s_time), clock_is_24h_style() ? "%H:%M" : "%I:%M", t);
  strftime(s_day,  sizeof(s_day),  "%A", t);
  strftime(s_date, sizeof(s_date), "%d %B", t);
  upcase(s_day);
  upcase(s_date);
  layer_mark_dirty(s_layer);
}

static void batt(BatteryChargeState c) {
  s_batt_pct = c.charge_percent;
  snprintf(s_batt, sizeof(s_batt), "%d%%", s_batt_pct);
  layer_mark_dirty(s_layer);
}

static void inbox(DictionaryIterator *it, void *context) {
  Tuple *tn = dict_find(it, MESSAGE_KEY_TempNow);
  Tuple *th = dict_find(it, MESSAGE_KEY_TempHi);
  Tuple *tl = dict_find(it, MESSAGE_KEY_TempLo);
  Tuple *tm = dict_find(it, MESSAGE_KEY_Theme);
  if (tn) snprintf(s_now, sizeof(s_now), "%d°", (int)tn->value->int32);
  if (th) snprintf(s_hi, sizeof(s_hi), "HI %d°", (int)th->value->int32);
  if (tl) snprintf(s_lo, sizeof(s_lo), "LO %d°", (int)tl->value->int32);
  if (tm) {
    int n = (int)tm->value->int32;
    if (n >= 0 && n < N_THEMES) {
      s_theme = n;
      persist_write_int(PERSIST_THEME, n);
    }
  }
  layer_mark_dirty(s_layer);
}

static void init(void) {
  if (persist_exists(PERSIST_THEME)) {
    int n = persist_read_int(PERSIST_THEME);
    if (n >= 0 && n < N_THEMES) s_theme = n;
  }

  f_time = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_TIME_58));
  f_now  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_NOW_30));
  f_day  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DAY_18));
  f_date = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DATE_15));
  f_batt = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_BATT_16));
  f_lbl  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_LBL_14));

  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
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
