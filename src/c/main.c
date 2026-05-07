#include <pebble.h>

#define PERSIST_KEY_TEMP_C 1
#define PERSIST_KEY_UNIT   2

static Window    *s_main_window;
static TextLayer *s_battery_layer;
static TextLayer *s_temp_layer;
static TextLayer *s_time_layer;
static TextLayer *s_date_layer;
static TextLayer *s_hr_layer;
static TextLayer *s_steps_layer;
static Layer     *s_quiet_layer;

static char s_battery_buf[8];
static char s_temp_buf[12];
static char s_time_buf[12];
static char s_date_buf[8];
static char s_hr_buf[12];
static char s_steps_buf[16];

static bool s_have_temp    = false;
static bool s_quiet_active = false;

static void format_with_commas(int value, char *buf, size_t bufsize) {
  char tmp[16];
  snprintf(tmp, sizeof(tmp), "%d", value);
  int len = (int)strlen(tmp);
  int commas = (len - 1) / 3;
  int new_len = len + commas;
  if (new_len >= (int)bufsize) {
    snprintf(buf, bufsize, "%d", value);
    return;
  }
  buf[new_len] = '\0';
  int j = new_len - 1;
  int count = 0;
  for (int i = len - 1; i >= 0; i--) {
    if (count == 3) {
      buf[j--] = ',';
      count = 0;
    }
    buf[j--] = tmp[i];
    count++;
  }
}

static GColor color_for_battery(int pct) {
  if (pct <= 20) return GColorRed;
  if (pct <= 50) return GColorChromeYellow;
  return GColorMintGreen;
}

static GColor color_for_temp_f(int temp_f) {
  if (temp_f < 40)  return GColorVividCerulean;
  if (temp_f < 60)  return GColorPictonBlue;
  if (temp_f < 78)  return GColorMintGreen;
  if (temp_f < 90)  return GColorChromeYellow;
  return GColorRed;
}

// Draws a crescent moon (white body with a black "bite" offset to the upper-right)
// when quiet time is active. Hidden otherwise.
static void quiet_update_proc(Layer *layer, GContext *ctx) {
  if (!s_quiet_active) return;
  GRect b = layer_get_bounds(layer);
  GPoint center = GPoint(b.size.w / 2, b.size.h / 2);
  int r = (b.size.w < b.size.h ? b.size.w : b.size.h) / 2 - 1;

  graphics_context_set_antialiased(ctx, true);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, center, r);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, GPoint(center.x + 4, center.y - 2), r);
}

static void update_quiet(void) {
  bool now_quiet = quiet_time_is_active();
  if (now_quiet != s_quiet_active) {
    s_quiet_active = now_quiet;
    if (s_quiet_layer) layer_mark_dirty(s_quiet_layer);
  }
}

static void update_time(struct tm *tick_time) {
  int h = tick_time->tm_hour;
  const char *ampm = (h < 12) ? "am" : "pm";
  h = h % 12;
  if (h == 0) h = 12;
  snprintf(s_time_buf, sizeof(s_time_buf), "%d:%02d%s", h, tick_time->tm_min, ampm);
  text_layer_set_text(s_time_layer, s_time_buf);

  strftime(s_date_buf, sizeof(s_date_buf), "%m/%d", tick_time);
  text_layer_set_text(s_date_layer, s_date_buf);
}

static void update_battery(BatteryChargeState state) {
  snprintf(s_battery_buf, sizeof(s_battery_buf), "%d%%", state.charge_percent);
  text_layer_set_text(s_battery_layer, s_battery_buf);
  text_layer_set_text_color(s_battery_layer, color_for_battery(state.charge_percent));
}

static void update_health(void) {
  if (health_service_metric_accessible(HealthMetricHeartRateBPM,
                                       time(NULL), time(NULL))
        & HealthServiceAccessibilityMaskAvailable) {
    HealthValue bpm = health_service_peek_current_value(HealthMetricHeartRateBPM);
    if (bpm > 0) {
      snprintf(s_hr_buf, sizeof(s_hr_buf), "%d bpm", (int)bpm);
    } else {
      snprintf(s_hr_buf, sizeof(s_hr_buf), "-- bpm");
    }
  } else {
    snprintf(s_hr_buf, sizeof(s_hr_buf), "-- bpm");
  }
  text_layer_set_text(s_hr_layer, s_hr_buf);

  time_t start = time_start_of_today();
  time_t end = time(NULL);
  if (health_service_metric_accessible(HealthMetricStepCount, start, end)
        & HealthServiceAccessibilityMaskAvailable) {
    HealthValue steps = health_service_sum_today(HealthMetricStepCount);
    char num[12];
    format_with_commas((int)steps, num, sizeof(num));
    snprintf(s_steps_buf, sizeof(s_steps_buf), "%s steps", num);
  } else {
    snprintf(s_steps_buf, sizeof(s_steps_buf), "-- steps");
  }
  text_layer_set_text(s_steps_layer, s_steps_buf);
}

static void update_temp_display(void) {
  if (!s_have_temp && !persist_exists(PERSIST_KEY_TEMP_C)) {
    snprintf(s_temp_buf, sizeof(s_temp_buf), "--");
    text_layer_set_text(s_temp_layer, s_temp_buf);
    text_layer_set_text_color(s_temp_layer, GColorLightGray);
    return;
  }

  int temp_c = persist_read_int(PERSIST_KEY_TEMP_C);
  char unit = 'F';
  if (persist_exists(PERSIST_KEY_UNIT)) {
    char unit_str[4];
    persist_read_string(PERSIST_KEY_UNIT, unit_str, sizeof(unit_str));
    if (unit_str[0] == 'C' || unit_str[0] == 'c') unit = 'C';
  }

  int temp_f = ((temp_c * 9) / 5) + 32;
  int display_temp = (unit == 'C') ? temp_c : temp_f;
  snprintf(s_temp_buf, sizeof(s_temp_buf), "%d\u00B0%c", display_temp, unit);
  text_layer_set_text(s_temp_layer, s_temp_buf);
  text_layer_set_text_color(s_temp_layer, color_for_temp_f(temp_f));
}

static void request_weather(void) {
  DictionaryIterator *iter;
  AppMessageResult result = app_message_outbox_begin(&iter);
  if (result != APP_MSG_OK) return;
  dict_write_uint8(iter, MESSAGE_KEY_FETCH_WEATHER, 1);
  app_message_outbox_send();
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *temp_tuple = dict_find(iter, MESSAGE_KEY_TEMPERATURE_C);
  Tuple *unit_tuple = dict_find(iter, MESSAGE_KEY_UNIT);

  if (temp_tuple) {
    int temp_c = (int)temp_tuple->value->int32;
    persist_write_int(PERSIST_KEY_TEMP_C, temp_c);
    s_have_temp = true;
  }
  if (unit_tuple) {
    persist_write_string(PERSIST_KEY_UNIT, unit_tuple->value->cstring);
  }
  update_temp_display();
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time(tick_time);
  update_health();
  update_quiet();
  if (tick_time->tm_min % 30 == 0) {
    request_weather();
  }
}

static TextLayer* make_label(GRect frame, GFont font, GTextAlignment align, GColor color) {
  TextLayer *l = text_layer_create(frame);
  text_layer_set_background_color(l, GColorClear);
  text_layer_set_text_color(l, color);
  text_layer_set_font(l, font);
  text_layer_set_text_alignment(l, align);
  return l;
}

static void main_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect b = layer_get_bounds(root);

  GFont small = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GFont mid   = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  GFont big   = fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);

  // Top row: battery (left), quiet-time moon (center, only visible when active),
  // temperature (right). Colors on battery/temp are set dynamically.
  // Center moon reserves a 22x22 slot; battery/temp are squeezed to make room.
  const int moon_size = 22;
  const int moon_x = b.size.w / 2 - moon_size / 2;
  s_battery_layer = make_label(GRect(4, 4, moon_x - 4, 22), small, GTextAlignmentLeft, GColorMintGreen);
  s_quiet_layer   = layer_create(GRect(moon_x, 4, moon_size, moon_size));
  layer_set_update_proc(s_quiet_layer, quiet_update_proc);
  s_temp_layer    = make_label(GRect(moon_x + moon_size, 4, b.size.w - (moon_x + moon_size) - 4, 22),
                               small, GTextAlignmentRight, GColorLightGray);

  // Center: time (white, focal) and date (soft gray, secondary).
  s_time_layer    = make_label(GRect(0,  70, b.size.w, 50), big, GTextAlignmentCenter, GColorWhite);
  s_date_layer    = make_label(GRect(0, 130, b.size.w, 30), mid, GTextAlignmentCenter, GColorLightGray);

  // Bottom: HR (red) and steps (green) — paired health/fitness.
  s_hr_layer      = make_label(GRect(4,          198, b.size.w/2 - 4, 22), small, GTextAlignmentLeft,  GColorRed);
  s_steps_layer   = make_label(GRect(b.size.w/2, 198, b.size.w/2 - 4, 22), small, GTextAlignmentRight, GColorMintGreen);

  layer_add_child(root, text_layer_get_layer(s_battery_layer));
  layer_add_child(root, s_quiet_layer);
  layer_add_child(root, text_layer_get_layer(s_temp_layer));
  layer_add_child(root, text_layer_get_layer(s_time_layer));
  layer_add_child(root, text_layer_get_layer(s_date_layer));
  layer_add_child(root, text_layer_get_layer(s_hr_layer));
  layer_add_child(root, text_layer_get_layer(s_steps_layer));
}

static void main_window_unload(Window *window) {
  text_layer_destroy(s_battery_layer);
  text_layer_destroy(s_temp_layer);
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_date_layer);
  text_layer_destroy(s_hr_layer);
  text_layer_destroy(s_steps_layer);
  layer_destroy(s_quiet_layer);
}

static void init(void) {
  s_main_window = window_create();
  window_set_background_color(s_main_window, GColorBlack);
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload,
  });
  window_stack_push(s_main_window, true);

  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  update_time(t);
  update_health();
  update_battery(battery_state_service_peek());
  update_temp_display();
  update_quiet();

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  battery_state_service_subscribe(update_battery);

  app_message_register_inbox_received(inbox_received_handler);
  app_message_open(128, 128);

  request_weather();
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
