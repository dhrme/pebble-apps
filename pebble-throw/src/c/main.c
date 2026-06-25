#include <pebble.h>

// Pebble Throw — skip a Pebble-shaped stone across the pond.
//   Zen   : endless distance skipping. Relax, go far.
//   Smash : topple the giants (apples + androids) wave by wave. It gets harder.
// One button (select) drives aim -> power -> throw. up/down on title pick mode.

#define SCR_W 200
#define SCR_H 228
#define WATER_Y 120
#define LX 22
#define LY 116
#define MAX_SKIPS 11
#define OPT_ANGLE 20
#define TICK_MS 40
#define FPP 40          // base frames per hop (higher = slower, more visible skip)
#define MIN_FPP 12      // floor so very long throws stay smooth
#define MAX_FLY_FRAMES 150  // cap total flight (~6s) so big throws don't drag
#define WINDUP 14       // anticipation frames before the stone leaves the hand
#define TRACE_FRAMES 28 // hold the finished path on screen (~1.1s) before scoring
#define MAX_TARGETS 8
#define HIT_R 12
#define TGT_Y (WATER_Y - 12)

typedef enum { MODE_ZEN, MODE_SMASH } Mode;
typedef enum {
  ST_TITLE, ST_AIM, ST_POWER, ST_FLY,
  ST_TRACE,         // freeze the full skip path for a beat
  ST_RESULT,        // zen
  ST_WAVE_CLEAR, ST_GAMEOVER,
} State;

typedef struct { int x; int type; bool alive; } Target; // type 0 apple, 1 android

static Window   *s_window;
static Layer    *s_layer;
static AppTimer *s_timer;
static GFont f_big, f_mid, f_small, f_tiny, f_title;

// Tilted Pebble-watch shape for the title, built from GPaths (rotatable).
static GPathInfo BODY_INFO   = {8, (GPoint[]){{-25,-18},{-14,-30},{14,-30},{25,-18},
                                              {25,18},{14,30},{-14,30},{-25,18}}};
static GPathInfo STRAPT_INFO = {4, (GPoint[]){{-10,-30},{10,-30},{8,-46},{-8,-46}}};
static GPathInfo STRAPB_INFO = {4, (GPoint[]){{-10,30},{10,30},{8,46},{-8,46}}};
static GPathInfo BEZEL_INFO  = {8, (GPoint[]){{-17,-12},{-10,-20},{10,-20},{17,-12},
                                              {17,12},{10,20},{-10,20},{-17,12}}};
static GPathInfo SCREEN_INFO = {8, (GPoint[]){{-13,-9},{-7,-15},{7,-15},{13,-9},
                                              {13,9},{7,15},{-7,15},{-13,9}}};
static GPath *s_gp_body, *s_gp_strapt, *s_gp_strapb, *s_gp_bezel, *s_gp_screen;

static Mode  s_mode  = MODE_SMASH;
static State s_state = ST_TITLE;

// aim
static int s_angle = OPT_ANGLE, s_power = 0, s_dir = 1, s_wind = 0;
static int s_angle_step = 2, s_power_step = 4;   // scale with wave in smash

// throw result
static int s_skips = 0, s_dist = 0;
static bool s_new_best = false;

// flight
static int s_hop_x[MAX_SKIPS + 2], s_hop_h[MAX_SKIPS + 2];
static int s_hops = 0, s_frame = 0, s_last_seg = -1, s_fpp = FPP;
static int s_trace_f = 0;

// smash run
static Target s_tg[MAX_TARGETS];
static int s_tg_n = 0;
static int s_wave = 1, s_throws_left = 0;
static int s_run_apples = 0, s_run_androids = 0, s_run_combos = 0;
static bool s_hit_apple = false, s_hit_android = false;

// persisted bests
#define PK_BEST_SKIPS 1
#define PK_BEST_DIST  2
#define PK_BEST_WAVE  3
#define PK_BEST_TOPPLE 4
#define PK_SOUND      5
static int s_best_skips = 0, s_best_dist = 0, s_best_wave = 0, s_best_topple = 0;
static bool s_sound = true;

static void schedule_tick(void);

// ---- sound ---------------------------------------------------------------

#define VOL 100
// All cues go through speaker_play_notes / speaker_play_tracks. speaker_play_tone
// is too quiet on the watch (short single tones were inaudible); square notes at
// full volume, ~120ms+, read clearly. Chords (tracks) are loud and celebratory.
static const SpeakerNote SND_THROW[]  = {{67, SpeakerWaveformSquare, 90, 0, 0},
                                         {79, SpeakerWaveformSquare, 120, 0, 0}};
static const SpeakerNote SND_PLOP[]   = {{52, SpeakerWaveformSquare, 150, 0, 0},
                                         {45, SpeakerWaveformSquare, 230, 0, 0}};
static const SpeakerNote SND_SPLASH[] = {{64, SpeakerWaveformSquare, 90, 0, 0},
                                         {57, SpeakerWaveformSquare, 150, 0, 0}};
static const SpeakerNote SND_WIN[]    = {{72, SpeakerWaveformSquare, 110, 0, 0},
                                         {76, SpeakerWaveformSquare, 110, 0, 0},
                                         {79, SpeakerWaveformSquare, 110, 0, 0},
                                         {84, SpeakerWaveformSquare, 250, 0, 0}};
static const SpeakerNote SND_OVER[]   = {{57, SpeakerWaveformSquare, 160, 0, 0},
                                         {53, SpeakerWaveformSquare, 160, 0, 0},
                                         {48, SpeakerWaveformSquare, 340, 0, 0}};

// combo = a 3-voice chord (polyphony) — plays loud and clear
static const SpeakerNote CB0[] = {{72, SpeakerWaveformSquare, 260, 0, 0}};
static const SpeakerNote CB1[] = {{76, SpeakerWaveformSquare, 260, 0, 0}};
static const SpeakerNote CB2[] = {{79, SpeakerWaveformSquare, 260, 0, 0}};
static const SpeakerTrack SND_COMBO[] = {{CB0, 1, NULL}, {CB1, 1, NULL}, {CB2, 1, NULL}};

static void play(const SpeakerNote *n, uint32_t c) {
  if (s_sound) speaker_play_notes(n, c, VOL);
}
#define PLAY(a) play(a, sizeof(a) / sizeof(a[0]))

static void play_combo(void) {
  if (s_sound) speaker_play_tracks(SND_COMBO, 3, VOL);
}

static void snd_skip(int i) {                  // rising plink per bounce
  if (!s_sound) return;
  static SpeakerNote n;
  int m = 74 + i * 2;
  if (m > 96) m = 96;
  n = (SpeakerNote){ (uint8_t)m, SpeakerWaveformSquare, 120, 0, 0 };
  speaker_play_notes(&n, 1, VOL);
}

static void snd_hit(int type) {                // apple hi, android lo
  if (!s_sound) return;
  static SpeakerNote n;
  n = (SpeakerNote){ type == 0 ? 86 : 59, SpeakerWaveformSquare, 170, 0, 0 };
  speaker_play_notes(&n, 1, VOL);
}

// ---- physics -------------------------------------------------------------

static int angle_quality(int angle) {
  int d = angle - OPT_ANGLE; if (d < 0) d = -d;
  int q = 100 - d * 100 / 35;
  return q < 0 ? 0 : q;
}

static int rating_stars(int skips) {
  if (skips <= 0) return 0;
  if (skips <= 2) return 1;
  if (skips <= 4) return 2;
  if (skips <= 6) return 3;
  if (skips <= 8) return 4;
  return 5;
}

static void compute_throw(void) {
  int aq = angle_quality(s_angle);
  int s = s_power * aq / 100 * MAX_SKIPS / 100;
  s += (rand() % 3) - 1;
  if (s < 0) s = 0;
  if (s > MAX_SKIPS) s = MAX_SKIPS;
  s_skips = s;
  int d = s * 12 + s_power / 3 + s_wind * 3;
  if (s > 0) d += 8;
  if (d < 0) d = 0;
  s_dist = d;
}

static void build_flight(void) {
  s_hops = s_skips < 1 ? 1 : s_skips;
  s_fpp = FPP;
  if (s_hops * s_fpp > MAX_FLY_FRAMES) s_fpp = MAX_FLY_FRAMES / s_hops;
  if (s_fpp < MIN_FPP) s_fpp = MIN_FPP;
  int end_x = 188, span = end_x - LX, total_w = 0;
  for (int i = 0; i < s_hops; i++) total_w += (s_hops - i);
  s_hop_x[0] = LX;
  int acc = 0;
  for (int i = 0; i < s_hops; i++) {
    acc += (s_hops - i);
    s_hop_x[i + 1] = LX + span * acc / total_w;
    s_hop_h[i] = 14 + (40 * (s_hops - i)) / s_hops;
  }
  s_frame = -WINDUP; s_last_seg = -1;
}

static int targets_left(void) {
  int n = 0;
  for (int i = 0; i < s_tg_n; i++) if (s_tg[i].alive) n++;
  return n;
}

static void try_hit(int x) {
  for (int i = 0; i < s_tg_n; i++) {
    if (!s_tg[i].alive) continue;
    int dx = s_tg[i].x - x; if (dx < 0) dx = -dx;
    if (dx <= HIT_R) {
      s_tg[i].alive = false;
      if (s_tg[i].type == 0) { s_run_apples++;   s_hit_apple   = true; }
      else                   { s_run_androids++; s_hit_android = true; }
      snd_hit(s_tg[i].type);
      vibes_short_pulse();
      return;                       // one giant per skip
    }
  }
}

static void spawn_wave(int w) {
  s_tg_n = 3 + (w - 1);
  if (s_tg_n > MAX_TARGETS) s_tg_n = MAX_TARGETS;
  int x_min = 70, x_max = 124 + w * 12;
  if (x_max > 186) x_max = 186;
  if (x_max < x_min + 20) x_max = x_min + 20;
  for (int i = 0; i < s_tg_n; i++) {
    s_tg[i].x = (s_tg_n == 1) ? x_min : x_min + (x_max - x_min) * i / (s_tg_n - 1);
    s_tg[i].type = rand() % 2;
    s_tg[i].alive = true;
  }
  s_throws_left = s_tg_n - w / 2;
  if (s_throws_left < 2) s_throws_left = 2;
  s_angle_step = 2 + w / 4;
  if (s_angle_step > 6) s_angle_step = 6;
  s_power_step = 4 + w / 3;
  if (s_power_step > 9) s_power_step = 9;
}

// ---- drawing -------------------------------------------------------------

// The launcher is a big Pebble watch sitting at the shore, flinging little ones.
static void draw_shooter(GContext *ctx) {
  int x = LX, y = 122;
  graphics_context_set_fill_color(ctx, GColorDarkGray);
  graphics_fill_rect(ctx, GRect(x - 5, y - 17, 10, 6), 1, GCornersAll);  // top strap
  graphics_fill_rect(ctx, GRect(x - 5, y + 11, 10, 6), 1, GCornersAll);  // bottom strap
  graphics_fill_rect(ctx, GRect(x - 11, y - 13, 22, 27), 5, GCornersAll);// body
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(x - 8, y - 9, 16, 19), 3, GCornersAll);  // bezel
  graphics_context_set_fill_color(ctx, GColorVividCerulean);
  graphics_fill_rect(ctx, GRect(x - 7, y - 8, 14, 17), 2, GCornersAll);  // screen
}

static void draw_pond(GContext *ctx) {        // sky + water, no launcher
  graphics_context_set_fill_color(ctx, GColorCeleste);
  graphics_fill_rect(ctx, GRect(0, 0, SCR_W, WATER_Y), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, GColorVividCerulean);
  graphics_fill_rect(ctx, GRect(0, WATER_Y, SCR_W, SCR_H - WATER_Y), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, GColorCeleste);
  graphics_fill_rect(ctx, GRect(0, WATER_Y, SCR_W, 3), 0, GCornerNone);
}

static void draw_scene(GContext *ctx) {       // pond + launcher (gameplay)
  draw_pond(ctx);
  draw_shooter(ctx);
}

static void draw_target(GContext *ctx, int x, int type) {
  if (type == 0) {                 // apple
    graphics_context_set_fill_color(ctx, GColorRed);
    graphics_fill_circle(ctx, GPoint(x, TGT_Y), 8);
    graphics_context_set_fill_color(ctx, GColorCeleste);
    graphics_fill_circle(ctx, GPoint(x + 5, TGT_Y - 2), 5);
    graphics_context_set_fill_color(ctx, GColorWindsorTan);
    graphics_fill_rect(ctx, GRect(x - 1, TGT_Y - 13, 2, 5), 0, GCornerNone);
    graphics_context_set_fill_color(ctx, GColorKellyGreen);
    graphics_fill_circle(ctx, GPoint(x + 4, TGT_Y - 12), 3);
  } else {                         // android
    graphics_context_set_stroke_color(ctx, GColorKellyGreen);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_line(ctx, GPoint(x - 4, TGT_Y - 6), GPoint(x - 6, TGT_Y - 12));
    graphics_draw_line(ctx, GPoint(x + 4, TGT_Y - 6), GPoint(x + 6, TGT_Y - 12));
    graphics_context_set_stroke_width(ctx, 1);
    graphics_context_set_fill_color(ctx, GColorKellyGreen);
    graphics_fill_circle(ctx, GPoint(x, TGT_Y), 8);
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_circle(ctx, GPoint(x - 3, TGT_Y - 1), 1);
    graphics_fill_circle(ctx, GPoint(x + 3, TGT_Y - 1), 1);
  }
}

static void draw_targets(GContext *ctx) {
  for (int i = 0; i < s_tg_n; i++)
    if (s_tg[i].alive) draw_target(ctx, s_tg[i].x, s_tg[i].type);
}

// The projectile is a Pebble watch: rounded body, screen, and two strap lugs.
static void draw_pebble(GContext *ctx, int x, int y) {
  graphics_context_set_fill_color(ctx, GColorDarkGray);
  graphics_fill_rect(ctx, GRect(x - 4, y - 13, 8, 5), 1, GCornersAll);  // top strap
  graphics_fill_rect(ctx, GRect(x - 4, y + 8,  8, 5), 1, GCornersAll);  // bottom strap
  graphics_fill_rect(ctx, GRect(x - 8, y - 9, 16, 18), 4, GCornersAll); // body
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(x - 5, y - 6, 10, 12), 2, GCornersAll); // bezel
  graphics_context_set_fill_color(ctx, GColorVividCerulean);
  graphics_fill_rect(ctx, GRect(x - 4, y - 5, 8, 10), 1, GCornersAll);  // screen
}

static void draw_ripple(GContext *ctx, int x, int y) {
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_draw_circle(ctx, GPoint(x, y), 6);
  graphics_context_set_stroke_color(ctx, GColorCeleste);
  graphics_draw_circle(ctx, GPoint(x, y), 3);
}

static void draw_needle(GContext *ctx, int angle, GColor col) {
  int32_t a = TRIG_MAX_ANGLE * angle / 360;
  int L = 74;
  int dx = L * cos_lookup(a) / TRIG_MAX_RATIO;
  int dy = L * sin_lookup(a) / TRIG_MAX_RATIO;
  graphics_context_set_stroke_color(ctx, col);
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_line(ctx, GPoint(LX, LY), GPoint(LX + dx, LY - dy));
  graphics_context_set_stroke_width(ctx, 1);
}

static void draw_hint(GContext *ctx, const char *txt) {
  graphics_context_set_fill_color(ctx, GColorOxfordBlue);
  graphics_fill_rect(ctx, GRect(20, 200, 160, 22), 6, GCornersAll);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, txt, f_tiny, GRect(20, 202, 160, 18),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

static void draw_smash_hud(GContext *ctx) {
  char buf[24];
  snprintf(buf, sizeof buf, "W%d  throws %d", s_wave, s_throws_left);
  graphics_context_set_fill_color(ctx, GColorOxfordBlue);
  graphics_fill_rect(ctx, GRect(6, 6, 130, 22), 6, GCornersAll);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, buf, f_tiny, GRect(10, 8, 124, 18),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  // mini tally top-right
  graphics_context_set_fill_color(ctx, GColorRed);
  graphics_fill_circle(ctx, GPoint(150, 17), 6);
  graphics_context_set_fill_color(ctx, GColorKellyGreen);
  graphics_fill_circle(ctx, GPoint(178, 17), 6);
  char a[6], n[6];
  snprintf(a, sizeof a, "%d", s_run_apples);
  snprintf(n, sizeof n, "%d", s_run_androids);
  graphics_context_set_text_color(ctx, GColorOxfordBlue);
  graphics_draw_text(ctx, a, f_tiny, GRect(156, 8, 16, 18),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  graphics_draw_text(ctx, n, f_tiny, GRect(184, 8, 16, 18),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

static void draw_wind(GContext *ctx, int y) {
  char buf[12];
  if (s_wind == 0) snprintf(buf, sizeof buf, "wind --");
  else snprintf(buf, sizeof buf, "wind %s%d", s_wind > 0 ? "+" : "", s_wind);
  graphics_context_set_text_color(ctx, GColorOxfordBlue);
  graphics_draw_text(ctx, buf, f_tiny, GRect(8, y, 110, 18),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

static void draw_stars(GContext *ctx, int n, int y) {
  int cx = SCR_W / 2 - 4 * 22;
  for (int i = 0; i < 5; i++) {
    graphics_context_set_fill_color(ctx, i < n ? GColorOrange : GColorLightGray);
    graphics_fill_circle(ctx, GPoint(cx + i * 22 + 36, y), 6);
  }
}

// Small speaker icon: sound waves when on, red slash when off. y = vertical centre.
static void draw_speaker(GContext *ctx, int x, int y, bool on) {
  graphics_context_set_fill_color(ctx, GColorOxfordBlue);
  graphics_context_set_stroke_color(ctx, GColorOxfordBlue);
  graphics_fill_rect(ctx, GRect(x, y - 2, 3, 5), 0, GCornerNone);     // box
  for (int xx = 0; xx <= 5; xx++) {                                   // cone
    int h = 2 + xx;
    graphics_draw_line(ctx, GPoint(x + 3 + xx, y - h), GPoint(x + 3 + xx, y + h));
  }
  if (on) {
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_arc(ctx, GRect(x + 2, y - 6, 12, 12), GOvalScaleModeFitCircle,
                      DEG_TO_TRIGANGLE(30), DEG_TO_TRIGANGLE(150));
    graphics_draw_arc(ctx, GRect(x - 1, y - 9, 18, 18), GOvalScaleModeFitCircle,
                      DEG_TO_TRIGANGLE(35), DEG_TO_TRIGANGLE(145));
    graphics_context_set_stroke_width(ctx, 1);
  } else {
    graphics_context_set_stroke_color(ctx, GColorRed);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_line(ctx, GPoint(x + 9, y - 8), GPoint(x + 17, y + 8));
    graphics_context_set_stroke_width(ctx, 1);
  }
}

// ► marker hugging the right edge, pointing at the physical button at height cy.
static void draw_marker(GContext *ctx, int cy) {
  graphics_context_set_stroke_color(ctx, GColorOrange);
  for (int c = 0; c <= 5; c++) {
    int h = 5 - c;
    graphics_draw_line(ctx, GPoint(191 + c, cy - h), GPoint(191 + c, cy + h));
  }
}

// Big Pebble watch, tilted, with speed lines — the title's hero graphic.
static void draw_pebble_big(GContext *ctx, int cx, int cy) {
  int32_t a = DEG_TO_TRIGANGLE(18);
  // speed lines trailing behind (to the left)
  graphics_context_set_stroke_color(ctx, GColorDukeBlue);
  graphics_context_set_stroke_width(ctx, 3);
  for (int i = 0; i < 3; i++) {
    int yy = cy - 16 + i * 16;
    graphics_draw_line(ctx, GPoint(cx - 58 + i * 5, yy + 3),
                            GPoint(cx - 34 + i * 5, yy - 1));
  }
  graphics_context_set_stroke_width(ctx, 1);
  struct { GPath *p; GColor col; } parts[] = {
    { s_gp_strapt, GColorDarkGray }, { s_gp_strapb, GColorDarkGray },
    { s_gp_body,   GColorDarkGray }, { s_gp_bezel,  GColorWhite },
    { s_gp_screen, GColorVividCerulean },
  };
  for (unsigned i = 0; i < sizeof parts / sizeof parts[0]; i++) {
    gpath_rotate_to(parts[i].p, a);
    gpath_move_to(parts[i].p, GPoint(cx, cy));
    graphics_context_set_fill_color(ctx, parts[i].col);
    gpath_draw_filled(ctx, parts[i].p);
  }
}

static void draw_title(GContext *ctx) {
  draw_pond(ctx);

  // "Pebble Throw" on one line, two-tone, centred
  GSize w1 = graphics_text_layout_get_content_size("Pebble ", f_title,
               GRect(0, 0, SCR_W, 40), GTextOverflowModeWordWrap, GTextAlignmentLeft);
  GSize w2 = graphics_text_layout_get_content_size("Throw", f_title,
               GRect(0, 0, SCR_W, 40), GTextOverflowModeWordWrap, GTextAlignmentLeft);
  int x0 = (SCR_W - w1.w - w2.w) / 2;
  graphics_context_set_text_color(ctx, GColorOxfordBlue);
  graphics_draw_text(ctx, "Pebble", f_title, GRect(x0, 2, w1.w + 4, 36),
                     GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
  graphics_context_set_text_color(ctx, GColorOrange);
  graphics_draw_text(ctx, "Throw", f_title, GRect(x0 + w1.w, 2, w2.w + 6, 36),
                     GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

  // hero pebble
  draw_pebble_big(ctx, 74, 118);

  // best score, bottom-left
  char best[20];
  if (s_mode == MODE_ZEN) snprintf(best, sizeof best, "best  %d skips", s_best_skips);
  else                    snprintf(best, sizeof best, "best  wave %d", s_best_wave);
  graphics_context_set_text_color(ctx, GColorDukeBlue);
  graphics_draw_text(ctx, best, f_small, GRect(8, 188, 130, 24),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  // up button: "mode" + the live mode name
  const char *name = s_mode == MODE_ZEN ? "Zen" : "Smash";
  graphics_context_set_text_color(ctx, GColorDukeBlue);
  graphics_draw_text(ctx, "mode", f_tiny, GRect(108, 38, 80, 16),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
  graphics_context_set_text_color(ctx, GColorOxfordBlue);
  graphics_draw_text(ctx, name, f_small, GRect(108, 52, 80, 22),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
  draw_marker(ctx, 54);

  // select button: Play
  graphics_context_set_text_color(ctx, GColorOxfordBlue);
  graphics_draw_text(ctx, "Play", f_small, GRect(108, 104, 80, 22),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
  draw_marker(ctx, 114);

  // down button: sound on/off icon
  draw_speaker(ctx, 168, 178, s_sound);
  draw_marker(ctx, 178);
}

static void draw_aim(GContext *ctx) {
  draw_scene(ctx);
  if (s_mode == MODE_SMASH) { draw_targets(ctx); draw_smash_hud(ctx); draw_wind(ctx, 30); }
  else draw_wind(ctx, 6);
  draw_needle(ctx, s_angle, GColorOrange);
  char buf[8]; snprintf(buf, sizeof buf, "%d°", s_angle);
  graphics_context_set_text_color(ctx, GColorOxfordBlue);
  graphics_draw_text(ctx, buf, f_mid, GRect(118, 40, 72, 30),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
  draw_hint(ctx, "select to lock angle");
}

static void draw_power(GContext *ctx) {
  draw_scene(ctx);
  if (s_mode == MODE_SMASH) { draw_targets(ctx); draw_smash_hud(ctx); }
  draw_needle(ctx, s_angle, GColorDarkGray);
  GRect track = GRect(170, 40, 16, 130);
  graphics_context_set_fill_color(ctx, GColorLightGray);
  graphics_fill_rect(ctx, track, 4, GCornersAll);
  int h = 130 * s_power / 100;
  graphics_context_set_fill_color(ctx, GColorOrange);
  graphics_fill_rect(ctx, GRect(170, 40 + 130 - h, 16, h), 4, GCornersAll);
  char buf[8]; snprintf(buf, sizeof buf, "%d", s_power);
  graphics_context_set_text_color(ctx, GColorOxfordBlue);
  graphics_draw_text(ctx, buf, f_mid, GRect(90, 70, 70, 30),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
  draw_hint(ctx, "select to throw!");
}

static void draw_fly(GContext *ctx) {
  draw_scene(ctx);
  if (s_mode == MODE_SMASH) { draw_targets(ctx); draw_smash_hud(ctx); }
  if (s_frame < 0) {                   // wind-up: stone pulls back, then springs
    int pull = -s_frame;               // 8..1
    draw_needle(ctx, s_angle, GColorDarkGray);
    draw_pebble(ctx, LX - pull / 2, LY + pull / 3);
    return;
  }
  int seg = s_frame / s_fpp; if (seg > s_hops) seg = s_hops;
  for (int i = 1; i <= seg && i <= s_hops; i++)
    draw_ripple(ctx, s_hop_x[i], WATER_Y + 4);
  if (seg < s_hops) {
    int lt = s_frame % s_fpp;
    int x0 = s_hop_x[seg], x1 = s_hop_x[seg + 1];
    int x = x0 + (x1 - x0) * lt / s_fpp;
    int t = lt * 100 / s_fpp;
    int arc = s_hop_h[seg] * 4 * t * (100 - t) / 10000;
    draw_pebble(ctx, x, WATER_Y - arc - 6);
  } else {
    draw_pebble(ctx, s_hop_x[s_hops], WATER_Y - 4);
  }
  if (s_mode == MODE_ZEN) {
    char hud[16];
    int shown = seg < s_skips ? seg : s_skips;
    snprintf(hud, sizeof hud, "skip x%d", shown);
    graphics_context_set_fill_color(ctx, GColorOxfordBlue);
    graphics_fill_rect(ctx, GRect(8, 8, 88, 24), 6, GCornersAll);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, hud, f_small, GRect(8, 9, 88, 22),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
}

// Replay the whole trajectory as a dotted arc across every hop.
static void draw_path(GContext *ctx) {
  graphics_context_set_fill_color(ctx, GColorWhite);
  for (int i = 0; i < s_hops; i++) {
    int x0 = s_hop_x[i], x1 = s_hop_x[i + 1], hh = s_hop_h[i];
    for (int s = 0; s <= 8; s += 2) {
      int t = s * 100 / 8;
      int x = x0 + (x1 - x0) * s / 8;
      int arc = hh * 4 * t * (100 - t) / 10000;
      graphics_fill_circle(ctx, GPoint(x, WATER_Y - arc - 6), 2);
    }
  }
}

static void draw_trace(GContext *ctx) {        // frozen path, held a beat
  draw_scene(ctx);
  if (s_mode == MODE_SMASH) { draw_targets(ctx); draw_smash_hud(ctx); }
  for (int i = 1; i <= s_hops; i++)
    draw_ripple(ctx, s_hop_x[i], WATER_Y + 4);
  draw_path(ctx);
  draw_pebble(ctx, s_hop_x[s_hops], WATER_Y - 2);   // resting where it sank
  if (s_mode == MODE_ZEN) {
    char hud[16];
    snprintf(hud, sizeof hud, "%d skips!", s_skips);
    graphics_context_set_fill_color(ctx, GColorOxfordBlue);
    graphics_fill_rect(ctx, GRect(8, 8, 110, 24), 6, GCornersAll);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, hud, f_small, GRect(12, 9, 102, 22),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }
}

static void draw_result(GContext *ctx) {       // zen
  draw_scene(ctx);
  draw_ripple(ctx, 150, WATER_Y + 6);
  char l1[16], l2[28];
  if (s_skips == 0) {
    graphics_context_set_text_color(ctx, GColorDukeBlue);
    graphics_draw_text(ctx, "plop!", f_big, GRect(0, 16, SCR_W, 44),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  } else {
    snprintf(l1, sizeof l1, "%d", s_skips);
    graphics_context_set_text_color(ctx, GColorOxfordBlue);
    graphics_draw_text(ctx, l1, f_big, GRect(0, 6, SCR_W, 56),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    graphics_draw_text(ctx, "skips", f_small, GRect(0, 62, SCR_W, 22),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
  snprintf(l2, sizeof l2, "%dm  ·  best %d", s_dist, s_best_skips);
  graphics_context_set_text_color(ctx, GColorOxfordBlue);
  graphics_draw_text(ctx, l2, f_tiny, GRect(0, 90, SCR_W, 18),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  draw_stars(ctx, rating_stars(s_skips), 130);
  if (s_new_best) {
    graphics_context_set_text_color(ctx, GColorIslamicGreen);
    graphics_draw_text(ctx, "new best!", f_small, GRect(0, 150, SCR_W, 22),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
  draw_hint(ctx, "select again  ·  back menu");
}

static void draw_wave_clear(GContext *ctx) {
  draw_scene(ctx);
  char l1[18], l2[28];
  snprintf(l1, sizeof l1, "wave %d", s_wave);
  graphics_context_set_text_color(ctx, GColorOxfordBlue);
  graphics_draw_text(ctx, l1, f_big, GRect(0, 24, SCR_W, 48),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  graphics_context_set_text_color(ctx, GColorIslamicGreen);
  graphics_draw_text(ctx, "clear!", f_mid, GRect(0, 78, SCR_W, 30),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  snprintf(l2, sizeof l2, "%d toppled · %d combos", s_run_apples + s_run_androids, s_run_combos);
  graphics_context_set_text_color(ctx, GColorOxfordBlue);
  graphics_draw_text(ctx, l2, f_tiny, GRect(0, 120, SCR_W, 18),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  draw_hint(ctx, "select: next wave");
}

static void draw_gameover(GContext *ctx) {
  draw_scene(ctx);
  graphics_context_set_text_color(ctx, GColorDukeBlue);
  graphics_draw_text(ctx, "game over", f_mid, GRect(0, 18, SCR_W, 32),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  char l1[24], l2[28];
  snprintf(l1, sizeof l1, "reached wave %d", s_wave);
  graphics_context_set_text_color(ctx, GColorOxfordBlue);
  graphics_draw_text(ctx, l1, f_small, GRect(0, 58, SCR_W, 24),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  // tally with icons
  graphics_context_set_fill_color(ctx, GColorRed);
  graphics_fill_circle(ctx, GPoint(64, 104), 9);
  graphics_context_set_fill_color(ctx, GColorKellyGreen);
  graphics_fill_circle(ctx, GPoint(120, 104), 9);
  char a[6], n[6];
  snprintf(a, sizeof a, "%d", s_run_apples);
  snprintf(n, sizeof n, "%d", s_run_androids);
  graphics_context_set_text_color(ctx, GColorOxfordBlue);
  graphics_draw_text(ctx, a, f_small, GRect(76, 92, 30, 24),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  graphics_draw_text(ctx, n, f_small, GRect(132, 92, 30, 24),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  snprintf(l2, sizeof l2, "best  wave %d · %d", s_best_wave, s_best_topple);
  graphics_context_set_text_color(ctx, GColorOxfordBlue);
  graphics_draw_text(ctx, l2, f_tiny, GRect(0, 138, SCR_W, 18),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  draw_hint(ctx, "select: menu");
}

static void update_proc(Layer *layer, GContext *ctx) {
  switch (s_state) {
    case ST_TITLE:      draw_title(ctx);      break;
    case ST_AIM:        draw_aim(ctx);        break;
    case ST_POWER:      draw_power(ctx);      break;
    case ST_FLY:        draw_fly(ctx);        break;
    case ST_TRACE:      draw_trace(ctx);      break;
    case ST_RESULT:     draw_result(ctx);     break;
    case ST_WAVE_CLEAR: draw_wave_clear(ctx); break;
    case ST_GAMEOVER:   draw_gameover(ctx);   break;
  }
}

// ---- flow ----------------------------------------------------------------

static void begin_aim(void) {
  s_wind  = (rand() % 7) - 3;
  s_angle = 10; s_dir = 1;
  s_state = ST_AIM;
  schedule_tick();
}

static void start_zen(void) {
  s_mode = MODE_ZEN;
  s_angle_step = 2; s_power_step = 4;
  begin_aim();
}

static void start_smash(void) {
  s_mode = MODE_SMASH;
  s_wave = 1; s_run_apples = 0; s_run_androids = 0; s_run_combos = 0;
  spawn_wave(1);
  begin_aim();
}

static void save_smash_bests(void) {
  int tot = s_run_apples + s_run_androids;
  if (s_wave > s_best_wave)  { s_best_wave = s_wave;   persist_write_int(PK_BEST_WAVE, s_best_wave); }
  if (tot    > s_best_topple){ s_best_topple = tot;    persist_write_int(PK_BEST_TOPPLE, s_best_topple); }
}

static void resolve_smash(void) {
  if (s_hit_apple && s_hit_android) { s_run_combos++; play_combo(); vibes_double_pulse(); }
  if (targets_left() == 0) {
    save_smash_bests();
    PLAY(SND_WIN);
    s_state = ST_WAVE_CLEAR;
  } else {
    s_throws_left--;
    if (s_throws_left <= 0) { save_smash_bests(); PLAY(SND_OVER); s_state = ST_GAMEOVER; }
    else { begin_aim(); return; }
  }
}

static void do_resolve(void) {                 // runs after the trace pause
  if (s_mode == MODE_ZEN) {
    s_new_best = false;
    if (s_skips > s_best_skips) { s_best_skips = s_skips; s_new_best = true;
                                  persist_write_int(PK_BEST_SKIPS, s_best_skips); }
    if (s_dist  > s_best_dist)  { s_best_dist  = s_dist;  s_new_best = true;
                                  persist_write_int(PK_BEST_DIST,  s_best_dist);  }
    if (s_new_best) { PLAY(SND_WIN); vibes_double_pulse(); }
    s_state = ST_RESULT;
  } else {
    resolve_smash();
  }
}

static void end_flight(void) {                 // pebble sank: freeze the path
  if (s_skips == 0) PLAY(SND_PLOP);
  else              PLAY(SND_SPLASH);
  s_state = ST_TRACE;
  s_trace_f = 0;
  schedule_tick();
}

// ---- tick ----------------------------------------------------------------

static void tick(void *data) {
  s_timer = NULL;
  if (s_state == ST_AIM) {
    s_angle += s_dir * s_angle_step;
    if (s_angle >= 60) { s_angle = 60; s_dir = -1; }
    if (s_angle <= 10) { s_angle = 10; s_dir = 1; }
  } else if (s_state == ST_POWER) {
    s_power += s_dir * s_power_step;
    if (s_power >= 100) { s_power = 100; s_dir = -1; }
    if (s_power <= 0)   { s_power = 0;   s_dir = 1; }
  } else if (s_state == ST_FLY) {
    if (s_frame < 0) {                 // anticipation, stone still in hand
      s_frame++;
      layer_mark_dirty(s_layer);
      schedule_tick();
      return;
    }
    int seg = s_frame / s_fpp;
    if (seg != s_last_seg && seg >= 1 && seg <= s_hops) {
      s_last_seg = seg;
      snd_skip(seg);
      if (s_mode == MODE_SMASH) try_hit(s_hop_x[seg]);
      else                      vibes_short_pulse();
    }
    s_frame++;
    if (s_frame > s_hops * s_fpp) {
      end_flight();
      layer_mark_dirty(s_layer);
      return;
    }
  } else if (s_state == ST_TRACE) {
    s_trace_f++;
    if (s_trace_f > TRACE_FRAMES) {
      do_resolve();
      layer_mark_dirty(s_layer);
      return;
    }
  }
  layer_mark_dirty(s_layer);
  schedule_tick();
}

static void schedule_tick(void) {
  if (s_state == ST_AIM || s_state == ST_POWER || s_state == ST_FLY || s_state == ST_TRACE)
    s_timer = app_timer_register(TICK_MS, tick, NULL);
}

// ---- buttons -------------------------------------------------------------

static void select_click(ClickRecognizerRef ref, void *ctx) {
  switch (s_state) {
    case ST_TITLE:
      if (s_mode == MODE_ZEN) start_zen(); else start_smash();
      break;
    case ST_AIM:
      s_state = ST_POWER; s_power = 0; s_dir = 1; schedule_tick();
      break;
    case ST_POWER:
      s_state = ST_FLY;
      s_hit_apple = false; s_hit_android = false;
      compute_throw(); build_flight();
      PLAY(SND_THROW);
      vibes_short_pulse();
      schedule_tick();
      break;
    case ST_FLY:
    case ST_TRACE:
      break;                       // ignore until the path settles
    case ST_RESULT:
      begin_aim();                 // zen: throw again
      break;
    case ST_WAVE_CLEAR:
      s_wave++; spawn_wave(s_wave); begin_aim();
      break;
    case ST_GAMEOVER:
      s_state = ST_TITLE;
      break;
  }
  layer_mark_dirty(s_layer);
}

static void mode_cycle(ClickRecognizerRef ref, void *ctx) {   // up
  if (s_state != ST_TITLE) return;
  s_mode = (s_mode == MODE_ZEN) ? MODE_SMASH : MODE_ZEN;
  layer_mark_dirty(s_layer);
}

static void sound_toggle(ClickRecognizerRef ref, void *ctx) { // down
  if (s_state != ST_TITLE) return;
  s_sound = !s_sound;
  persist_write_int(PK_SOUND, s_sound ? 1 : 0);
  if (s_sound) { static const SpeakerNote c = {79, SpeakerWaveformSquare, 160, 0, 0};
                 speaker_play_notes(&c, 1, VOL); }                    // chirp on
  layer_mark_dirty(s_layer);
}

static void back_click(ClickRecognizerRef ref, void *ctx) {
  switch (s_state) {
    case ST_TITLE:
      window_stack_pop_all(true);
      break;
    case ST_FLY:
    case ST_TRACE:
      break;
    default:
      s_state = ST_TITLE;
      layer_mark_dirty(s_layer);
      break;
  }
}

static void click_config(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_single_click_subscribe(BUTTON_ID_UP,     mode_cycle);
  window_single_click_subscribe(BUTTON_ID_DOWN,   sound_toggle);
  window_single_click_subscribe(BUTTON_ID_BACK,   back_click);
}

// ---- lifecycle -----------------------------------------------------------

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_layer, update_proc);
  layer_add_child(root, s_layer);
}

static void window_unload(Window *window) { layer_destroy(s_layer); }

static void init(void) {
  srand(time(NULL));
  s_best_skips  = persist_exists(PK_BEST_SKIPS)  ? persist_read_int(PK_BEST_SKIPS)  : 0;
  s_best_dist   = persist_exists(PK_BEST_DIST)   ? persist_read_int(PK_BEST_DIST)   : 0;
  s_best_wave   = persist_exists(PK_BEST_WAVE)   ? persist_read_int(PK_BEST_WAVE)   : 0;
  s_best_topple = persist_exists(PK_BEST_TOPPLE) ? persist_read_int(PK_BEST_TOPPLE) : 0;
  s_sound       = persist_exists(PK_SOUND) ? (persist_read_int(PK_SOUND) != 0) : true;
  speaker_set_volume(VOL);

  f_big   = fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);
  f_mid   = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  f_small = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  f_tiny  = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  f_title = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);

  s_gp_body   = gpath_create(&BODY_INFO);
  s_gp_strapt = gpath_create(&STRAPT_INFO);
  s_gp_strapb = gpath_create(&STRAPB_INFO);
  s_gp_bezel  = gpath_create(&BEZEL_INFO);
  s_gp_screen = gpath_create(&SCREEN_INFO);

  s_window = window_create();
  window_set_click_config_provider(s_window, click_config);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load, .unload = window_unload,
  });
  window_stack_push(s_window, true);
}

static void deinit(void) {
  if (s_timer) app_timer_cancel(s_timer);
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
