#include <pebble.h>

// Weekday and month names are hard-coded rather than taken from strftime's
// %A/%B so the face is always in Portuguese, whatever the phone's language.
static const char *const WEEKDAYS[] = {
  "domingo", "segunda-feira", "terça-feira", "quarta-feira",
  "quinta-feira", "sexta-feira", "sábado",
};

static const char *const MONTHS[] = {
  "janeiro", "fevereiro", "março", "abril", "maio", "junho",
  "julho", "agosto", "setembro", "outubro", "novembro", "dezembro",
};

#define RULE_WIDTH 24

static Window *s_window;
static TextLayer *s_time_layer;
static Layer *s_rule_layer;
static TextLayer *s_day_layer;
static TextLayer *s_date_layer;

static void prv_update_time(struct tm *tick_time) {
  static char s_time_buffer[8];
  const bool is_24h = clock_is_24h_style();
  strftime(s_time_buffer, sizeof(s_time_buffer), is_24h ? "%H:%M" : "%I:%M", tick_time);
  // Drop the leading zero in 12-hour mode ("9:41", not "09:41").
  char *time_text = s_time_buffer;
  if (!is_24h && time_text[0] == '0') {
    time_text++;
  }
  text_layer_set_text(s_time_layer, time_text);
}

static void prv_update_date(struct tm *tick_time) {
  // Redraw whenever the calendar day differs from the one shown. DAY_UNIT alone
  // misses backward clock changes (setting the time back, travelling west).
  static int s_shown_day = -1;
  const int day = tick_time->tm_year * 366 + tick_time->tm_yday;
  if (day == s_shown_day) {
    return;
  }
  s_shown_day = day;

  static char s_date_buffer[24];
  snprintf(s_date_buffer, sizeof(s_date_buffer), "%d de %s", tick_time->tm_mday, MONTHS[tick_time->tm_mon]);
  text_layer_set_text(s_day_layer, WEEKDAYS[tick_time->tm_wday]);
  text_layer_set_text(s_date_layer, s_date_buffer);
}

static void prv_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  prv_update_time(tick_time);
  prv_update_date(tick_time);
}

static void prv_rule_update_proc(Layer *layer, GContext *ctx) {
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);
}

static TextLayer *prv_text_layer_create(Layer *parent, GRect frame, const char *font_key) {
  TextLayer *layer = text_layer_create(frame);
  text_layer_set_background_color(layer, GColorClear);
  text_layer_set_text_color(layer, GColorWhite);
  text_layer_set_font(layer, fonts_get_system_font(font_key));
  text_layer_set_text_alignment(layer, GTextAlignmentCenter);
  layer_add_child(parent, text_layer_get_layer(layer));
  return layer;
}

static void prv_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  int16_t w = bounds.size.w;

  s_time_layer = prv_text_layer_create(window_layer, GRect(0, 38, w, 50), FONT_KEY_LECO_42_NUMBERS);

  s_rule_layer = layer_create(GRect((w - RULE_WIDTH) / 2, 98, RULE_WIDTH, 2));
  layer_set_update_proc(s_rule_layer, prv_rule_update_proc);
  layer_add_child(window_layer, s_rule_layer);

  s_day_layer = prv_text_layer_create(window_layer, GRect(0, 106, w, 28), FONT_KEY_GOTHIC_24_BOLD);
  s_date_layer = prv_text_layer_create(window_layer, GRect(0, 134, w, 24), FONT_KEY_GOTHIC_18);
}

static void prv_window_unload(Window *window) {
  text_layer_destroy(s_date_layer);
  text_layer_destroy(s_day_layer);
  layer_destroy(s_rule_layer);
  text_layer_destroy(s_time_layer);
}

static void prv_init(void) {
  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload,
  });
  window_stack_push(s_window, true);

  time_t now = time(NULL);
  struct tm *tick_time = localtime(&now);
  prv_update_time(tick_time);
  prv_update_date(tick_time);
  tick_timer_service_subscribe(MINUTE_UNIT, prv_tick_handler);
}

static void prv_deinit(void) {
  tick_timer_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
