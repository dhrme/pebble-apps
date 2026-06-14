#include <pebble.h>

// Voice Notes — dictate a note (speech -> text), browse a list, read, delete.
// Every saved note is ALSO relayed (via the phone, src/pkjs/index.js) to a
// Google Apps Script web app that creates a Google Calendar event.
//
// Local storage: persist key 0 = count, keys 1..N = note strings.
// NOTE: deleting a note on the watch is local-only; it does not remove the
// calendar event (that would need per-note event-id tracking — a v2 item).

#define MAX_NOTES      20
#define NOTE_MAX_LEN   160         // bytes incl. null; <= PERSIST_STRING_MAX_LENGTH (256)
#define PKEY_COUNT     0
#define PKEY_NOTE_BASE 1

// ---- data ----
static char s_notes[MAX_NOTES][NOTE_MAX_LEN];
static int  s_note_count = 0;

// ---- ui ----
static Window    *s_list_window;
static MenuLayer *s_menu_layer;
static Window    *s_detail_window;
static TextLayer *s_detail_text;
static TextLayer *s_detail_hint;
static int        s_detail_index = -1;
static DictationSession *s_dictation;

// ---- calendar-result flash ----
static Window    *s_result_window;
static TextLayer *s_result_text;
static char       s_result_buf[96];
static bool       s_result_ok;
static AppTimer  *s_result_timer;

// ---------------------------------------------------------------- persistence
static void save_notes(void) {
  persist_write_int(PKEY_COUNT, s_note_count);
  for (int i = 0; i < s_note_count; i++) {
    int res = persist_write_string(PKEY_NOTE_BASE + i, s_notes[i]);
    if (res < 0) {
      APP_LOG(APP_LOG_LEVEL_WARNING, "persist_write_string key %d failed (%d)", i, res);
    }
  }
  if (s_note_count < MAX_NOTES && persist_exists(PKEY_NOTE_BASE + s_note_count)) {
    persist_delete(PKEY_NOTE_BASE + s_note_count);
  }
}

static void load_notes(void) {
  s_note_count = persist_exists(PKEY_COUNT) ? persist_read_int(PKEY_COUNT) : 0;
  if (s_note_count < 0) s_note_count = 0;
  if (s_note_count > MAX_NOTES) s_note_count = MAX_NOTES;
  for (int i = 0; i < s_note_count; i++) {
    if (persist_exists(PKEY_NOTE_BASE + i)) {
      persist_read_string(PKEY_NOTE_BASE + i, s_notes[i], NOTE_MAX_LEN);
    } else {
      s_notes[i][0] = '\0';
    }
  }
}

// ------------------------------------------------------------------ note ops
static void add_note(const char *text) {
  int start = (s_note_count < MAX_NOTES) ? s_note_count : (MAX_NOTES - 1);
  for (int i = start; i > 0; i--) {
    strncpy(s_notes[i], s_notes[i - 1], NOTE_MAX_LEN);
    s_notes[i][NOTE_MAX_LEN - 1] = '\0';
  }
  strncpy(s_notes[0], text, NOTE_MAX_LEN - 1);
  s_notes[0][NOTE_MAX_LEN - 1] = '\0';
  if (s_note_count < MAX_NOTES) s_note_count++;
  save_notes();
}

static void delete_note(int idx) {
  if (idx < 0 || idx >= s_note_count) return;
  for (int i = idx; i < s_note_count - 1; i++) {
    strncpy(s_notes[i], s_notes[i + 1], NOTE_MAX_LEN);
    s_notes[i][NOTE_MAX_LEN - 1] = '\0';
  }
  s_note_count--;
  save_notes();
}

// ------------------------------------------------------ calendar result flash
static void result_timer_cb(void *data) {
  s_result_timer = NULL;
  if (s_result_window) {
    window_stack_remove(s_result_window, true);
  }
}

static void result_window_load(Window *window) {
  window_set_background_color(window, s_result_ok ? GColorJaegerGreen : GColorDarkCandyAppleRed);
  Layer *root = window_get_root_layer(window);
  GRect b = layer_get_bounds(root);

  s_result_text = text_layer_create(GRect(6, 36, b.size.w - 12, b.size.h - 44));
  text_layer_set_font(s_result_text, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_alignment(s_result_text, GTextAlignmentCenter);
  text_layer_set_overflow_mode(s_result_text, GTextOverflowModeWordWrap);
  text_layer_set_background_color(s_result_text, GColorClear);
  text_layer_set_text_color(s_result_text, GColorWhite);
  text_layer_set_text(s_result_text, s_result_buf);
  layer_add_child(root, text_layer_get_layer(s_result_text));
}

static void result_window_unload(Window *window) {
  text_layer_destroy(s_result_text);
  window_destroy(window);
  s_result_window = NULL;
}

static void show_cal_result(bool ok, const char *msg) {
  s_result_ok = ok;
  snprintf(s_result_buf, sizeof(s_result_buf), "%s\n%s",
           ok ? "Added to\nCalendar" : "Calendar\nfailed", msg ? msg : "");
  if (s_result_timer) { app_timer_cancel(s_result_timer); s_result_timer = NULL; }
  if (s_result_window) { window_stack_remove(s_result_window, false); }

  s_result_window = window_create();
  window_set_window_handlers(s_result_window, (WindowHandlers) {
    .load = result_window_load,
    .unload = result_window_unload,
  });
  window_stack_push(s_result_window, true);
  if (!ok) vibes_double_pulse();
  s_result_timer = app_timer_register(2600, result_timer_cb, NULL);
}

// ----------------------------------------------------------------- appmessage
static void send_to_calendar(const char *text) {
  DictionaryIterator *it;
  if (app_message_outbox_begin(&it) != APP_MSG_OK) return;
  dict_write_cstring(it, MESSAGE_KEY_NoteText, text);
  app_message_outbox_send();
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *ok_t  = dict_find(iter, MESSAGE_KEY_CalOk);
  Tuple *msg_t = dict_find(iter, MESSAGE_KEY_CalMsg);
  bool ok = ok_t && ok_t->value->cstring[0] == '1';
  show_cal_result(ok, msg_t ? msg_t->value->cstring : (ok ? "Added" : "Failed"));
}

static void outbox_failed_handler(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  show_cal_result(false, "Phone offline");
}

// -------------------------------------------------------------- detail window
static void detail_select_long_handler(ClickRecognizerRef rec, void *context) {
  if (s_detail_index >= 0) {
    delete_note(s_detail_index);
    vibes_double_pulse();
    window_stack_pop(true);            // back to the list (which reloads on appear)
  }
}

static void detail_click_config_provider(void *context) {
  window_long_click_subscribe(BUTTON_ID_SELECT, 0, detail_select_long_handler, NULL);
}

static void detail_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect b = layer_get_bounds(root);

  s_detail_text = text_layer_create(GRect(4, 2, b.size.w - 8, b.size.h - 28));
  text_layer_set_font(s_detail_text, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_overflow_mode(s_detail_text, GTextOverflowModeWordWrap);
  text_layer_set_text(s_detail_text,
      (s_detail_index >= 0 && s_detail_index < s_note_count) ? s_notes[s_detail_index] : "");
  layer_add_child(root, text_layer_get_layer(s_detail_text));

  s_detail_hint = text_layer_create(GRect(0, b.size.h - 24, b.size.w, 24));
  text_layer_set_font(s_detail_hint, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_detail_hint, GTextAlignmentCenter);
  text_layer_set_text(s_detail_hint, "Hold SELECT to delete");
  layer_add_child(root, text_layer_get_layer(s_detail_hint));

  window_set_click_config_provider(window, detail_click_config_provider);
}

static void detail_window_unload(Window *window) {
  text_layer_destroy(s_detail_text);
  text_layer_destroy(s_detail_hint);
  window_destroy(window);
  s_detail_window = NULL;
}

static void show_detail(int idx) {
  s_detail_index = idx;
  s_detail_window = window_create();
  window_set_window_handlers(s_detail_window, (WindowHandlers) {
    .load = detail_window_load,
    .unload = detail_window_unload,
  });
  window_stack_push(s_detail_window, true);
}

// ------------------------------------------------------------------ dictation
static void dictation_handler(DictationSession *session, DictationSessionStatus status,
                              char *transcription, void *context) {
  if (status == DictationSessionStatusSuccess && transcription && transcription[0] != '\0') {
    add_note(transcription);
    if (s_menu_layer) {
      menu_layer_reload_data(s_menu_layer);
    }
    vibes_short_pulse();
    send_to_calendar(transcription);     // every note also goes to Google Calendar
  } else {
    APP_LOG(APP_LOG_LEVEL_INFO, "Dictation not saved (status=%d)", (int)status);
  }
}

// ---------------------------------------------------------------- list window
static uint16_t menu_get_num_rows(MenuLayer *menu, uint16_t section, void *context) {
  return 1 + (s_note_count == 0 ? 1 : s_note_count);   // row 0 = New Note
}

static int16_t menu_get_header_height(MenuLayer *menu, uint16_t section, void *context) {
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void menu_draw_header(GContext *ctx, const Layer *cell_layer, uint16_t section, void *context) {
  menu_cell_basic_header_draw(ctx, cell_layer, "Voice Notes");
}

static void menu_draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *context) {
  int row = cell_index->row;
  if (row == 0) {
    menu_cell_basic_draw(ctx, cell_layer, "New Note", "Dictate + add to Calendar", NULL);
  } else if (s_note_count == 0) {
    menu_cell_basic_draw(ctx, cell_layer, "No notes yet", "Press SELECT above", NULL);
  } else {
    menu_cell_basic_draw(ctx, cell_layer, s_notes[row - 1], NULL, NULL);
  }
}

static void menu_select(MenuLayer *menu, MenuIndex *cell_index, void *context) {
  int row = cell_index->row;
  if (row == 0) {
    if (!s_dictation) {
      s_dictation = dictation_session_create(NOTE_MAX_LEN, dictation_handler, NULL);
    }
    if (s_dictation) {
      dictation_session_start(s_dictation);
    } else {
      vibes_short_pulse();
    }
  } else if (s_note_count > 0) {
    show_detail(row - 1);
  }
}

static void list_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect b = layer_get_bounds(root);

  s_menu_layer = menu_layer_create(b);
  menu_layer_set_callbacks(s_menu_layer, NULL, (MenuLayerCallbacks) {
    .get_num_rows      = menu_get_num_rows,
    .get_header_height = menu_get_header_height,
    .draw_header       = menu_draw_header,
    .draw_row          = menu_draw_row,
    .select_click      = menu_select,
  });
  menu_layer_set_click_config_onto_window(s_menu_layer, window);
#if defined(PBL_COLOR)
  menu_layer_set_highlight_colors(s_menu_layer, GColorBlue, GColorWhite);
#endif
  layer_add_child(root, menu_layer_get_layer(s_menu_layer));
}

static void list_window_unload(Window *window) {
  menu_layer_destroy(s_menu_layer);
  s_menu_layer = NULL;
}

static void list_window_appear(Window *window) {
  if (s_menu_layer) {
    menu_layer_reload_data(s_menu_layer);
  }
}

// ----------------------------------------------------------------- lifecycle
static void init(void) {
  load_notes();
  s_dictation = dictation_session_create(NOTE_MAX_LEN, dictation_handler, NULL);

  app_message_register_inbox_received(inbox_received_handler);
  app_message_register_outbox_failed(outbox_failed_handler);
  app_message_open(256, 256);

  s_list_window = window_create();
  window_set_window_handlers(s_list_window, (WindowHandlers) {
    .load   = list_window_load,
    .unload = list_window_unload,
    .appear = list_window_appear,
  });
  window_stack_push(s_list_window, true);
}

static void deinit(void) {
  if (s_dictation) {
    dictation_session_destroy(s_dictation);
  }
  window_destroy(s_list_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
