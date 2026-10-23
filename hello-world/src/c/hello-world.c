#include <pebble.h>

static Window *s_window;
static TextLayer *s_hello_layer;
static TextLayer *s_time_layer;

static void prv_update_time(struct tm *tick_time) {
  // Must be static: the TextLayer keeps a pointer to this buffer.
  static char s_buffer[8];
  strftime(s_buffer, sizeof(s_buffer), clock_is_24h_style() ? "%H:%M" : "%I:%M", tick_time);
  text_layer_set_text(s_time_layer, s_buffer);
}

static void prv_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  prv_update_time(tick_time);
}

static void prv_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_hello_layer = text_layer_create(GRect(0, 30, bounds.size.w, 30));
  text_layer_set_background_color(s_hello_layer, GColorClear);
  text_layer_set_text_color(s_hello_layer, GColorWhite);
  text_layer_set_font(s_hello_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(s_hello_layer, GTextAlignmentCenter);
  text_layer_set_text(s_hello_layer, "Hello, World!");
  layer_add_child(window_layer, text_layer_get_layer(s_hello_layer));

  s_time_layer = text_layer_create(GRect(0, 70, bounds.size.w, 50));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, GColorWhite);
  text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD));
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_time_layer));
}

static void prv_window_unload(Window *window) {
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_hello_layer);
}

static void prv_init(void) {
  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload,
  });
  window_stack_push(s_window, true);

  // Show the time immediately, then refresh once a minute.
  time_t now = time(NULL);
  prv_update_time(localtime(&now));
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
