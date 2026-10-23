#include <pebble.h>

// Elevação: the time as four isometric blocks standing on pillars, a nod to
// the "Elevation" watch face, with Mínimo's Portuguese weekday and date on top.
//
// Each digit is a 3x5 block glyph on the top face of a pillar. Pillars sit on
// a 2x2 grid: hours along the back-left edge, minutes along the front-right
// edge. Each pillar is raised by its digit's value, hence the name.

static const char *const WEEKDAYS[] = {
  "domingo", "segunda-feira", "terça-feira", "quarta-feira",
  "quinta-feira", "sexta-feira", "sábado",
};

static const char *const MONTHS[] = {
  "janeiro", "fevereiro", "março", "abril", "maio", "junho",
  "julho", "agosto", "setembro", "outubro", "novembro", "dezembro",
};

// One row per glyph line, top to bottom; bit 2 is the leftmost column.
static const uint8_t GLYPHS[10][5] = {
  {7, 5, 5, 5, 7}, {2, 2, 2, 2, 2}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1},
  {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7},
};

// Projection: ground point (u, v) maps to screen (OX + (u + v) * SX, OY + (u - v) * SY).
// u runs down-right and is the glyph's left-to-right axis; v runs up-right and
// is the glyph's bottom-to-top axis.
#define SX 3
#define SY 2
#define OX 6
#define OY 124
#define CELL 2                          // glyph cell size, in ground units
#define MARGIN 1                        // blank border around the glyph on the face
#define FACE_U (3 * CELL + 2 * MARGIN)
#define FACE_V (5 * CELL + 2 * MARGIN)
#define GAP 2                           // ground units between pillars
#define STEP 2                          // pixels of elevation per unit of digit value

static Window *s_window;
static Layer *s_canvas_layer;
static TextLayer *s_day_layer;
static TextLayer *s_date_layer;
static uint8_t s_digits[4];

static void prv_set_pixel(GBitmap *fb, int x, int y, bool white) {
  GBitmapDataRowInfo row = gbitmap_get_data_row_info(fb, y);
  if (x < row.min_x || x > row.max_x) {
    return;
  }
  if (gbitmap_get_format(fb) == GBitmapFormat1Bit) {
    uint8_t mask = 1 << (x % 8);
    if (white) {
      row.data[x / 8] |= mask;
    } else {
      row.data[x / 8] &= ~mask;
    }
  } else {
    row.data[x] = white ? GColorWhiteARGB8 : GColorBlackARGB8;
  }
}

static bool prv_glyph_lit(uint8_t digit, int col, int row) {
  return col >= 0 && col < 3 && row >= 0 && row < 5 && (GLYPHS[digit][row] >> (2 - col)) & 1;
}

// Draws one pillar whose top face has its back corner at ground (u0, v0).
// Painter's order: later calls draw over earlier ones.
static void prv_draw_pillar(GBitmap *fb, GSize size, int u0, int v0, uint8_t digit) {
  const int bx = OX + (u0 + v0) * SX;
  const int by = OY + (u0 - v0) * SY - STEP * digit;
  const int k = 2 * SX * SY;

  for (int dx = 0; dx < (FACE_U + FACE_V) * SX; dx++) {
    const int x = bx + dx;
    if (x < 0 || x >= size.w) {
      continue;
    }
    // Upper and lower outline of the top face in this column; the walls hang below it.
    const int y_top = dx <= FACE_V * SX ? by - dx * SY / SX
                                        : by - FACE_V * SY + (dx - FACE_V * SX) * SY / SX;
    const int y_bot = dx <= FACE_U * SX ? by + dx * SY / SX
                                        : by + FACE_U * SY - (dx - FACE_U * SX) * SY / SX;
    const int y_start = y_top < 0 ? 0 : y_top;
    const bool left_wall = dx < FACE_U * SX;

    for (int y = y_start; y < size.h; y++) {
      bool white;
      if (y < y_bot) {
        // Top face: white outline, white glyph cells, black elsewhere.
        // Invert the projection; u and v come out scaled by k.
        const int a = dx * SY;
        const int b = (y - by) * SX;
        const int u = a + b - MARGIN * k;
        const int v = a - b - MARGIN * k;
        const int col = u >= 0 ? u / (CELL * k) : -1;
        const int row = v >= 0 ? 4 - v / (CELL * k) : -1;
        white = y == y_start || y == y_bot - 1 || prv_glyph_lit(digit, col, row);
      } else if (left_wall) {
        white = (x + y) % 2 == 0;          // 50% checkerboard
      } else {
        white = x % 2 == 0 && y % 2 == 0;  // 25% dots
      }
      prv_set_pixel(fb, x, y, white);
    }
  }
}

static void prv_canvas_update_proc(Layer *layer, GContext *ctx) {
  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (!fb) {
    return;
  }
  const GSize size = gbitmap_get_bounds(fb).size;
  // Back to front: hour ones (back), hour tens (left), minute ones (right), minute tens (front).
  prv_draw_pillar(fb, size, 0, FACE_V + GAP, s_digits[1]);
  prv_draw_pillar(fb, size, 0, 0, s_digits[0]);
  prv_draw_pillar(fb, size, FACE_U + GAP, FACE_V + GAP, s_digits[3]);
  prv_draw_pillar(fb, size, FACE_U + GAP, 0, s_digits[2]);
  graphics_release_frame_buffer(ctx, fb);
}

static void prv_update_time(struct tm *tick_time) {
  int hour = tick_time->tm_hour;
  if (!clock_is_24h_style()) {
    hour = hour % 12 ? hour % 12 : 12;
  }
  s_digits[0] = hour / 10;
  s_digits[1] = hour % 10;
  s_digits[2] = tick_time->tm_min / 10;
  s_digits[3] = tick_time->tm_min % 10;
  layer_mark_dirty(s_canvas_layer);
}

static void prv_update_date(struct tm *tick_time) {
  static char s_date_buffer[24];
  snprintf(s_date_buffer, sizeof(s_date_buffer), "%d de %s", tick_time->tm_mday, MONTHS[tick_time->tm_mon]);
  text_layer_set_text(s_day_layer, WEEKDAYS[tick_time->tm_wday]);
  text_layer_set_text(s_date_layer, s_date_buffer);
}

static void prv_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  prv_update_time(tick_time);
  if (units_changed & DAY_UNIT) {
    prv_update_date(tick_time);
  }
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

  s_canvas_layer = layer_create(bounds);
  layer_set_update_proc(s_canvas_layer, prv_canvas_update_proc);
  layer_add_child(window_layer, s_canvas_layer);

  s_day_layer = prv_text_layer_create(window_layer, GRect(0, 0, w, 28), FONT_KEY_GOTHIC_24_BOLD);
  s_date_layer = prv_text_layer_create(window_layer, GRect(0, 26, w, 24), FONT_KEY_GOTHIC_18);
}

static void prv_window_unload(Window *window) {
  text_layer_destroy(s_date_layer);
  text_layer_destroy(s_day_layer);
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
