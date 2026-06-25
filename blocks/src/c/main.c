#include <pebble.h>

// Bauhaus Blocks watchface — five stacked colour bands, all-left aligned.
// Rows: day / date / big time / battery / temperature (now + hi/lo).

#define W 200

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
  // --- colour bands ---
  draw_band(ctx, 0,   30, GColorChromeYellow);  // day
  draw_band(ctx, 30,  28, GColorTiffanyBlue);   // date
  draw_band(ctx, 58,  82, GColorWhite);         // time
  draw_band(ctx, 140, 40, GColorOxfordBlue);    // battery
  draw_band(ctx, 180, 48, GColorOrange);        // temperature

  // --- day ---
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, s_day, f_day, GRect(14, 4, 180, 26),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  // --- date ---
  graphics_draw_text(ctx, s_date, f_date, GRect(14, 34, 180, 22),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  // --- time ---
  graphics_draw_text(ctx, s_time, f_time, GRect(10, 62, 192, 80),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  // --- battery glyph + percent ---
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_round_rect(ctx, GRect(14, 152, 50, 16), 2);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(64, 156, 3, 8), 0, GCornerNone);   // cap
  int fillw = (s_batt_pct * 44) / 100;                             // inner usable ~44px
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(17, 155, fillw, 10), 0, GCornerNone);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, s_batt, f_batt, GRect(76, 150, 110, 22),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  // --- temperature: current (big, left) ---
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, s_now, f_now, GRect(12, 181, 100, 44),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  // hi (right)
  graphics_draw_text(ctx, s_hi, f_lbl, GRect(96, 184, 90, 20),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
  // lo (right, muted)
  graphics_context_set_text_color(ctx, GColorWindsorTan);
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
  if (tn) snprintf(s_now, sizeof(s_now), "%d°", (int)tn->value->int32);
  if (th) snprintf(s_hi, sizeof(s_hi), "HI %d°", (int)th->value->int32);
  if (tl) snprintf(s_lo, sizeof(s_lo), "LO %d°", (int)tl->value->int32);
  layer_mark_dirty(s_layer);
}

static void init(void) {
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
