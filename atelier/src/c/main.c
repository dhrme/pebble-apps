#include <pebble.h>

#define DATE_FONT_RES RESOURCE_ID_FONT_LORA_DATE_18
#define PERSIST_THEME 1

// One palette per theme. Picked in Settings, persisted on the watch.
// Kept in sync with THEMES in src/pkjs/index.js.
typedef struct {
  uint8_t bg;        // dial background
  uint8_t ink;       // ticks, hands, centre cap
  uint8_t accent;    // 12 o'clock dot, centre pip, low-battery bar
  uint8_t date;      // date text
  uint8_t track;     // battery track
  uint8_t batt;      // battery fill (normal)
} Theme;

static const Theme THEMES[] = {
  // 0 — Atelier (original): warm orange on white
  { GColorWhiteARGB8, GColorBlackARGB8, GColorOrangeARGB8,       GColorDarkGrayARGB8,  GColorLightGrayARGB8, GColorDarkGrayARGB8 },
  // 1 — Noir: inverted, orange accent
  { GColorBlackARGB8, GColorWhiteARGB8, GColorOrangeARGB8,       GColorLightGrayARGB8, GColorDarkGrayARGB8,  GColorLightGrayARGB8 },
  // 2 — Sage: green accent on white
  { GColorWhiteARGB8, GColorBlackARGB8, GColorIslamicGreenARGB8, GColorDarkGrayARGB8,  GColorLightGrayARGB8, GColorDarkGrayARGB8 },
  // 3 — Blush: pink accent on white
  { GColorWhiteARGB8, GColorBlackARGB8, GColorFollyARGB8,        GColorDarkGrayARGB8,  GColorLightGrayARGB8, GColorDarkGrayARGB8 },
};
#define N_THEMES (int)(sizeof(THEMES) / sizeof(THEMES[0]))

static int s_theme = 0;
static inline GColor gc(uint8_t argb) { return (GColor){ .argb = argb }; }

static Window *s_window;
static Layer *s_canvas;
static GFont s_date_font;

static GPoint polar(GPoint c, int32_t angle, int r) {
  return GPoint(
    c.x + (sin_lookup(angle) * r / TRIG_MAX_RATIO),
    c.y - (cos_lookup(angle) * r / TRIG_MAX_RATIO)
  );
}

// draw text with manual letter-spacing (Pebble draw_text has no tracking)
static void draw_tracked(GContext *ctx, const char *s, GFont font, int cx, int y, int tracking) {
  GRect probe = GRect(0, 0, 200, 40);
  int total = 0, n = 0;
  for (const char *p = s; *p; p++) {
    char c[2] = { *p, 0 };
    total += graphics_text_layout_get_content_size(c, font, probe,
               GTextOverflowModeWordWrap, GTextAlignmentLeft).w;
    n++;
  }
  if (n > 1) total += tracking * (n - 1);
  int x = cx - total / 2;
  for (const char *p = s; *p; p++) {
    char c[2] = { *p, 0 };
    int w = graphics_text_layout_get_content_size(c, font, probe,
              GTextOverflowModeWordWrap, GTextAlignmentLeft).w;
    graphics_draw_text(ctx, c, font, GRect(x, y, w + 4, 30),
      GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
    x += w + tracking;
  }
}

static void canvas_update(Layer *layer, GContext *ctx) {
  const Theme *th = &THEMES[s_theme];
  GRect b = layer_get_bounds(layer);
  GPoint c = GPoint(b.size.w / 2, 100);
  const int tick_out = 84;

  graphics_context_set_fill_color(ctx, gc(th->bg));
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  // hour ticks: quarters longer + thicker
  graphics_context_set_stroke_color(ctx, gc(th->ink));
  for (int i = 0; i < 12; i++) {
    int32_t a = TRIG_MAX_ANGLE * i / 12;
    bool quarter = (i % 3 == 0);
    int inset = quarter ? 14 : 8;
    graphics_context_set_stroke_width(ctx, quarter ? 3 : 2);
    graphics_draw_line(ctx, polar(c, a, tick_out - inset), polar(c, a, tick_out));
  }

  // warm accent dot just inside 12
  graphics_context_set_fill_color(ctx, gc(th->accent));
  graphics_fill_circle(ctx, polar(c, 0, tick_out - 18), 4);

  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  int32_t min_a = TRIG_MAX_ANGLE * t->tm_min / 60;
  int32_t hr_a  = TRIG_MAX_ANGLE * ((t->tm_hour % 12) * 60 + t->tm_min) / 720;

  // hands
  graphics_context_set_stroke_color(ctx, gc(th->ink));
  graphics_context_set_stroke_width(ctx, 4);
  graphics_draw_line(ctx, c, polar(c, hr_a, 50));
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_line(ctx, c, polar(c, min_a, 76));

  // center cap
  graphics_context_set_fill_color(ctx, gc(th->ink));
  graphics_fill_circle(ctx, c, 5);
  graphics_context_set_fill_color(ctx, gc(th->accent));
  graphics_fill_circle(ctx, c, 2);

  // date (subtle, lower band)
  static char date_buf[16];
  strftime(date_buf, sizeof(date_buf), "%a %d", t);
  for (char *p = date_buf; *p; p++) {
    if (*p >= 'a' && *p <= 'z') *p -= 32;
  }
  graphics_context_set_text_color(ctx, gc(th->date));
  draw_tracked(ctx, date_buf, s_date_font, b.size.w / 2, 190, 3);

  // battery (subtle slim bar)
  BatteryChargeState bat = battery_state_service_peek();
  int pct = bat.charge_percent;
  int bw = 60, bh = 4, bx = (b.size.w - bw) / 2, by = 216;
  graphics_context_set_fill_color(ctx, gc(th->track));
  graphics_fill_rect(ctx, GRect(bx, by, bw, bh), 2, GCornersAll);
  graphics_context_set_fill_color(ctx, gc(pct <= 20 ? th->accent : th->batt));
  graphics_fill_rect(ctx, GRect(bx, by, bw * pct / 100, bh), 2, GCornersAll);
}

static void tick_handler(struct tm *tick_time, TimeUnits units) {
  layer_mark_dirty(s_canvas);
}

static void inbox(DictionaryIterator *it, void *context) {
  Tuple *tm = dict_find(it, MESSAGE_KEY_Theme);
  if (tm) {
    int n = (int)tm->value->int32;
    if (n >= 0 && n < N_THEMES) {
      s_theme = n;
      persist_write_int(PERSIST_THEME, n);
      window_set_background_color(s_window, gc(THEMES[n].bg));
      layer_mark_dirty(s_canvas);
    }
  }
}

static void battery_handler(BatteryChargeState state) {
  layer_mark_dirty(s_canvas);
}

static void window_load(Window *window) {
  s_date_font = fonts_load_custom_font(resource_get_handle(DATE_FONT_RES));
  Layer *root = window_get_root_layer(window);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *window) {
  layer_destroy(s_canvas);
  fonts_unload_custom_font(s_date_font);
}

static void init(void) {
  if (persist_exists(PERSIST_THEME)) {
    int n = persist_read_int(PERSIST_THEME);
    if (n >= 0 && n < N_THEMES) s_theme = n;
  }

  s_window = window_create();
  window_set_background_color(s_window, gc(THEMES[s_theme].bg));
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  battery_state_service_subscribe(battery_handler);

  app_message_register_inbox_received(inbox);
  app_message_open(64, 16);
}

static void deinit(void) {
  battery_state_service_unsubscribe();
  tick_timer_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
