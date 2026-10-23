#include <pebble.h>

// F-91W: a homage to the Casio F-91W, with Portuguese abbreviated weekdays
// (Do, 2ª, 3ª, 4ª, 5ª, 6ª, Sa).
//
// The LCD is drawn by hand with tapered segments: seven-segment digits for the
// time and date, and a richer alphanumeric segment set for the weekday field,
// like the original's.

// Segment bits: a (top), b (top right), c (bottom right), d (bottom),
// e (bottom left), f (top left), g (middle, in two halves).
#define SEG_A 0x0001
#define SEG_B 0x0002
#define SEG_C 0x0004
#define SEG_D 0x0008
#define SEG_E 0x0010
#define SEG_F 0x0020
#define SEG_G1 0x0040  // middle, left half
#define SEG_G2 0x0080  // middle, right half
#define SEG_G (SEG_G1 | SEG_G2)
#define SEG_BLANK 0x0000

static const uint16_t DIGIT_SEGMENTS[10] = {
  SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,
  SEG_B | SEG_C,
  SEG_A | SEG_B | SEG_G | SEG_E | SEG_D,
  SEG_A | SEG_B | SEG_G | SEG_C | SEG_D,
  SEG_F | SEG_G | SEG_B | SEG_C,
  SEG_A | SEG_F | SEG_G | SEG_C | SEG_D,
  SEG_A | SEG_F | SEG_G | SEG_E | SEG_D | SEG_C,
  SEG_A | SEG_B | SEG_C,
  SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G,
  SEG_A | SEG_B | SEG_C | SEG_D | SEG_F | SEG_G,
};

// Weekday characters can light just one half of the middle bar, and "D" and
// "ª" are custom glyphs, as the original's day field has its own segment set.

#define ALPHA_2 (SEG_A | SEG_B | SEG_G1 | SEG_G2 | SEG_E | SEG_D)
#define ALPHA_3 (SEG_A | SEG_B | SEG_G2 | SEG_C | SEG_D)
#define ALPHA_4 (SEG_F | SEG_G1 | SEG_G2 | SEG_B | SEG_C)
#define ALPHA_5 (SEG_A | SEG_F | SEG_G1 | SEG_G2 | SEG_C | SEG_D)
#define ALPHA_6 (SEG_A | SEG_F | SEG_E | SEG_D | SEG_C | SEG_G1 | SEG_G2)
#define ALPHA_LOWER_A (SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_G)
#define ALPHA_LOWER_O (SEG_C | SEG_D | SEG_E | SEG_G)
#define ALPHA_S (SEG_A | SEG_F | SEG_G1 | SEG_G2 | SEG_C | SEG_D)
#define ALPHA_D 0xFFFE        // custom glyph
#define ALPHA_ORDINAL 0xFFFF  // "ª", custom glyph

// Weekday field, Sunday first.
static const uint16_t WEEKDAY_SEGMENTS[7][2] = {
  {ALPHA_D, ALPHA_LOWER_O}, {ALPHA_2, ALPHA_ORDINAL}, {ALPHA_3, ALPHA_ORDINAL}, {ALPHA_4, ALPHA_ORDINAL},
  {ALPHA_5, ALPHA_ORDINAL}, {ALPHA_6, ALPHA_ORDINAL}, {ALPHA_S, ALPHA_LOWER_A},
};

// LCD window and character geometry, in pixels.
#define LCD_X 6
#define LCD_Y 34
#define LCD_W 132
#define LCD_H 86

#define BIG_W 24
#define BIG_H 42
#define BIG_T 5
#define BIG_Y 70

#define SMALL_W 14
#define SMALL_H 22
#define SMALL_T 3
#define TOP_Y 42

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

// A horizontal segment: a hexagon from x0 to x1, centred on row y, with
// pointed ends.
static void prv_draw_hseg(int x0, int x1, int y, int t) {
  for (int dy = -t / 2; dy <= t / 2; dy++) {
    const int inset = dy < 0 ? -dy : dy;
    for (int x = x0 + inset; x <= x1 - inset; x++) {
      prv_set_black(x, y + dy);
    }
  }
}

// A vertical segment: a hexagon from y0 to y1, centred on column x.
static void prv_draw_vseg(int x, int y0, int y1, int t) {
  for (int dx = -t / 2; dx <= t / 2; dx++) {
    const int inset = dx < 0 ? -dx : dx;
    for (int y = y0 + inset; y <= y1 - inset; y++) {
      prv_set_black(x + dx, y);
    }
  }
}

// Draws one segment character in the box (x, y, w, h) with stroke t.
static void prv_draw_char(int x, int y, int w, int h, int t, uint16_t segments) {
  const int half = t / 2;
  const int left = x + half;
  const int right = x + w - 1 - half;
  const int centre = x + w / 2;
  const int top = y + half;
  const int middle = y + h / 2;
  const int bottom = y + h - 1 - half;
  const int gap = 1;

  if (segments & SEG_A) prv_draw_hseg(left + gap, right - gap, top, t);
  if (segments & SEG_B) prv_draw_vseg(right, top + gap, middle - gap, t);
  if (segments & SEG_C) prv_draw_vseg(right, middle + gap, bottom - gap, t);
  if (segments & SEG_D) prv_draw_hseg(left + gap, right - gap, bottom, t);
  if (segments & SEG_E) prv_draw_vseg(left, middle + gap, bottom - gap, t);
  if (segments & SEG_F) prv_draw_vseg(left, top + gap, middle - gap, t);
  if ((segments & SEG_G1) && (segments & SEG_G2)) {
    prv_draw_hseg(left + gap, right - gap, middle, t);
  } else if (segments & SEG_G1) {
    prv_draw_hseg(left + gap, centre - gap, middle, t);
  } else if (segments & SEG_G2) {
    prv_draw_hseg(centre + gap, right - gap, middle, t);
  }
}

// "ª": a small raised single-storey "a" over an underline, filling the same
// box as a weekday character. A thinner stroke keeps the small "a" open.
static void prv_draw_ordinal(int x, int y, int w, int h) {
  const int t = 2;
  const int a_w = w - 4;
  const int a_h = h * 3 / 5;
  prv_draw_char(x + 2, y, a_w, a_h, t, ALPHA_LOWER_A);
  prv_draw_hseg(x + 2, x + 2 + a_w - 1, y + h - 2, t);
}

// "D": an unbroken left stroke and cut-off right corners, so it reads as a
// letter rather than a "0".
static void prv_draw_letter_d(int x, int y, int w, int h, int t) {
  const int half = t / 2;
  const int left = x + half;
  const int right = x + w - 1 - half;
  const int top = y + half;
  const int bottom = y + h - 1 - half;
  const int corner = 4;

  prv_draw_vseg(left, top + 1, bottom - 1, t);
  prv_draw_hseg(left + 1, right - corner, top, t);
  prv_draw_hseg(left + 1, right - corner, bottom, t);
  prv_draw_vseg(right, top + corner, bottom - corner, t);
  for (int k = 0; k <= corner; k++) {
    for (int dx = -half; dx <= half; dx++) {
      for (int dy = -half; dy <= half; dy++) {
        prv_set_black(right - corner + k + dx, top + k + dy);
        prv_set_black(right - corner + k + dx, bottom - k + dy);
      }
    }
  }
}

static void prv_draw_weekday_char(int x, uint16_t segments) {
  if (segments == ALPHA_D) {
    prv_draw_letter_d(x, TOP_Y, SMALL_W, SMALL_H, SMALL_T);
  } else if (segments == ALPHA_ORDINAL) {
    prv_draw_ordinal(x, TOP_Y, SMALL_W, SMALL_H);
  } else {
    prv_draw_char(x, TOP_Y, SMALL_W, SMALL_H, SMALL_T, segments);
  }
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
  const uint16_t *weekday = WEEKDAY_SEGMENTS[s_now.tm_wday];
  prv_draw_weekday_char(14, weekday[0]);
  prv_draw_weekday_char(31, weekday[1]);
  const int mday = s_now.tm_mday;
  prv_draw_char(100, TOP_Y, SMALL_W, SMALL_H, SMALL_T, mday >= 10 ? DIGIT_SEGMENTS[mday / 10] : SEG_BLANK);
  prv_draw_char(117, TOP_Y, SMALL_W, SMALL_H, SMALL_T, DIGIT_SEGMENTS[mday % 10]);

  // Main row: HH:MM, with the 12-hour leading zero left blank as on the F-91W.
  const bool blank_tens = !is_24h && hour < 10;
  prv_draw_char(14, BIG_Y, BIG_W, BIG_H, BIG_T, blank_tens ? SEG_BLANK : DIGIT_SEGMENTS[hour / 10]);
  prv_draw_char(41, BIG_Y, BIG_W, BIG_H, BIG_T, DIGIT_SEGMENTS[hour % 10]);
  prv_draw_colon(69, BIG_Y, 5);
  prv_draw_char(79, BIG_Y, BIG_W, BIG_H, BIG_T, DIGIT_SEGMENTS[s_now.tm_min / 10]);
  prv_draw_char(106, BIG_Y, BIG_W, BIG_H, BIG_T, DIGIT_SEGMENTS[s_now.tm_min % 10]);

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

// The printing on the case around the LCD.
typedef struct {
  GRect frame;
  const char *text;
  const char *font_key;
  GTextAlignment alignment;
} CaseLabel;

static const CaseLabel CASE_LABELS[] = {
  {{{6, 0}, {40, 12}}, "LIGHT", FONT_KEY_GOTHIC_09, GTextAlignmentLeft},
  {{{40, 0}, {98, 12}}, "ALARM CHRONOGRAPH", FONT_KEY_GOTHIC_09, GTextAlignmentRight},
  {{{6, 10}, {80, 22}}, "CASIO", FONT_KEY_GOTHIC_18_BOLD, GTextAlignmentLeft},
  {{{6, 126}, {132, 14}}, "WATER  WR  RESIST", FONT_KEY_GOTHIC_09, GTextAlignmentCenter},
  {{{6, 146}, {40, 14}}, "MODE", FONT_KEY_GOTHIC_09, GTextAlignmentLeft},
  {{{40, 146}, {98, 14}}, "ALARM ON·OFF / 24HR", FONT_KEY_GOTHIC_09, GTextAlignmentRight},
};

static TextLayer *s_labels[ARRAY_LENGTH(CASE_LABELS)];

static void prv_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_canvas_layer = layer_create(bounds);
  layer_set_update_proc(s_canvas_layer, prv_canvas_update_proc);
  layer_add_child(window_layer, s_canvas_layer);

  for (size_t i = 0; i < ARRAY_LENGTH(CASE_LABELS); i++) {
    const CaseLabel *label = &CASE_LABELS[i];
    s_labels[i] = prv_label_create(window_layer, label->frame, label->text, label->font_key, GColorWhite,
                                   label->alignment);
  }

  // "PM" sits inside the LCD, between the weekday and the date.
  s_pm_layer = prv_label_create(window_layer, GRect(56, TOP_Y + 6, 24, 12), "PM", FONT_KEY_GOTHIC_09,
                                GColorBlack, GTextAlignmentCenter);
}

static void prv_window_unload(Window *window) {
  text_layer_destroy(s_pm_layer);
  for (size_t i = 0; i < ARRAY_LENGTH(CASE_LABELS); i++) {
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
  prv_tick_handler(localtime(&now), MINUTE_UNIT);
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
