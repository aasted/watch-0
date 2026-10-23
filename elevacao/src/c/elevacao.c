#include <pebble.h>

// Elevação: a nod to the "Elevation" watch face, with Mínimo's Portuguese
// weekday and date on top.
//
// The time is four 3x5 block digits seen from an isometric angle. Every lit
// cell of a digit is a solid column, so the number itself is the raised
// surface, with its walls falling away to the bottom of the screen. All four
// digits share one plane; hours run along the back-left, minutes along the
// front-right. The view is 2:1 pixel isometric.

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
#define SX 2
#define SY 1
#define OX 2
#define OY 104
#define CELL 4                          // edge of one glyph cell's column, in ground units
#define DIGIT_U (3 * CELL)
#define DIGIT_V (5 * CELL)
#define GAP 3                           // ground units between digits
#define MAX_COLUMNS (4 * 15)

typedef struct {
  int16_t u, v;
} Column;

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

// Draws one glyph cell as a solid column: a white top face
// and two walls (dithered and black) down to the bottom of the screen. Painter's order:
// later calls draw over earlier ones.
static void prv_draw_column(GBitmap *fb, GSize size, const Column *column) {
  const int bx = OX + (column->u + column->v) * SX;
  const int by = OY + (column->u - column->v) * SY;

  for (int dx = 0; dx < 2 * CELL * SX; dx++) {
    const int x = bx + dx;
    if (x < 0 || x >= size.w) {
      continue;
    }
    // The top face is a diamond: its upper and lower edge in this column.
    const int rise = dx <= CELL * SX ? dx : 2 * CELL * SX - dx;
    const int y_top = by - rise * SY / SX;
    const int y_bot = by + rise * SY / SX;
    const bool left_wall = dx < CELL * SX;

    for (int y = y_top < 0 ? 0 : y_top; y < size.h; y++) {
      bool white;
      if (y <= y_bot) {
        // Top face. Including the y_bot row closes the 1px seams that integer
        // rounding would otherwise leave between neighbouring columns.
        white = true;
      } else if (left_wall) {
        white = (x + y) % 2 == 0;          // lit wall: 50% checkerboard
      } else {
        white = false;                     // shaded wall: solid black
      }
      prv_set_pixel(fb, x, y, white);
    }
  }
}

static void prv_canvas_update_proc(Layer *layer, GContext *ctx) {
  // Ground position of each digit's bottom-left glyph cell.
  static const int16_t DIGIT_ORIGIN[4][2] = {
    {0, 0}, {0, DIGIT_V + GAP}, {DIGIT_U + GAP, 0}, {DIGIT_U + GAP, DIGIT_V + GAP},
  };

  Column columns[MAX_COLUMNS];
  int count = 0;
  for (int i = 0; i < 4; i++) {
    const uint8_t digit = s_digits[i];
    for (int row = 0; row < 5; row++) {
      for (int col = 0; col < 3; col++) {
        if ((GLYPHS[digit][row] >> (2 - col)) & 1) {
          columns[count++] = (Column) {
            .u = DIGIT_ORIGIN[i][0] + col * CELL,
            .v = DIGIT_ORIGIN[i][1] + (4 - row) * CELL,
          };
        }
      }
    }
  }

  // Back to front: columns further back (smaller u - v) are drawn first.
  for (int i = 1; i < count; i++) {
    const Column column = columns[i];
    int j = i - 1;
    while (j >= 0 && columns[j].u - columns[j].v > column.u - column.v) {
      columns[j + 1] = columns[j];
      j--;
    }
    columns[j + 1] = column;
  }

  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (!fb) {
    return;
  }
  const GSize size = gbitmap_get_bounds(fb).size;
  for (int i = 0; i < count; i++) {
    prv_draw_column(fb, size, &columns[i]);
  }
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

static TextLayer *prv_text_layer_create(Layer *parent, GRect frame, const char *font_key) {
  TextLayer *layer = text_layer_create(frame);
  text_layer_set_background_color(layer, GColorClear);
  text_layer_set_text_color(layer, GColorWhite);
  text_layer_set_font(layer, fonts_get_system_font(font_key));
  text_layer_set_text_alignment(layer, GTextAlignmentLeft);
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

  // Left-aligned into the empty top-left corner above the blocks.
  s_day_layer = prv_text_layer_create(window_layer, GRect(6, 0, w - 6, 28), FONT_KEY_GOTHIC_24_BOLD);
  s_date_layer = prv_text_layer_create(window_layer, GRect(6, 26, w - 6, 24), FONT_KEY_GOTHIC_18);
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
