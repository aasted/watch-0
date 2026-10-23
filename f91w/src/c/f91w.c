#include <pebble.h>

// F-91W: a homage to the Casio F-91W, with Portuguese abbreviated weekdays
// ("2a" for segunda-feira, "3a" for terça-feira, ...).
//
// The LCD is drawn by hand: every digit is seven tapered segments, and the
// segments that are off still show faintly ("ghosting"), as on a real LCD.

// Segment bits: a (top), b (top right), c (bottom right), d (bottom),
// e (bottom left), f (top left), g (middle).
#define SEG_A 0x01
#define SEG_B 0x02
#define SEG_C 0x04
#define SEG_D 0x08
#define SEG_E 0x10
#define SEG_F 0x20
#define SEG_G 0x40
#define SEG_BLANK 0x00

static const uint8_t DIGIT_SEGMENTS[10] = {
  0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F,
};

// Weekday field, Sunday first: "do" (domingo), "2a" ... "6a", "Sa" (sábado).
#define CHAR_a 0x5F
#define CHAR_d 0x5E
#define CHAR_o 0x5C
#define CHAR_S 0x6D
static const uint8_t WEEKDAY_SEGMENTS[7][2] = {
  {CHAR_d, CHAR_o}, {0x5B, CHAR_a}, {0x4F, CHAR_a}, {0x66, CHAR_a},
  {0x6D, CHAR_a}, {0x7D, CHAR_a}, {CHAR_S, CHAR_a},
};

// LCD window and digit geometry, in pixels.
#define LCD_X 6
#define LCD_Y 34
#define LCD_W 132
#define LCD_H 86

#define BIG_W 20
#define BIG_H 38
#define BIG_T 5
#define BIG_Y 74

#define SMALL_W 12
#define SMALL_H 22
#define SMALL_T 3
#define TOP_Y 44
#define SECONDS_Y (BIG_Y + BIG_H - SMALL_H)

// The frame buffer rows, looked up once per redraw: SDK calls are slow on the
// watch, so they must not be made per pixel.
#define MAX_ROWS 168

typedef struct {
  GSize size;
  bool one_bit;
  uint8_t *data[MAX_ROWS];
  int16_t min_x[MAX_ROWS];
  int16_t max_x[MAX_ROWS];
} FrameBuffer;

static Window *s_window;
static Layer *s_canvas_layer;
static TextLayer *s_pm_layer;
static FrameBuffer s_frame;
static struct tm s_now;

static void prv_frame_load(GBitmap *fb) {
  s_frame.size = gbitmap_get_bounds(fb).size;
  if (s_frame.size.h > MAX_ROWS) {
    s_frame.size.h = MAX_ROWS;
  }
  s_frame.one_bit = gbitmap_get_format(fb) == GBitmapFormat1Bit;
  for (int y = 0; y < s_frame.size.h; y++) {
    const GBitmapDataRowInfo row = gbitmap_get_data_row_info(fb, y);
    s_frame.data[y] = row.data;
    s_frame.min_x[y] = row.min_x;
    s_frame.max_x[y] = row.max_x;
  }
}

static void prv_set_black(int x, int y) {
  if (y < 0 || y >= s_frame.size.h || x < s_frame.min_x[y] || x > s_frame.max_x[y]) {
    return;
  }
  if (s_frame.one_bit) {
    s_frame.data[y][x / 8] &= ~(1 << (x % 8));
  } else {
    s_frame.data[y][x] = GColorBlackARGB8;
  }
}

// Lit segments are solid black; unlit ones get a sparse dot pattern so the
// whole "8" stays faintly visible. Small characters get sparser dots, or a
// ghosted segment would make "3a" read as "38".
static int s_ghost_spacing = 3;

static void prv_plot(int x, int y, bool lit) {
  if (lit || (x % s_ghost_spacing == 0 && y % s_ghost_spacing == 0)) {
    prv_set_black(x, y);
  }
}

// A horizontal segment: a hexagon from x0 to x1, centred on row y, with
// pointed ends.
static void prv_draw_hseg(int x0, int x1, int y, int t, bool lit) {
  for (int dy = -t / 2; dy <= t / 2; dy++) {
    const int inset = dy < 0 ? -dy : dy;
    for (int x = x0 + inset; x <= x1 - inset; x++) {
      prv_plot(x, y + dy, lit);
    }
  }
}

// A vertical segment: a hexagon from y0 to y1, centred on column x.
static void prv_draw_vseg(int x, int y0, int y1, int t, bool lit) {
  for (int dx = -t / 2; dx <= t / 2; dx++) {
    const int inset = dx < 0 ? -dx : dx;
    for (int y = y0 + inset; y <= y1 - inset; y++) {
      prv_plot(x + dx, y, lit);
    }
  }
}

// Draws one seven-segment character in the box (x, y, w, h) with stroke t.
static void prv_draw_char(int x, int y, int w, int h, int t, uint8_t segments) {
  const int half = t / 2;
  const int left = x + half;
  const int right = x + w - 1 - half;
  const int top = y + half;
  const int middle = y + h / 2;
  const int bottom = y + h - 1 - half;
  const int gap = 1;
  s_ghost_spacing = h >= BIG_H ? 3 : 5;

  prv_draw_hseg(left + gap, right - gap, top, t, segments & SEG_A);
  prv_draw_vseg(right, top + gap, middle - gap, t, segments & SEG_B);
  prv_draw_vseg(right, middle + gap, bottom - gap, t, segments & SEG_C);
  prv_draw_hseg(left + gap, right - gap, bottom, t, segments & SEG_D);
  prv_draw_vseg(left, middle + gap, bottom - gap, t, segments & SEG_E);
  prv_draw_vseg(left, top + gap, middle - gap, t, segments & SEG_F);
  prv_draw_hseg(left + gap, right - gap, middle, t, segments & SEG_G);
}

static void prv_draw_colon(int x, int y, int size) {
  for (int dy = 0; dy < size; dy++) {
    for (int dx = 0; dx < size; dx++) {
      prv_set_black(x + dx, y + BIG_H / 3 - size / 2 + dy);
      prv_set_black(x + dx, y + 2 * BIG_H / 3 - size / 2 + dy);
    }
  }
}

static void prv_canvas_update_proc(Layer *layer, GContext *ctx) {
  // The LCD window: light background in the black case.
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(LCD_X, LCD_Y, LCD_W, LCD_H), 6, GCornersAll);

  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (!fb) {
    return;
  }
  prv_frame_load(fb);

  const bool is_24h = clock_is_24h_style();
  int hour = s_now.tm_hour;
  if (!is_24h) {
    hour = hour % 12 ? hour % 12 : 12;
  }

  // Top row: weekday on the left, day of the month on the right.
  const uint8_t *weekday = WEEKDAY_SEGMENTS[s_now.tm_wday];
  prv_draw_char(14, TOP_Y, SMALL_W, SMALL_H, SMALL_T, weekday[0]);
  prv_draw_char(29, TOP_Y, SMALL_W, SMALL_H, SMALL_T, weekday[1]);
  const int mday = s_now.tm_mday;
  prv_draw_char(106, TOP_Y, SMALL_W, SMALL_H, SMALL_T, mday >= 10 ? DIGIT_SEGMENTS[mday / 10] : SEG_BLANK);
  prv_draw_char(121, TOP_Y, SMALL_W, SMALL_H, SMALL_T, DIGIT_SEGMENTS[mday % 10]);

  // Main row: HH:MM, with the 12-hour leading zero left blank as on the F-91W.
  const bool blank_tens = !is_24h && hour < 10;
  prv_draw_char(10, BIG_Y, BIG_W, BIG_H, BIG_T, blank_tens ? SEG_BLANK : DIGIT_SEGMENTS[hour / 10]);
  prv_draw_char(33, BIG_Y, BIG_W, BIG_H, BIG_T, DIGIT_SEGMENTS[hour % 10]);
  prv_draw_colon(56, BIG_Y, 4);
  prv_draw_char(63, BIG_Y, BIG_W, BIG_H, BIG_T, DIGIT_SEGMENTS[s_now.tm_min / 10]);
  prv_draw_char(86, BIG_Y, BIG_W, BIG_H, BIG_T, DIGIT_SEGMENTS[s_now.tm_min % 10]);

  // Seconds, small and bottom-aligned with the time.
  prv_draw_char(106, SECONDS_Y, SMALL_W, SMALL_H, SMALL_T, DIGIT_SEGMENTS[s_now.tm_sec / 10]);
  prv_draw_char(121, SECONDS_Y, SMALL_W, SMALL_H, SMALL_T, DIGIT_SEGMENTS[s_now.tm_sec % 10]);

  graphics_release_frame_buffer(ctx, fb);
}

static void prv_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  s_now = *tick_time;
  layer_set_hidden(text_layer_get_layer(s_pm_layer), clock_is_24h_style() || s_now.tm_hour < 12);
  layer_mark_dirty(s_canvas_layer);
}

static TextLayer *prv_label_create(Layer *parent, GRect frame, const char *text, const char *font_key,
                                   GColor color, GTextAlignment alignment) {
  TextLayer *layer = text_layer_create(frame);
  text_layer_set_background_color(layer, GColorClear);
  text_layer_set_text_color(layer, color);
  text_layer_set_font(layer, fonts_get_system_font(font_key));
  text_layer_set_text_alignment(layer, alignment);
  text_layer_set_text(layer, text);
  layer_add_child(parent, text_layer_get_layer(layer));
  return layer;
}

#define MAX_LABELS 6
static TextLayer *s_labels[MAX_LABELS];

static void prv_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_canvas_layer = layer_create(bounds);
  layer_set_update_proc(s_canvas_layer, prv_canvas_update_proc);
  layer_add_child(window_layer, s_canvas_layer);

  // The printing on the case around the LCD.
  int n = 0;
  s_labels[n++] = prv_label_create(window_layer, GRect(6, 0, 40, 12), "LIGHT", FONT_KEY_GOTHIC_09,
                                   GColorWhite, GTextAlignmentLeft);
  s_labels[n++] = prv_label_create(window_layer, GRect(40, 0, 98, 12), "ALARM CHRONOGRAPH",
                                   FONT_KEY_GOTHIC_09, GColorWhite, GTextAlignmentRight);
  s_labels[n++] = prv_label_create(window_layer, GRect(6, 10, 80, 22), "CASIO", FONT_KEY_GOTHIC_18_BOLD,
                                   GColorWhite, GTextAlignmentLeft);
  s_labels[n++] = prv_label_create(window_layer, GRect(6, 126, 132, 14), "WATER  WR  RESIST",
                                   FONT_KEY_GOTHIC_09, GColorWhite, GTextAlignmentCenter);
  s_labels[n++] = prv_label_create(window_layer, GRect(6, 146, 40, 14), "MODE", FONT_KEY_GOTHIC_09,
                                   GColorWhite, GTextAlignmentLeft);
  s_labels[n++] = prv_label_create(window_layer, GRect(40, 146, 98, 14), "ALARM ON·OFF / 24HR",
                                   FONT_KEY_GOTHIC_09, GColorWhite, GTextAlignmentRight);

  // "PM" sits inside the LCD, just above the hours.
  s_pm_layer = prv_label_create(window_layer, GRect(44, TOP_Y + 6, 20, 12), "PM", FONT_KEY_GOTHIC_09,
                                GColorBlack, GTextAlignmentLeft);
}

static void prv_window_unload(Window *window) {
  text_layer_destroy(s_pm_layer);
  for (int i = 0; i < MAX_LABELS; i++) {
    text_layer_destroy(s_labels[i]);
  }
  layer_destroy(s_canvas_layer);
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
  prv_tick_handler(localtime(&now), SECOND_UNIT);
  tick_timer_service_subscribe(SECOND_UNIT, prv_tick_handler);
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
