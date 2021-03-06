#include "graph_element.h"
#include "layout.h"
#include "preferences.h"
#include "staleness.h"

#define BOLUS_TICK_HEIGHT 7

static GPoint center_of_point(int x, int y) {
  if (get_prefs()->point_shape == POINT_SHAPE_CIRCLE) {
    return GPoint(x + get_prefs()->point_width / 2, y + get_prefs()->point_width / 2);
  } else {
    return GPoint(x + get_prefs()->point_width / 2, y + get_prefs()->point_rect_height / 2);
  }
}

static void plot_point(int x, int y, GContext *ctx) {
  if (get_prefs()->point_shape == POINT_SHAPE_RECTANGLE) {
    graphics_fill_rect(ctx, GRect(x, y, get_prefs()->point_width, get_prefs()->point_rect_height), 0, GCornerNone);
  } else if (get_prefs()->point_shape == POINT_SHAPE_CIRCLE) {
    graphics_fill_circle(ctx, center_of_point(x, y), get_prefs()->point_width / 2);
  }
}

static void plot_tick(int x, int bottom_y, GContext *ctx) {
  uint8_t width;
  if (get_prefs()->point_width >= 5 && get_prefs()->point_width % 2 == 1) {
    width = 3;
  } else {
    width = 2;
  }
  graphics_fill_rect(ctx, GRect(x + get_prefs()->point_width / 2 - width / 2, bottom_y - BOLUS_TICK_HEIGHT, width, BOLUS_TICK_HEIGHT), 0, GCornerNone);
}

static int bg_to_y(int height, int bg) {
  // Graph lower bound, graph upper bound
  int graph_min = get_prefs()->bottom_of_graph;
  int graph_max = get_prefs()->top_of_graph;
  return (float)height - (float)(bg - graph_min) / (float)(graph_max - graph_min) * (float)height + 0.5f;
}

static int index_to_x(uint8_t i, uint8_t graph_width, uint8_t padding) {
  return graph_width - (get_prefs()->point_width + get_prefs()->point_margin) * (1 + i + padding) + get_prefs()->point_margin - get_prefs()->point_right_margin;
}

static int bg_to_y_for_point(int height, int bg) {
  int min = 0;
  int diameter = get_prefs()->point_shape == POINT_SHAPE_CIRCLE ? get_prefs()->point_width : get_prefs()->point_rect_height;
  int max = height - diameter;

  int y = (float)bg_to_y(height, bg) - diameter / 2.0f + 0.5f;
  if (y < min) {
    return min;
  } else if (y > max) {
    return max;
  } else {
    return y;
  }
}

static void fill_rect_gray(GContext *ctx, GRect bounds, GColor previous_color) {
  graphics_context_set_fill_color(ctx, GColorLightGray);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  graphics_context_set_fill_color(ctx, previous_color);
}

static uint8_t decode_bits(uint8_t value, uint8_t offset, uint8_t bits) {
  return (value >> offset) & (0xff >> (8 - bits));
}

static void graph_update_proc(Layer *layer, GContext *ctx) {
  int i, x, y;
  GSize layer_size = layer_get_bounds(layer).size;
  uint8_t graph_width = layer_size.w;
  uint8_t graph_height = get_prefs()->basal_graph ? layer_size.h - get_prefs()->basal_height : layer_size.h;

  GraphData *data = layer_get_data(layer);
  graphics_context_set_stroke_color(ctx, data->color);
  graphics_context_set_fill_color(ctx, data->color);
  int padding = graph_staleness_padding();

  // Target range bounds
  uint16_t limits[2] = {get_prefs()->top_of_range, get_prefs()->bottom_of_range};
  bool is_top[2] = {true, false};
  for(i = 0; i < (int)ARRAY_LENGTH(limits); i++) {
    y = bg_to_y(graph_height, limits[i]);
    for(x = 0; x < graph_width; x += 2) {
      // Draw bounds symmetrically, on the inside of the range
      if (is_top[i]) {
        fill_rect_gray(ctx, GRect(0, y - 1, graph_width, 4), data->color);
      } else {
        fill_rect_gray(ctx, GRect(0, y - 2, graph_width, 4), data->color);
      }
    }
  }

  // Horizontal gridlines
  int h_gridline_frequency = get_prefs()->h_gridlines;
  if (h_gridline_frequency > 0) {
    int graph_min = get_prefs()->bottom_of_graph;
    int graph_max = get_prefs()->top_of_graph;
    for(int g = 0; g < graph_max; g += h_gridline_frequency) {
      if (g <= graph_min || g == limits[0] || g == limits[1]) {
        continue;
      }
      y = bg_to_y(graph_height, g);
      for(x = 2; x < graph_width; x += 8) {
        graphics_draw_line(ctx, GPoint(x, y), GPoint(x + 1, y));
      }
    }
  }

  if (get_prefs()->plot_line) {
    graphics_context_set_stroke_width(ctx, get_prefs()->plot_line_width);
  }

  // SGVs
  GPoint last_center = GPointZero;
  for(i = 0; i < data->count; i++) {
    // XXX: JS divides by 2 to fit into 1 byte
    int bg = data->sgvs[i] * 2;
    if(bg == 0) {
      continue;
    }
    x = index_to_x(i, graph_width, padding);
    y = bg_to_y_for_point(graph_height, bg);

    // stop plotting if the SGV is off-screen
    if (x < 0) {
      break;
    }

    // line
    if (get_prefs()->plot_line) {
      GPoint center = center_of_point(x, y);
      if (!gpoint_equal(&last_center, &GPointZero)) {
        graphics_draw_line(ctx, center, last_center);
      }
      last_center = center;
    }

    // point
    plot_point(x, y, ctx);
  }

  graphics_context_set_stroke_width(ctx, 1);

  // Boluses
  for(i = 0; i < data->count; i++) {
    bool bolus = decode_bits(data->extra[i], GRAPH_EXTRA_BOLUS_OFFSET, GRAPH_EXTRA_BOLUS_BITS);
    if (bolus) {
      x = index_to_x(i, graph_width, padding);
      plot_tick(x, graph_height, ctx);
    }
  }

  // Basals
  if (get_prefs()->basal_graph) {
    graphics_draw_line(ctx, GPoint(0, graph_height), GPoint(graph_width, graph_height));
    for(i = 0; i < data->count; i++) {
      uint8_t basal = decode_bits(data->extra[i], GRAPH_EXTRA_BASAL_OFFSET, GRAPH_EXTRA_BASAL_BITS);
      x = index_to_x(i, graph_width, padding);
      y = layer_size.h - basal;
      uint8_t width = get_prefs()->point_width + get_prefs()->point_margin;
      if (i == data->count - 1) {
        // if this is the last point to draw, extend its basal data to the left edge
        width += x;
        x = 0;
      }
      graphics_draw_line(ctx, GPoint(x, y), GPoint(x + width - 1, y));
      if (basal > 1) {
        fill_rect_gray(ctx, GRect(x, y + 1, width, basal - 1), data->color);
      }
    }
    if (padding > 0) {
      x = index_to_x(padding - 1, graph_width, 0);
      graphics_fill_rect(ctx, GRect(x, graph_height, graph_width - x, get_prefs()->basal_height), 0, GCornerNone);
    }
  }
}

GraphElement* graph_element_create(Layer *parent) {
  GRect bounds = element_get_bounds(parent);

  Layer* graph_layer = layer_create_with_data(
    GRect(0, 0, bounds.size.w, bounds.size.h),
    sizeof(GraphData)
  );
  ((GraphData*)layer_get_data(graph_layer))->color = element_fg(parent);
  ((GraphData*)layer_get_data(graph_layer))->sgvs = malloc(GRAPH_MAX_SGV_COUNT * sizeof(uint8_t));
  ((GraphData*)layer_get_data(graph_layer))->extra = malloc(GRAPH_MAX_SGV_COUNT * sizeof(uint8_t));
  layer_set_update_proc(graph_layer, graph_update_proc);
  layer_add_child(parent, graph_layer);

  ConnectionStatusComponent *conn_status = connection_status_component_create(parent, 1, 1);

  GraphElement *el = malloc(sizeof(GraphElement));
  el->graph_layer = graph_layer;
  el->conn_status = conn_status;
  return el;
}

void graph_element_destroy(GraphElement *el) {
  free(((GraphData*)layer_get_data(el->graph_layer))->sgvs);
  free(((GraphData*)layer_get_data(el->graph_layer))->extra);
  layer_destroy(el->graph_layer);
  connection_status_component_destroy(el->conn_status);
  free(el);
}

void graph_element_update(GraphElement *el, DataMessage *data) {
  GraphData *graph_data = layer_get_data(el->graph_layer);
  graph_data->count = data->sgv_count;
  memcpy(graph_data->sgvs, data->sgvs, data->sgv_count * sizeof(uint8_t));
  memcpy(graph_data->extra, data->graph_extra, data->sgv_count * sizeof(uint8_t));
  layer_mark_dirty(el->graph_layer);
  connection_status_component_tick(el->conn_status);
}

void graph_element_tick(GraphElement *el) {
  connection_status_component_tick(el->conn_status);
}

void graph_element_show_request_state(GraphElement *el, RequestState state, AppMessageResult reason) {
  connection_status_component_show_request_state(el->conn_status, state, reason);
}
