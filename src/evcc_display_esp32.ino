/*
 * EVCC Display for ESP32 with ST7796 TFT
 * C++ version with LVGL, WiFi, and HTTP polling
 * Replaces MicroPython version for better memory management
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <esp_task_wdt.h>
#include "wifi_config.h"
#include "config.h"

// Threshold (in Watts) below which values are considered inactive for dimming
#ifndef POWER_ACTIVE_THRESHOLD
#define POWER_ACTIVE_THRESHOLD 10.0f
#endif

// Declare the stripe pattern image
LV_IMG_DECLARE(img_skew_strip);



// WiFi credentials - see wifi_config.h
// Note: Copy wifi_config.h.template to wifi_config.h and configure
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// EVCC server configuration - see wifi_config.h
const char* evcc_host = EVCC_HOST;
const int evcc_port = EVCC_PORT;
const char* combined_path = EVCC_API_PATH;

// Display and LVGL setup
TFT_eSPI tft = TFT_eSPI();
static lv_color_t buf[SCREEN_WIDTH * 10];
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;

EVCCData data;

// Stripe pattern style
lv_style_t stripe_style;
lv_timer_t* stripe_timer = nullptr;
int stripe_offset = 0;

// Loadpoint rotation state
RotationState rotationState;

// UI element references
struct UIElements {
    lv_obj_t* screen;
    lv_obj_t* upper_container;
    lv_obj_t* lower_container;
    
    // Energy row labels (value1, value2) and composite bars
    struct {
        lv_obj_t* desc;
        lv_obj_t* value1;
        lv_obj_t* value2;
    } generation, battery_discharge, grid_feed, consumption, loadpoint, battery_charge, grid_feedin;
    
    // Composite energy bars
    struct {
        lv_obj_t* container;
        lv_obj_t* generation_segment;
        lv_obj_t* battery_out_segment;
        lv_obj_t* grid_in_segment;
        lv_obj_t* generation_label;
        lv_obj_t* battery_out_label;
        lv_obj_t* grid_in_label;
    } in_bar;
    
    struct {
        lv_obj_t* container;
        lv_obj_t* consumption_segment;
        lv_obj_t* loadpoint_segment;
        lv_obj_t* battery_in_segment;
        lv_obj_t* grid_out_segment;
        lv_obj_t* consumption_label;
        lv_obj_t* loadpoint_label;
        lv_obj_t* battery_in_label;
        lv_obj_t* grid_out_label;
    } out_bar;
    
    // Car section elements
    struct {
        lv_obj_t* title_label;
        lv_obj_t* car_label;
        lv_obj_t* power_label;
        lv_obj_t* phase_bg_bars[3];      // Grey background bars (full width)
        lv_obj_t* phase_offered_bars[3]; // Offered current bars (dimmed green)
        lv_obj_t* phase_bars[3];         // Current phase bars (actual charging current, full green)
        lv_obj_t* soc_bar;
        lv_obj_t* plan_soc_marker;
        lv_obj_t* limit_soc_marker;
        lv_obj_t* soc_desc;
        lv_obj_t* plan_desc;
        lv_obj_t* limit_desc;
        lv_obj_t* soc_value;
        lv_obj_t* range_value;
        lv_obj_t* ladedauer_value;
        lv_obj_t* plan_value;
        lv_obj_t* plan_soc_value;
        lv_obj_t* ladelimit_value;
    } car;
};

UIElements ui;

// Common styling functions
void styleLabel(lv_obj_t* label, const lv_font_t* font, lv_color_t color) {
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
}

void styleLabelPrimary(lv_obj_t* label) {
    styleLabel(label, FONT_SECONDARY, lv_color_hex(COLOR_TEXT_PRIMARY));
}

void styleLabelSecondary(lv_obj_t* label) {
    styleLabel(label, FONT_SMALL, lv_color_hex(COLOR_TEXT_SECONDARY));
}

void styleLabelValue(lv_obj_t* label) {
    styleLabel(label, FONT_SECONDARY, lv_color_hex(COLOR_TEXT_VALUE));
}

void styleLabelHeader(lv_obj_t* label) {
    styleLabel(label, FONT_PRIMARY, lv_color_hex(COLOR_TEXT_PRIMARY));
}

void styleContainer(lv_obj_t* container) {
    lv_obj_set_style_bg_color(container, lv_color_hex(COLOR_PANEL_BG), 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, CONTAINER_PAD, 0);
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_OFF);
}

void styleContainerWithBorder(lv_obj_t* container) {
    styleContainer(container);
    lv_obj_set_style_border_width(container, 1, 0);
    lv_obj_set_style_border_color(container, lv_color_hex(COLOR_PANEL_BORDER), 0);
}

void positionAndAlign(lv_obj_t* obj, int x, int y, int width, lv_text_align_t align) {
    lv_obj_set_pos(obj, x, y);
    if (width > 0) {
        lv_obj_set_width(obj, width);
        lv_obj_set_style_text_align(obj, align, 0);
    }
}

// LVGL display driver callback
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t*)&color_p->full, w * h, true);
    tft.endWrite();
    
    lv_disp_flush_ready(disp);
}

// Get active loadpoint data based on rotation logic
LoadpointData* getActiveLoadpoint() {
    bool lp1Charging = data.lp1.charging;
    bool lp2Charging = data.lp2.charging;
    
    // If only one is charging, show that one
    if (lp1Charging && !lp2Charging) {
        return &data.lp1;
    }
    if (lp2Charging && !lp1Charging) {
        return &data.lp2;
    }
    
    // If both charging or neither charging, rotate every 10 seconds
    unsigned long now = millis();
    if (now - rotationState.lastRotation >= ROTATION_INTERVAL) {
        rotationState.currentLoadpoint = !rotationState.currentLoadpoint;
        rotationState.lastRotation = now;
        Serial.printf("Rotating to loadpoint %d\n", rotationState.currentLoadpoint ? 1 : 2);
    }
    
    return rotationState.currentLoadpoint ? &data.lp1 : &data.lp2;
}

// Utility functions
String formatPower(float watts) {
    if (abs(watts) < 1000) {
        return String((int)watts) + "W";
    } else if (abs(watts) < 10000) {
        return String(watts/1000.0, 1) + "kW";
    } else {
        return String(watts/1000.0, 0) + "kW";
    }
}

// Initialize stripe pattern style  
void initStripeStyle() {
    lv_style_init(&stripe_style);
    lv_style_set_bg_img_src(&stripe_style, &img_skew_strip);
    lv_style_set_bg_img_tiled(&stripe_style, true);
    lv_style_set_bg_img_opa(&stripe_style, LV_OPA_30);
    lv_style_set_bg_img_recolor_opa(&stripe_style, 0);
}

// Stripe pattern state tracking
bool stripe_applied = false;

// Apply or remove stripe pattern based on charging state
void applyStripePattern(lv_obj_t* segment, bool charging) {
    if (!segment) return;
    
    if (charging && !stripe_applied) {
        // Apply stripe pattern when charging
        lv_obj_add_style(segment, &stripe_style, LV_PART_INDICATOR);
        stripe_applied = true;
        Serial.println("Applied stripe pattern (charging)");
    } else if (!charging && stripe_applied) {
        // Remove stripe pattern when not charging
        lv_obj_remove_style(segment, &stripe_style, LV_PART_INDICATOR);
        stripe_applied = false;
        Serial.println("Removed stripe pattern (not charging)");
    }
}



// Create composite energy bar for IN/OUT sections
lv_obj_t* createCompositeBar(lv_obj_t* parent, int x, int y, int width, int height) {
    if (!parent) return nullptr;
    
    lv_obj_t* container = lv_obj_create(parent);
    if (!container) return nullptr;
    
    lv_obj_set_pos(container, x, y);
    lv_obj_set_size(container, width, height);
    lv_obj_set_style_bg_color(container, lv_color_hex(COLOR_BAR_BACKGROUND), 0); // Grey background
    lv_obj_set_style_bg_opa(container, LV_OPA_COVER, 0); // Opaque background
    lv_obj_set_style_border_width(container, 0, 0); // Remove border
    lv_obj_set_style_radius(container, 8, 0); // Rounded container
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_style_clip_corner(container, true, 0); // Clip children to rounded corners
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_OFF); // Disable scrollbars
    lv_obj_add_flag(container, LV_OBJ_FLAG_HIDDEN); // Start hidden until data available
    
    return container;
}

// Create colored segment within composite bar with label
lv_obj_t* createBarSegment(lv_obj_t* parent, lv_color_t color, lv_obj_t** label_out) {
    if (!parent) return nullptr;
    
    lv_obj_t* segment = lv_obj_create(parent);
    if (!segment) return nullptr;
    
    int parentHeight = lv_obj_get_height(parent);
    
    lv_obj_set_pos(segment, 0, 0);
    lv_obj_set_size(segment, 0, parentHeight); // Start with zero width, full height
    lv_obj_set_style_bg_color(segment, color, 0);
    lv_obj_set_style_bg_opa(segment, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(segment, 0, 0);
    lv_obj_set_style_radius(segment, 0, 0); // No radius - container handles rounding
    lv_obj_set_style_pad_all(segment, 0, 0);
    lv_obj_set_scrollbar_mode(segment, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(segment, LV_OBJ_FLAG_HIDDEN); // Start hidden
    
    // Create label on segment for displaying power value
    if (label_out) {
        *label_out = lv_label_create(segment);
        lv_obj_set_style_text_font(*label_out, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(*label_out, lv_color_hex(0xFFFFFF), 0); // White text
        lv_obj_set_style_text_align(*label_out, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(*label_out, "");
        lv_obj_center(*label_out);
        lv_obj_add_flag(*label_out, LV_OBJ_FLAG_HIDDEN); // Start hidden
    }
    
    return segment;
}

// Update composite bar with proportional segments
void updateCompositeBar(lv_obj_t* container, lv_obj_t** segments, lv_obj_t** labels, float* values, int segmentCount, int barWidth) {
    if (!container || !segments || !values) return;
    
    // Calculate total power for normalization
    float totalPower = 0;
    for (int i = 0; i < segmentCount; i++) {
        if (values[i] > 0) {
            totalPower += values[i];
        }
    }
    
    // Hide bar if no power
    if (totalPower < 1.0) {
        lv_obj_add_flag(container, LV_OBJ_FLAG_HIDDEN);
        // Hide all segments safely
        for (int i = 0; i < segmentCount; i++) {
            if (segments[i]) {
                lv_obj_add_flag(segments[i], LV_OBJ_FLAG_HIDDEN);
            }
            if (labels && labels[i]) {
                lv_obj_add_flag(labels[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        return;
    }
    
    lv_obj_clear_flag(container, LV_OBJ_FLAG_HIDDEN);
    
    // Get container height dynamically
    int containerHeight = lv_obj_get_height(container);
    
    // Minimum width to show text (approx 40px for "999W" with 12pt font)
    const int minWidthForText = 40;
    
    // Position segments proportionally
    int currentX = 0;
    for (int i = 0; i < segmentCount; i++) {
        if (!segments[i]) continue;
        
        if (values[i] > 0) {
            // Calculate segment width based on proportion
            int segmentWidth = (int)((values[i] / totalPower) * barWidth);
            if (segmentWidth < 1) segmentWidth = 1; // Minimum 1px for visibility
            
            // Position and size segment
            lv_obj_set_pos(segments[i], currentX, 0);
            lv_obj_set_width(segments[i], segmentWidth);
            lv_obj_set_height(segments[i], containerHeight); // Match container height dynamically
            lv_obj_clear_flag(segments[i], LV_OBJ_FLAG_HIDDEN);
            
            // Update label if provided and segment is wide enough
            if (labels && labels[i]) {
                if (segmentWidth >= minWidthForText) {
                    lv_label_set_text(labels[i], formatPower(values[i]).c_str());
                    lv_obj_center(labels[i]); // Re-center after text update
                    lv_obj_clear_flag(labels[i], LV_OBJ_FLAG_HIDDEN);
                } else {
                    lv_obj_add_flag(labels[i], LV_OBJ_FLAG_HIDDEN);
                }
            }
            
            currentX += segmentWidth;
        } else {
            // Hide segment if no power
            lv_obj_add_flag(segments[i], LV_OBJ_FLAG_HIDDEN);
            if (labels && labels[i]) {
                lv_obj_add_flag(labels[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

String formatEnergy(float wh) {
    if (abs(wh) < 1000) {
        return String((int)wh) + "Wh";
    } else if (abs(wh) < 10000) {
        return String(wh/1000.0, 1) + "kWh";
    } else {
        return String(wh/1000.0, 0) + "kWh";
    }
}

String formatPercentage(float value) {
    return value >= 0 ? String((int)value) + "%" : "---";
}

String formatDistance(float value) {
    return value >= 0 ? String((int)value) + "km" : "-- km";
}

String formatPlanTime(const String& isoTime) {
    if (isoTime.isEmpty() || isoTime.length() < 19) {
        return "keiner";
    }
    
    // Parse ISO 8601 format: "2025-10-12T05:00:00Z"
    int year = isoTime.substring(0, 4).toInt();
    int month = isoTime.substring(5, 7).toInt();
    int day = isoTime.substring(8, 10).toInt();
    int hour = isoTime.substring(11, 13).toInt();
    int minute = isoTime.substring(14, 16).toInt();
    
    // Get current local time to determine if we're in DST
    time_t now = time(nullptr);
    struct tm* currentLocal = localtime(&now);
    bool isDST = currentLocal->tm_isdst > 0;
    
    // Convert UTC to local time (Germany: CET=UTC+1, CEST=UTC+2)
    int localHour = hour + (isDST ? 2 : 1);
    int localDay = day;
    int localMonth = month;
    int localYear = year;
    
    // Handle day overflow
    if (localHour >= 24) {
        localHour -= 24;
        localDay++;
        
        // Simple day overflow handling (good enough for near-future dates)
        int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (localYear % 4 == 0 && (localYear % 100 != 0 || localYear % 400 == 0)) {
            daysInMonth[1] = 29; // Leap year
        }
        
        if (localDay > daysInMonth[localMonth - 1]) {
            localDay = 1;
            localMonth++;
            if (localMonth > 12) {
                localMonth = 1;
                localYear++;
            }
        }
    }
    
    // Calculate days difference from today
    int todayYear = currentLocal->tm_year + 1900;
    int todayMonth = currentLocal->tm_mon + 1;
    int todayDay = currentLocal->tm_mday;
    
    // Simple day difference calculation (good for dates within a week)
    int daysDiff = 0;
    if (localYear == todayYear && localMonth == todayMonth) {
        daysDiff = localDay - todayDay;
    } else if (localYear > todayYear || (localYear == todayYear && localMonth > todayMonth)) {
        daysDiff = 7; // Force to show date format for far future
    } else {
        daysDiff = -7; // Force to show date format for past
    }
    
    // German day names
    const char* germanDays[] = {"Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"};
    
    String dayString;
    if (daysDiff == 0) {
        dayString = "Heute";
    } else if (daysDiff == 1) {
        dayString = "Morgen";
    } else if (daysDiff >= 2 && daysDiff < 7) {
        // Calculate day of week for the target date
        struct tm targetDate = {0};
        targetDate.tm_year = localYear - 1900;
        targetDate.tm_mon = localMonth - 1;
        targetDate.tm_mday = localDay;
        mktime(&targetDate); // This normalizes and calculates tm_wday
        dayString = String(germanDays[targetDate.tm_wday]);
    } else {
        // Show date for dates outside the week
        dayString = String(localDay) + "." + String(localMonth) + ".";
    }
    
    // Format time as HH:MM
    String timeString = "";
    if (localHour < 10) timeString += "0";
    timeString += String(localHour) + ":";
    if (minute < 10) timeString += "0";
    timeString += String(minute);
    
    return dayString + " " + timeString;
}

// WiFi connection
bool connectWiFi() {
    Serial.println("Connecting to WiFi...");
    updateWiFiStatus("Connecting to WiFi...", ssid, "");
    
    WiFi.begin(ssid, password);
    
    int attempts = 0;
    int maxAttempts = 40; // 20 seconds max (40 * 500ms)
    while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
        delay(500);
        Serial.print(".");
        // Feed watchdog and keep LVGL/UI responsive while blocking here
        esp_task_wdt_reset();
        lv_task_handler();
        yield();

        // Update display with current attempt counter
        char progressText[40];
        snprintf(progressText, sizeof(progressText), "Connecting to WiFi, attempt %d/%d", attempts + 1, maxAttempts);
        updateWiFiStatus(progressText, ssid, "");
        
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println();
        Serial.print("Connected! IP: ");
        Serial.println(WiFi.localIP());
        
        // Show success status
        updateWiFiStatus("WiFi Connected!", ssid, WiFi.localIP().toString());
        delay(1000); // Show success message briefly
        
        return true;
    } else {
        Serial.println();
        Serial.println("Failed to connect to WiFi");
        
        // Show failure status
        updateWiFiStatus("WiFi Connection Failed", ssid, "Check credentials");
        delay(2000); // Show error message longer
        
        return false;
    }
}

// HTTP request function
bool httpGet(const char* path, String& response) {
    HTTPClient http;
    String url = "http://" + String(evcc_host) + ":" + String(evcc_port) + String(path);
    
    Serial.print("Requesting: ");
    Serial.println(url);
    
    http.begin(url);
    http.setTimeout(HTTP_TIMEOUT);
    
    int httpCode = http.GET();
    bool success = false;
    
    if (httpCode == HTTP_CODE_OK) {
        response = http.getString();
        Serial.printf("HTTP success: %d chars\n", response.length());
        Serial.println("=== HTTP Response ===");
        Serial.println(response);
        Serial.println("=== End Response ===");
        success = true;
    } else {
        Serial.printf("HTTP error: %d\n", httpCode);
    }
    
    http.end();
    return success;
}

// Parse combined data
bool parseCombinedData(const String& json) {
    DynamicJsonDocument doc(1536);
    DeserializationError error = deserializeJson(doc, json);
    
    if (error) {
        Serial.printf("Combined parse error: %s\n", error.c_str());
        return false;
    }
    
    // Parse energy data
    data.gridPower = doc["gridPower"] | 0.0;
    data.pvPower = doc["pvPower"] | 0.0;
    data.batterySoc = doc["batterySoc"] | -1.0;
    data.homePower = doc["homePower"] | 0.0;
    data.batteryPower = doc["batteryPower"] | 0.0;
    
    // Parse solar forecast data
    JsonObject solar = doc["solar"];
    if (!solar.isNull()) {
        data.solarForecastScale = solar["scale"] | 1.0;
        data.solarForecastTodayEnergy = solar["todayEnergy"] | 0.0;
    } else {
        data.solarForecastScale = 1.0;
        data.solarForecastTodayEnergy = 0.0;
    }
    
    // Reset loadpoint data
    data.lp1.soc = -1.0;
    data.lp1.chargePower = 0.0;
    data.lp1.vehicleRange = -1.0;
    data.lp1.effectivePlanTime = "";
    data.lp1.effectivePlanSoc = -1.0;
    data.lp1.effectiveLimitSoc = -1.0;
    data.lp1.planProjectedStart = "";
    data.lp2.soc = -1.0;
    data.lp2.chargePower = 0.0;
    data.lp2.vehicleRange = -1.0;
    data.lp2.effectivePlanTime = "";
    data.lp2.effectivePlanSoc = -1.0;
    data.lp2.effectiveLimitSoc = -1.0;
    data.lp2.planProjectedStart = "";
    
    // Parse loadpoint data
    JsonArray loadpoints = doc["loadpoints"];
    if (loadpoints.size() > 0 && !loadpoints[0].isNull()) {
        JsonObject lp1 = loadpoints[0];
        data.lp1.soc = lp1["soc"] | -1.0;
        data.lp1.chargePower = lp1["chargePower"] | 0.0;
        data.lp1.title = lp1["title"] | "LP1";
        data.lp1.vehicleTitle = lp1["vehicletitle"] | "";
        data.lp1.charging = lp1["charging"] | false;
        data.lp1.plugged = lp1["plugged"] | false;
        data.lp1.vehicleRange = lp1["vehicleRange"] | -1.0;
        data.lp1.effectivePlanTime = lp1["effectivePlanTime"] | "";
        data.lp1.effectivePlanSoc = lp1["effectivePlanSoc"] | -1.0;
        data.lp1.effectiveLimitSoc = lp1["effectiveLimitSoc"] | -1.0;
        data.lp1.planProjectedStart = lp1["planProjectedStart"] | "";
        data.lp1.maxCurrent = lp1["maxCurrent"] | 0.0;
        data.lp1.offeredCurrent = lp1["offeredCurrent"] | 0.0;
        data.lp1.phasesActive = lp1["phasesActive"] | 0;
        JsonArray currents = lp1["chargeCurrents"];
        for (int i = 0; i < 3 && i < currents.size(); i++) {
            data.lp1.chargeCurrents[i] = currents[i] | 0.0;
        }
    }
    
    if (loadpoints.size() > 1 && !loadpoints[1].isNull()) {
        JsonObject lp2 = loadpoints[1];
        data.lp2.soc = lp2["soc"] | -1.0;
        data.lp2.chargePower = lp2["chargePower"] | 0.0;
        data.lp2.title = lp2["title"] | "LP2";
        data.lp2.vehicleTitle = lp2["vehicletitle"] | "";
        data.lp2.charging = lp2["charging"] | false;
        data.lp2.plugged = lp2["plugged"] | false;
        data.lp2.vehicleRange = lp2["vehicleRange"] | -1.0;
        data.lp2.effectivePlanTime = lp2["effectivePlanTime"] | "";
        data.lp2.effectivePlanSoc = lp2["effectivePlanSoc"] | -1.0;
        data.lp2.effectiveLimitSoc = lp2["effectiveLimitSoc"] | -1.0;
        data.lp2.planProjectedStart = lp2["planProjectedStart"] | "";
        data.lp2.maxCurrent = lp2["maxCurrent"] | 0.0;
        data.lp2.offeredCurrent = lp2["offeredCurrent"] | 0.0;
        data.lp2.phasesActive = lp2["phasesActive"] | 0;
        JsonArray currents = lp2["chargeCurrents"];
        for (int i = 0; i < 3 && i < currents.size(); i++) {
            data.lp2.chargeCurrents[i] = currents[i] | 0.0;
        }
    }
    
    // Calculate derived values
    // float total_charge_power = data.lp1.chargePower + data.lp2.chargePower;
    
    
    return true;
}


// Create energy row
void createEnergyRow(lv_obj_t* parent, const char* description, const char* value1, const char* value2, 
                     int y_pos, lv_obj_t** desc_out, lv_obj_t** val1_out, lv_obj_t** val2_out) {
    // Description label
    *desc_out = lv_label_create(parent);
    lv_label_set_text(*desc_out, description);
    styleLabelPrimary(*desc_out);
    lv_obj_set_pos(*desc_out, 0, y_pos);
    
    // Value 1 (left value)
    *val1_out = lv_label_create(parent);
    lv_label_set_text(*val1_out, value1);
    styleLabelValue(*val1_out);
    positionAndAlign(*val1_out, 110, y_pos, 65, LV_TEXT_ALIGN_RIGHT);
    
    // Value 2 (right value)
    *val2_out = lv_label_create(parent);
    lv_label_set_text(*val2_out, value2);
    styleLabelValue(*val2_out);
    positionAndAlign(*val2_out, 161, y_pos, 60, LV_TEXT_ALIGN_RIGHT);
}

// Create column
lv_obj_t* createColumn(lv_obj_t* parent, const char* title, int x_pos, bool showHeader) {
    lv_obj_t* column = lv_obj_create(parent);
    lv_obj_set_pos(column, x_pos, 0);
    lv_obj_set_size(column, COLUMN_WIDTH, UPPER_SECTION_HEIGHT - (2 * PADDING));
    styleContainer(column);
    
    // Header (only if showHeader is true)
    if (showHeader) {
        lv_obj_t* header = lv_label_create(column);
        lv_label_set_text(header, title);
        styleLabelHeader(header);
        lv_obj_set_pos(header, 0, 0);
    }
    
    return column;
}

// Create car section
void createCarSection(lv_obj_t* parent, const char* title, const char* car_name) {
    lv_obj_t* container = lv_obj_create(parent);
    lv_obj_set_pos(container, PADDING, 0);
    lv_obj_set_size(container, SCREEN_WIDTH - (2 * PADDING), LOWER_SECTION_HEIGHT - 8);
    styleContainer(container);
    
    // Title (left) and Car name (right)
    ui.car.title_label = lv_label_create(container);
    lv_label_set_text(ui.car.title_label, title);
    styleLabelPrimary(ui.car.title_label);
    lv_obj_set_pos(ui.car.title_label, 0, 0);
    
    ui.car.car_label = lv_label_create(container);
    lv_label_set_text_fmt(ui.car.car_label, "%s", car_name);
    styleLabelSecondary(ui.car.car_label);
    positionAndAlign(ui.car.car_label, SCREEN_WIDTH-(4*PADDING)-16-120, 0, 120, LV_TEXT_ALIGN_RIGHT);
    
    // Power
    ui.car.power_label = lv_label_create(container);
    lv_label_set_text(ui.car.power_label, formatPower(0).c_str());
    styleLabelSecondary(ui.car.power_label);
    lv_obj_set_pos(ui.car.power_label, 0, 25);

    // Ladedauer
    ui.car.ladedauer_value = lv_label_create(container);
    lv_label_set_text(ui.car.ladedauer_value, "--:--");
    styleLabelSecondary(ui.car.ladedauer_value);
    positionAndAlign(ui.car.ladedauer_value, SCREEN_WIDTH-(4*PADDING)-16-120, 25, 120, LV_TEXT_ALIGN_RIGHT);

    // Phase current bars (above SoC bar) - 3 phases max, 30px wide each, 4px tall
    int phaseBarY = 50;
    int phaseBarWidth = 30;
    int phaseBarHeight = 4;
    int phaseBarSpacing = 2;
    
    // Create grey background bars first (bottom layer, always full width when visible)
    for (int i = 0; i < 3; i++) {
        int phaseBarX = i * (phaseBarWidth + phaseBarSpacing);
        
        ui.car.phase_bg_bars[i] = lv_obj_create(container);
        lv_obj_set_pos(ui.car.phase_bg_bars[i], phaseBarX, phaseBarY);
        lv_obj_set_size(ui.car.phase_bg_bars[i], phaseBarWidth, phaseBarHeight);
        lv_obj_set_style_bg_color(ui.car.phase_bg_bars[i], lv_color_hex(0xE0E0E0), 0); // Light grey
        lv_obj_set_style_bg_opa(ui.car.phase_bg_bars[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(ui.car.phase_bg_bars[i], 0, 0);
        lv_obj_set_style_radius(ui.car.phase_bg_bars[i], 2, 0);
        lv_obj_set_style_pad_all(ui.car.phase_bg_bars[i], 0, 0);
        lv_obj_add_flag(ui.car.phase_bg_bars[i], LV_OBJ_FLAG_HIDDEN); // Start hidden
    }
    
    // Create offered current bars (middle layer, dimmed green)
    for (int i = 0; i < 3; i++) {
        int phaseBarX = i * (phaseBarWidth + phaseBarSpacing);
        
        ui.car.phase_offered_bars[i] = lv_obj_create(container);
        lv_obj_set_pos(ui.car.phase_offered_bars[i], phaseBarX, phaseBarY);
        lv_obj_set_size(ui.car.phase_offered_bars[i], phaseBarWidth, phaseBarHeight);
        lv_obj_set_style_bg_color(ui.car.phase_offered_bars[i], lv_color_hex(0x8BC34A), 0); // Dimmed green
        lv_obj_set_style_bg_opa(ui.car.phase_offered_bars[i], LV_OPA_40, 0);
        lv_obj_set_style_border_width(ui.car.phase_offered_bars[i], 0, 0);
        lv_obj_set_style_radius(ui.car.phase_offered_bars[i], 2, 0);
        lv_obj_set_style_pad_all(ui.car.phase_offered_bars[i], 0, 0);
        lv_obj_add_flag(ui.car.phase_offered_bars[i], LV_OBJ_FLAG_HIDDEN); // Start hidden
    }
    
    // Create actual charging current bars (top layer, full green)
    for (int i = 0; i < 3; i++) {
        int phaseBarX = i * (phaseBarWidth + phaseBarSpacing);
        
        ui.car.phase_bars[i] = lv_obj_create(container);
        lv_obj_set_pos(ui.car.phase_bars[i], phaseBarX, phaseBarY);
        lv_obj_set_size(ui.car.phase_bars[i], 0, phaseBarHeight); // Start with 0 width
        lv_obj_set_style_bg_color(ui.car.phase_bars[i], lv_color_hex(COLOR_BAR_GENERATION), 0); // Full green
        lv_obj_set_style_bg_opa(ui.car.phase_bars[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(ui.car.phase_bars[i], 0, 0);
        lv_obj_set_style_radius(ui.car.phase_bars[i], 2, 0);
        lv_obj_set_style_pad_all(ui.car.phase_bars[i], 0, 0);
        lv_obj_add_flag(ui.car.phase_bars[i], LV_OBJ_FLAG_HIDDEN); // Start hidden
    }

    // SoC bar (moved down by 20px from 50 to 70, then up 5px to 65)
    ui.car.soc_bar = lv_bar_create(container);
    lv_obj_set_size(ui.car.soc_bar, SCREEN_WIDTH - (4 * PADDING) - 16, 20);
    lv_obj_set_pos(ui.car.soc_bar, 0, 65);
    lv_bar_set_value(ui.car.soc_bar, 35, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(ui.car.soc_bar, lv_color_hex(0xE0E0E0), LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui.car.soc_bar, lv_color_hex(0x4CAF50), LV_PART_INDICATOR);
    lv_obj_set_style_radius(ui.car.soc_bar, 8, LV_PART_MAIN);
    lv_obj_set_style_radius(ui.car.soc_bar, 8, LV_PART_INDICATOR);
    
    // Plan SoC marker (smaller, darker line) - moved down 20px then up 5px
    ui.car.plan_soc_marker = lv_obj_create(container);
    lv_obj_set_size(ui.car.plan_soc_marker, 2, 20);
    lv_obj_set_pos(ui.car.plan_soc_marker, 0, 65); // Aligned with bar
    lv_obj_set_style_bg_color(ui.car.plan_soc_marker, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_width(ui.car.plan_soc_marker, 0, 0);
    lv_obj_set_style_radius(ui.car.plan_soc_marker, 1, 0);
    lv_obj_add_flag(ui.car.plan_soc_marker, LV_OBJ_FLAG_HIDDEN); // Initially hidden
    
    // Limit SoC marker (larger, bar-colored with rounded edges) - moved down 20px then up 5px
    ui.car.limit_soc_marker = lv_obj_create(container);
    lv_obj_set_size(ui.car.limit_soc_marker, 6, 28);
    lv_obj_set_pos(ui.car.limit_soc_marker, 0, 61); // Slightly above bar
    lv_obj_set_style_bg_color(ui.car.limit_soc_marker, lv_color_hex(0x4CAF50), 0); // Same as bar color
    lv_obj_set_style_border_width(ui.car.limit_soc_marker, 0, 0);
    lv_obj_set_style_radius(ui.car.limit_soc_marker, 3, 0); // Rounded edges
    lv_obj_add_flag(ui.car.limit_soc_marker, LV_OBJ_FLAG_HIDDEN); // Initially hidden
    
    // Description labels
    ui.car.soc_desc = lv_label_create(container);
    lv_label_set_text(ui.car.soc_desc, "LADESTAND");
    styleLabelSecondary(ui.car.soc_desc);
    positionAndAlign(ui.car.soc_desc, 0, 90, 120, LV_TEXT_ALIGN_LEFT);
    
    ui.car.plan_desc = lv_label_create(container);
    lv_label_set_text(ui.car.plan_desc, "PLAN");
    styleLabelSecondary(ui.car.plan_desc);
    positionAndAlign(ui.car.plan_desc, 180, 90, 120, LV_TEXT_ALIGN_CENTER);
    
    ui.car.limit_desc = lv_label_create(container);
    lv_label_set_text(ui.car.limit_desc, "LADELIMIT");
    styleLabelSecondary(ui.car.limit_desc);
    positionAndAlign(ui.car.limit_desc, SCREEN_WIDTH-(4*PADDING)-16-120, 90, 120, LV_TEXT_ALIGN_RIGHT);
    
    // Status values
    ui.car.soc_value = lv_label_create(container);
    lv_label_set_text(ui.car.soc_value, "35%");
    styleLabelPrimary(ui.car.soc_value);
    positionAndAlign(ui.car.soc_value, 0, 110, 120, LV_TEXT_ALIGN_LEFT);
    
    ui.car.plan_value = lv_label_create(container);
    lv_label_set_text(ui.car.plan_value, "keiner");
    styleLabelPrimary(ui.car.plan_value);
    positionAndAlign(ui.car.plan_value, 180, 110, 120, LV_TEXT_ALIGN_CENTER);
    
    ui.car.plan_soc_value = lv_label_create(container);
    lv_label_set_text(ui.car.plan_soc_value, "");
    styleLabelSecondary(ui.car.plan_soc_value);
    positionAndAlign(ui.car.plan_soc_value, 180, 130, 120, LV_TEXT_ALIGN_CENTER);
    
    ui.car.range_value = lv_label_create(container);
    lv_label_set_text(ui.car.range_value, "140 km");
    styleLabelSecondary(ui.car.range_value);
    positionAndAlign(ui.car.range_value, 0, 130, 120, LV_TEXT_ALIGN_LEFT);
    
    ui.car.ladelimit_value = lv_label_create(container);
    lv_label_set_text(ui.car.ladelimit_value, "80%");
    styleLabelPrimary(ui.car.ladelimit_value);
    positionAndAlign(ui.car.ladelimit_value, SCREEN_WIDTH-(4*PADDING)-16-120, 110, 120, LV_TEXT_ALIGN_RIGHT);
}

// Create UI
void createUI() {
    // Main screen
    ui.screen = lv_obj_create(NULL);
    lv_obj_set_size(ui.screen, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(ui.screen, lv_color_hex(COLOR_GRID_BG), 0);
    lv_obj_set_style_pad_all(ui.screen, 0, 0);
    lv_obj_set_scrollbar_mode(ui.screen, LV_SCROLLBAR_MODE_OFF);
    
    // Upper container
    ui.upper_container = lv_obj_create(ui.screen);
    lv_obj_set_pos(ui.upper_container, 0, 0);
    lv_obj_set_size(ui.upper_container, SCREEN_WIDTH, UPPER_SECTION_HEIGHT);
    styleContainerWithBorder(ui.upper_container);
    lv_obj_set_style_pad_all(ui.upper_container, PADDING, 0);
    
    // Create columns (IN with header, OUT without - we'll add OUT label separately)
    lv_obj_t* in_column = createColumn(ui.upper_container, "In", PADDING, true);
    lv_obj_t* out_column = createColumn(ui.upper_container, "Out", COLUMN_WIDTH + (2 * PADDING), false);
    
    // Calculate bar dimensions - center bars horizontally
    // Total usable width minus padding on both sides
    int totalUsableWidth = SCREEN_WIDTH - (4 * PADDING);
    int barWidth = 360; // Fixed width for centered bars
    int barStartX = (SCREEN_WIDTH - barWidth) / 2; // Center horizontally
    
    // OUT label position (right side, aligned with value columns)
    int outLabelX = SCREEN_WIDTH - (3 * PADDING) - 60; // Right-aligned
    
    // Create IN bar centered at top
    ui.in_bar.container = createCompositeBar(ui.upper_container, barStartX, 2, barWidth, 20);
    ui.in_bar.generation_segment = createBarSegment(ui.in_bar.container, lv_color_hex(COLOR_BAR_GENERATION), &ui.in_bar.generation_label);
    ui.in_bar.battery_out_segment = createBarSegment(ui.in_bar.container, lv_color_hex(COLOR_BAR_BATTERY_OUT), &ui.in_bar.battery_out_label);
    ui.in_bar.grid_in_segment = createBarSegment(ui.in_bar.container, lv_color_hex(COLOR_BAR_GRID_IN), &ui.in_bar.grid_in_label);
    
    // Create OUT bar below IN bar (offset by bar height + gap)
    int outBarY = 2 + 20 + 4; // IN bar Y + height + gap
    ui.out_bar.container = createCompositeBar(ui.upper_container, barStartX, outBarY, barWidth, 20);
    ui.out_bar.consumption_segment = createBarSegment(ui.out_bar.container, lv_color_hex(COLOR_BAR_CONSUMPTION), &ui.out_bar.consumption_label);
    ui.out_bar.loadpoint_segment = createBarSegment(ui.out_bar.container, lv_color_hex(COLOR_BAR_LOADPOINT), &ui.out_bar.loadpoint_label);
    ui.out_bar.battery_in_segment = createBarSegment(ui.out_bar.container, lv_color_hex(COLOR_BAR_BATTERY_IN), &ui.out_bar.battery_in_label);
    ui.out_bar.grid_out_segment = createBarSegment(ui.out_bar.container, lv_color_hex(COLOR_BAR_GRID_OUT), &ui.out_bar.grid_out_label);
    
    // Create OUT label right-aligned, vertically aligned with OUT bar
    lv_obj_t* out_header = lv_label_create(ui.upper_container);
    lv_label_set_text(out_header, "Out");
    styleLabelHeader(out_header);
    positionAndAlign(out_header, outLabelX, outBarY, 60, LV_TEXT_ALIGN_RIGHT);
    
    // Create energy rows (moved down by 30px: from 30/52/74/96 to 60/82/104/126)
    createEnergyRow(in_column, "Erzeugung", "", "0W", 60, 
                    &ui.generation.desc, &ui.generation.value1, &ui.generation.value2);
    createEnergyRow(in_column, "Batterie entladen", "", "0W", 82, 
                    &ui.battery_discharge.desc, &ui.battery_discharge.value1, &ui.battery_discharge.value2);
    createEnergyRow(in_column, "Netzbezug", "", "0W", 104, 
                    &ui.grid_feed.desc, &ui.grid_feed.value1, &ui.grid_feed.value2);

    createEnergyRow(out_column, "Verbrauch", "", "0W", 60, 
                    &ui.consumption.desc, &ui.consumption.value1, &ui.consumption.value2);
    createEnergyRow(out_column, "Ladepunkt", "", "0W", 82, 
                    &ui.loadpoint.desc, &ui.loadpoint.value1, &ui.loadpoint.value2);
    createEnergyRow(out_column, "Batterie laden", "", "0W", 104, 
                    &ui.battery_charge.desc, &ui.battery_charge.value1, &ui.battery_charge.value2);
    createEnergyRow(out_column, "Einspeisung", "", "5W", 126, 
                    &ui.grid_feedin.desc, &ui.grid_feedin.value1, &ui.grid_feedin.value2);
    
    // Lower container
    ui.lower_container = lv_obj_create(ui.screen);
    lv_obj_set_pos(ui.lower_container, 0, UPPER_SECTION_HEIGHT);
    lv_obj_set_size(ui.lower_container, SCREEN_WIDTH, LOWER_SECTION_HEIGHT);
    styleContainerWithBorder(ui.lower_container);
    lv_obj_set_style_pad_all(ui.lower_container, PADDING, 0);
    
    // Create car section
    createCarSection(ui.lower_container, "Gartenhaus", "NotAModelY");
    
    // Load screen
    lv_scr_load(ui.screen);
    
    Serial.printf("UI created - Free heap: %d bytes\n", ESP.getFreeHeap());
}

// Update UI with current data
void updateUI() {
    // Read actual bar width dynamically from container
    int barMaxWidth = ui.in_bar.container ? lv_obj_get_width(ui.in_bar.container) : 360;
    
    // Update generation (PV)
    lv_label_set_text(ui.generation.value2, formatPower(data.pvPower).c_str());
    
    // Update solar forecast today (scaled) in value1 column
    float scaledSolarForecastEnergy = data.solarForecastTodayEnergy * data.solarForecastScale;
    // Note: todayEnergy from API is already in Wh, not kWh
    lv_label_set_text(ui.generation.value1, formatEnergy(scaledSolarForecastEnergy).c_str());
    
    // Apply conditional formatting for generation row
    lv_color_t genColor = (fabs(data.pvPower) < POWER_ACTIVE_THRESHOLD) ? lv_color_hex(COLOR_TEXT_SECONDARY) : lv_color_hex(COLOR_TEXT_VALUE);
    lv_obj_set_style_text_color(ui.generation.desc, genColor, 0);
    lv_obj_set_style_text_color(ui.generation.value1, genColor, 0);
    lv_obj_set_style_text_color(ui.generation.value2, genColor, 0);
    
    // Update consumption (house load)
    lv_label_set_text(ui.consumption.value2, formatPower(data.homePower).c_str());
    // Apply conditional formatting for consumption row
    lv_color_t consColor = (fabs(data.homePower) < POWER_ACTIVE_THRESHOLD) ? lv_color_hex(COLOR_TEXT_SECONDARY) : lv_color_hex(COLOR_TEXT_VALUE);
    lv_obj_set_style_text_color(ui.consumption.desc, consColor, 0);
    lv_obj_set_style_text_color(ui.consumption.value2, consColor, 0);
    
    // Update battery discharge/charge based on direction
    if (data.batteryPower > POWER_ACTIVE_THRESHOLD) { // Discharging (positive and above threshold)
        lv_label_set_text(ui.battery_discharge.value1, formatPercentage(data.batterySoc).c_str());
        lv_label_set_text(ui.battery_discharge.value2, formatPower(data.batteryPower).c_str());
        lv_label_set_text(ui.battery_charge.value1, formatPercentage(data.batterySoc).c_str());
        lv_label_set_text(ui.battery_charge.value2, formatPower(0).c_str());
        // Apply conditional formatting
        lv_obj_set_style_text_color(ui.battery_discharge.desc, lv_color_hex(COLOR_TEXT_VALUE), 0);
        lv_obj_set_style_text_color(ui.battery_discharge.value1, lv_color_hex(COLOR_TEXT_VALUE), 0);
        lv_obj_set_style_text_color(ui.battery_discharge.value2, lv_color_hex(COLOR_TEXT_VALUE), 0);
        lv_obj_set_style_text_color(ui.battery_charge.desc, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
        lv_obj_set_style_text_color(ui.battery_charge.value1, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
        lv_obj_set_style_text_color(ui.battery_charge.value2, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    } else if (data.batteryPower < -POWER_ACTIVE_THRESHOLD) { // Charging (negative and above threshold)
        float chargePower = -data.batteryPower;
        lv_label_set_text(ui.battery_charge.value1, formatPercentage(data.batterySoc).c_str());
        lv_label_set_text(ui.battery_charge.value2, formatPower(chargePower).c_str());
        lv_label_set_text(ui.battery_discharge.value1, formatPercentage(data.batterySoc).c_str());
        lv_label_set_text(ui.battery_discharge.value2, formatPower(0).c_str());
        // Apply conditional formatting
        lv_obj_set_style_text_color(ui.battery_charge.desc, lv_color_hex(COLOR_TEXT_VALUE), 0);
        lv_obj_set_style_text_color(ui.battery_charge.value1, lv_color_hex(COLOR_TEXT_VALUE), 0);
        lv_obj_set_style_text_color(ui.battery_charge.value2, lv_color_hex(COLOR_TEXT_VALUE), 0);
        lv_obj_set_style_text_color(ui.battery_discharge.desc, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
        lv_obj_set_style_text_color(ui.battery_discharge.value1, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
        lv_obj_set_style_text_color(ui.battery_discharge.value2, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    } else { // Below threshold -> dim both, show 0W movement
        lv_label_set_text(ui.battery_discharge.value1, formatPercentage(data.batterySoc).c_str());
        lv_label_set_text(ui.battery_discharge.value2, formatPower(0).c_str());
        lv_label_set_text(ui.battery_charge.value1, formatPercentage(data.batterySoc).c_str());
        lv_label_set_text(ui.battery_charge.value2, formatPower(0).c_str());
        lv_obj_set_style_text_color(ui.battery_discharge.desc, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
        lv_obj_set_style_text_color(ui.battery_discharge.value1, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
        lv_obj_set_style_text_color(ui.battery_discharge.value2, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
        lv_obj_set_style_text_color(ui.battery_charge.desc, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
        lv_obj_set_style_text_color(ui.battery_charge.value1, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
        lv_obj_set_style_text_color(ui.battery_charge.value2, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    }
    
    // Update grid feed/feed-in based on direction
    if (data.gridPower > POWER_ACTIVE_THRESHOLD) { // Consuming from grid (above threshold)
        lv_label_set_text(ui.grid_feed.value2, formatPower(data.gridPower).c_str());
        lv_label_set_text(ui.grid_feedin.value2, formatPower(0).c_str());
        lv_obj_set_style_text_color(ui.grid_feed.desc, lv_color_hex(COLOR_TEXT_VALUE), 0);
        lv_obj_set_style_text_color(ui.grid_feed.value2, lv_color_hex(COLOR_TEXT_VALUE), 0);
        lv_obj_set_style_text_color(ui.grid_feedin.desc, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
        lv_obj_set_style_text_color(ui.grid_feedin.value2, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    } else if (data.gridPower < -POWER_ACTIVE_THRESHOLD) { // Feeding into grid (above threshold)
        float feedinPower = -data.gridPower;
        lv_label_set_text(ui.grid_feedin.value2, formatPower(feedinPower).c_str());
        lv_label_set_text(ui.grid_feed.value2, formatPower(0).c_str());
        lv_obj_set_style_text_color(ui.grid_feedin.desc, lv_color_hex(COLOR_TEXT_VALUE), 0);
        lv_obj_set_style_text_color(ui.grid_feedin.value2, lv_color_hex(COLOR_TEXT_VALUE), 0);
        lv_obj_set_style_text_color(ui.grid_feed.desc, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
        lv_obj_set_style_text_color(ui.grid_feed.value2, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    } else { // Below threshold
        lv_label_set_text(ui.grid_feed.value2, formatPower(0).c_str());
        lv_label_set_text(ui.grid_feedin.value2, formatPower(0).c_str());
        lv_obj_set_style_text_color(ui.grid_feed.desc, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
        lv_obj_set_style_text_color(ui.grid_feed.value2, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
        lv_obj_set_style_text_color(ui.grid_feedin.desc, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
        lv_obj_set_style_text_color(ui.grid_feedin.value2, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    }
    
    // Update loadpoint
    float total_lp_power = data.lp1.chargePower + data.lp2.chargePower;
    lv_label_set_text(ui.loadpoint.value2, formatPower(total_lp_power).c_str());
    // Apply conditional formatting for loadpoint row
    lv_color_t lpColor = (fabs(total_lp_power) < POWER_ACTIVE_THRESHOLD) ? lv_color_hex(COLOR_TEXT_SECONDARY) : lv_color_hex(COLOR_TEXT_VALUE);
    lv_obj_set_style_text_color(ui.loadpoint.desc, lpColor, 0);
    lv_obj_set_style_text_color(ui.loadpoint.value2, lpColor, 0);
    
    // Update composite bars
    // IN bar: PV generation, battery discharge, grid consumption
    float inValues[3] = {
        data.pvPower > 0 ? data.pvPower : 0,
        data.batteryPower > 0 ? data.batteryPower : 0,
        data.gridPower > 0 ? data.gridPower : 0
    };
    lv_obj_t* inSegments[3] = {ui.in_bar.generation_segment, ui.in_bar.battery_out_segment, ui.in_bar.grid_in_segment};
    lv_obj_t* inLabels[3] = {ui.in_bar.generation_label, ui.in_bar.battery_out_label, ui.in_bar.grid_in_label};
    updateCompositeBar(ui.in_bar.container, inSegments, inLabels, inValues, 3, barMaxWidth);
    
    // OUT bar: house consumption, car charging, battery charging, grid feed-in
    float outValues[4] = {
        data.homePower > 0 ? data.homePower : 0,
        total_lp_power > 0 ? total_lp_power : 0,
        data.batteryPower < 0 ? -data.batteryPower : 0,
        data.gridPower < 0 ? -data.gridPower : 0
    };
    lv_obj_t* outSegments[4] = {ui.out_bar.consumption_segment, ui.out_bar.loadpoint_segment, ui.out_bar.battery_in_segment, ui.out_bar.grid_out_segment};
    lv_obj_t* outLabels[4] = {ui.out_bar.consumption_label, ui.out_bar.loadpoint_label, ui.out_bar.battery_in_label, ui.out_bar.grid_out_label};
    updateCompositeBar(ui.out_bar.container, outSegments, outLabels, outValues, 4, barMaxWidth);
    
    // Update car section (using active loadpoint data)
    auto* activeLP = getActiveLoadpoint();
    
    // Update power label based on charging and connection status
    if (activeLP->charging) {
        // Show power value when actively charging
        lv_label_set_text(ui.car.power_label, formatPower(activeLP->chargePower).c_str());
    } else if (activeLP->plugged) {
        // Show "Connected" when plugged but not charging
        lv_label_set_text(ui.car.power_label, "Verbunden");
    } else {
        // Show "Not connected" when not plugged in
        lv_label_set_text(ui.car.power_label, "Nicht verbunden");
    }
    
    // Update SoC and bar
    if (activeLP->soc >= 0) {
        lv_bar_set_value(ui.car.soc_bar, (int)activeLP->soc, LV_ANIM_OFF);
        lv_label_set_text(ui.car.soc_value, formatPercentage(activeLP->soc).c_str());
        
        // Apply or remove stripe pattern based on charging state
        applyStripePattern(ui.car.soc_bar, activeLP->charging);
    } else {
        lv_label_set_text(ui.car.soc_value, "---");
    }
    
    // Update phase current bars
    int phaseBarWidth = 30;
    for (int i = 0; i < 3; i++) {
        if (i < activeLP->phasesActive && activeLP->maxCurrent > 0) {
            // Show grey background bar (full width)
            lv_obj_clear_flag(ui.car.phase_bg_bars[i], LV_OBJ_FLAG_HIDDEN);
            
            // Calculate offered current bar width (middle layer, dimmed green)
            float offeredRatio = activeLP->offeredCurrent / activeLP->maxCurrent;
            if (offeredRatio > 1.0) offeredRatio = 1.0;
            int offeredWidth = (int)(offeredRatio * phaseBarWidth);
            if (offeredWidth < 1 && activeLP->offeredCurrent > 0.1) offeredWidth = 1;
            lv_obj_set_width(ui.car.phase_offered_bars[i], offeredWidth);
            lv_obj_clear_flag(ui.car.phase_offered_bars[i], LV_OBJ_FLAG_HIDDEN);
            
            // Calculate and show actual charging current bar (top layer, full green)
            if (activeLP->chargeCurrents[i] > 0) {
                float currentRatio = activeLP->chargeCurrents[i] / activeLP->maxCurrent;
                if (currentRatio > 1.0) currentRatio = 1.0;
                int actualWidth = (int)(currentRatio * phaseBarWidth);
                if (actualWidth < 1 && activeLP->chargeCurrents[i] > 0.1) actualWidth = 1; // Minimum visibility
                lv_obj_set_width(ui.car.phase_bars[i], actualWidth);
                lv_obj_clear_flag(ui.car.phase_bars[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(ui.car.phase_bars[i], LV_OBJ_FLAG_HIDDEN);
            }
        } else {
            // Hide all bars for inactive phases or when maxCurrent not available
            lv_obj_add_flag(ui.car.phase_bg_bars[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui.car.phase_offered_bars[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui.car.phase_bars[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    
    // Update plan SoC marker (Y position updated to 65)
    if (activeLP->effectivePlanSoc > 0) { // Only show if > 0, not just >= 0
        int barWidth = SCREEN_WIDTH - (4 * PADDING) - 16;
        int markerX = (activeLP->effectivePlanSoc / 100.0) * barWidth - 1; // Center the 2px marker
        lv_obj_set_pos(ui.car.plan_soc_marker, markerX, 65);
        lv_obj_clear_flag(ui.car.plan_soc_marker, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ui.car.plan_soc_marker, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Update limit SoC marker (Y position updated to 61)
    if (activeLP->effectiveLimitSoc >= 0) {
        int barWidth = SCREEN_WIDTH - (4 * PADDING) - 16;
        int markerX = (activeLP->effectiveLimitSoc / 100.0) * barWidth - 3; // Center the 6px marker (offset by half width)
        
        // Ensure marker doesn't go outside bar bounds
        if (markerX < 0) markerX = 0;
        if (markerX > barWidth - 6) markerX = barWidth - 6;
        
        lv_obj_set_pos(ui.car.limit_soc_marker, markerX, 61);
        lv_obj_clear_flag(ui.car.limit_soc_marker, LV_OBJ_FLAG_HIDDEN);
        
    } else {
        lv_obj_add_flag(ui.car.limit_soc_marker, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Update range - only show if vehicleRange is available from EVCC
    if (activeLP->vehicleRange >= 0) {
        lv_label_set_text(ui.car.range_value, formatDistance(activeLP->vehicleRange).c_str());
    } else {
        lv_label_set_text(ui.car.range_value, formatDistance(-1).c_str());
    }
    
    // Update titles
    if (!activeLP->vehicleTitle.isEmpty()) {
        lv_label_set_text(ui.car.car_label, activeLP->vehicleTitle.c_str());
    }
    if (!activeLP->title.isEmpty()) {
        lv_label_set_text(ui.car.title_label, activeLP->title.c_str());
    }
    
    // Update plan time and plan SoC
    if (!activeLP->effectivePlanTime.isEmpty()) {
        lv_label_set_text(ui.car.plan_value, formatPlanTime(activeLP->effectivePlanTime).c_str());
        
        // Show plan SoC if plan time exists
        if (activeLP->effectivePlanSoc >= 0) {
            lv_label_set_text(ui.car.plan_soc_value, formatPercentage(activeLP->effectivePlanSoc).c_str());
        } else {
            lv_label_set_text(ui.car.plan_soc_value, "");
        }
    } else {
        lv_label_set_text(ui.car.plan_value, "keiner");
        lv_label_set_text(ui.car.plan_soc_value, "");
    }
    
    // Update charge limit
    if (activeLP->effectiveLimitSoc >= 0) {
        lv_label_set_text(ui.car.ladelimit_value, formatPercentage(activeLP->effectiveLimitSoc).c_str());
    } else {
        lv_label_set_text(ui.car.ladelimit_value, "---");
    }
    
    // Update Ladedauer with planProjectedStart if available
    if (!activeLP->planProjectedStart.isEmpty()) {
        String formattedTime = formatPlanTime(activeLP->planProjectedStart);
        char projectedDisplay[64];
        snprintf(projectedDisplay, sizeof(projectedDisplay), "|--> %s", formattedTime.c_str());
        lv_label_set_text(ui.car.ladedauer_value, projectedDisplay);
    } else {
        // Clear the planned start display when no plan exists
        lv_label_set_text(ui.car.ladedauer_value, "--:--");
    }
}

// Poll EVCC data
bool pollEVCCData() {
    Serial.printf("Starting poll - Free heap: %d bytes\n", ESP.getFreeHeap());
    
    // Check memory before HTTP request
    if (ESP.getFreeHeap() < 16000) {
        Serial.println("Insufficient memory for HTTP request");
        return false;
    }
    
    String response;
    response.reserve(2048); // Pre-allocate to avoid fragmentation
    
    // Get combined data in single request
    if (httpGet(combined_path, response)) {
        if (parseCombinedData(response)) {
            data.lastUpdate = millis();
            data.consecutiveFailures = 0;
            updateUI();
            
            // Force string cleanup
            response = String();
            
            // Memory health check
            if (ESP.getFreeHeap() < 12000) {
                Serial.printf("WARNING: Low memory after poll: %d bytes\n", ESP.getFreeHeap());
            }
            
            return true;
        }
    } else {
        Serial.println("HTTP request failed");
    }
    
    return false;
}

// WiFi status display variables
lv_obj_t* wifi_screen = nullptr;
lv_obj_t* wifi_status_label = nullptr;
lv_obj_t* wifi_ssid_label = nullptr;
lv_obj_t* wifi_ip_label = nullptr;

// Show initial WiFi connecting status screen
void showWiFiConnectingStatus() {
    // Create simple status screen
    wifi_screen = lv_obj_create(NULL);
    lv_obj_set_size(wifi_screen, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(wifi_screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_pad_all(wifi_screen, 20, 0);
    lv_obj_set_scrollbar_mode(wifi_screen, LV_SCROLLBAR_MODE_OFF);
    
    // Main status label
    wifi_status_label = lv_label_create(wifi_screen);
    lv_label_set_text(wifi_status_label, "Initializing...");
    lv_obj_set_style_text_font(wifi_status_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(wifi_status_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_pos(wifi_status_label, 0, 100);
    lv_obj_set_width(wifi_status_label, SCREEN_WIDTH - 40);
    lv_obj_set_style_text_align(wifi_status_label, LV_TEXT_ALIGN_CENTER, 0);
    
    // SSID label
    wifi_ssid_label = lv_label_create(wifi_screen);
    lv_label_set_text(wifi_ssid_label, "");
    lv_obj_set_style_text_font(wifi_ssid_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(wifi_ssid_label, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_pos(wifi_ssid_label, 0, 140);
    lv_obj_set_width(wifi_ssid_label, SCREEN_WIDTH - 40);
    lv_obj_set_style_text_align(wifi_ssid_label, LV_TEXT_ALIGN_CENTER, 0);
    
    // IP/Status label
    wifi_ip_label = lv_label_create(wifi_screen);
    lv_label_set_text(wifi_ip_label, "");
    lv_obj_set_style_text_font(wifi_ip_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(wifi_ip_label, lv_color_hex(0x999999), 0);
    lv_obj_set_pos(wifi_ip_label, 0, 180);
    lv_obj_set_width(wifi_ip_label, SCREEN_WIDTH - 40);
    lv_obj_set_style_text_align(wifi_ip_label, LV_TEXT_ALIGN_CENTER, 0);
    
    // Load the screen
    lv_scr_load(wifi_screen);
    lv_task_handler(); // Process immediately
}

// Update WiFi status on screen
void updateWiFiStatus(const String& status, const String& ssid_name, const String& ip_info) {
    if (wifi_status_label) {
        lv_label_set_text(wifi_status_label, status.c_str());
    }
    if (wifi_ssid_label && !ssid_name.isEmpty()) {
        String ssid_text = "Network: " + ssid_name;
        lv_label_set_text(wifi_ssid_label, ssid_text.c_str());
    }
    if (wifi_ip_label && !ip_info.isEmpty()) {
        lv_label_set_text(wifi_ip_label, ip_info.c_str());
    }
    lv_task_handler(); // Process immediately
}

// Clean up WiFi status screen and reset pointers
void cleanupWiFiStatusScreen() {
    if (wifi_screen) {
        lv_obj_del(wifi_screen);
        wifi_screen = nullptr;
        wifi_status_label = nullptr;
        wifi_ssid_label = nullptr;
        wifi_ip_label = nullptr;
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("EVCC Display ESP32 - Starting...");
    
    // Initialize watchdog timer (8 seconds)
    esp_task_wdt_deinit(); // Clear any existing watchdog
    esp_task_wdt_init(8, true);
    esp_task_wdt_add(NULL);
    
    // Initialize display FIRST for immediate feedback
    tft.init();
    tft.setRotation(1); // Landscape
    tft.fillScreen(TFT_BLACK);
    
    // Configure backlight
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    
    // Initialize LVGL
    lv_init();
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, SCREEN_WIDTH * 10);
    
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = SCREEN_WIDTH;
    disp_drv.ver_res = SCREEN_HEIGHT;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);
    
    // Initialize stripe pattern style
    initStripeStyle();
    
    Serial.printf("Display initialized - Free heap: %d bytes\n", ESP.getFreeHeap());
    
    // Show WiFi connecting status immediately
    showWiFiConnectingStatus();
    
    // Connect to WiFi
    if (connectWiFi()) {
        Serial.printf("After WiFi - Free heap: %d bytes\n", ESP.getFreeHeap());
        
        // Update status for time sync
        updateWiFiStatus("Synchronizing time...", ssid, WiFi.localIP().toString());
        
        // Initialize time with proper timezone (CET/CEST for Germany)
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");
        setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1); // Europe/Berlin timezone
        tzset();
        Serial.println("Waiting for time synchronization...");
        time_t now = time(nullptr);
        int attempts = 0;
        while (now < 8 * 3600 * 2 && attempts < 20) {
            delay(500);
            now = time(nullptr);
            attempts++;
            // Feed watchdog and keep UI alive while waiting for time
            esp_task_wdt_reset();
            lv_task_handler();
            yield();
            // Provide a progress update every second attempt (~1s)
            if (attempts % 2 == 0) {
                char syncMsg[48];
                snprintf(syncMsg, sizeof(syncMsg), "Synchronizing time (%d/20)", attempts);
                updateWiFiStatus(syncMsg, ssid, WiFi.localIP().toString());
            }
        }
        Serial.printf("Time synchronized: %s", ctime(&now));
        
        // Update status for HTTP test
        updateWiFiStatus("Testing EVCC connection...", ssid, WiFi.localIP().toString());
        
        // Test HTTP before UI creation
        Serial.println("Testing HTTP before UI creation...");
        String response;
        if (httpGet(combined_path, response)) {
            Serial.println("✅ HTTP test successful before UI!");
            parseCombinedData(response);
        }
        
        Serial.printf("After HTTP test - Free heap: %d bytes\n", ESP.getFreeHeap());
        
        // Update status for UI creation
        updateWiFiStatus("Creating interface...", ssid, WiFi.localIP().toString());
        
        // Create UI
        createUI();
        
        // Clean up WiFi status screen
        cleanupWiFiStatusScreen();
        
        Serial.printf("After UI creation - Free heap: %d bytes\n", ESP.getFreeHeap());
        
        // Update UI with initial data
        updateUI();
        Serial.println("📊 UI updated with initial data");
        
        Serial.printf("Setup complete - Free heap: %d bytes\n", ESP.getFreeHeap());
    } else {
        Serial.println("WiFi failed - running in demo mode");
        
        // Keep error message visible for a moment, then switch to demo UI
        delay(3000);
        
        // Create demo UI
        createUI();
        
        // Clean up WiFi status screen
        cleanupWiFiStatusScreen();
    }
}

void loop() {
    static unsigned long lastPoll = 0;
    static unsigned long lastLVGL = 0;
    
    unsigned long now = millis();
    
    // Handle millis() overflow (every ~49 days)
    if (now < lastPoll) {
        lastPoll = 0;
    }
    if (now < lastLVGL) {
        lastLVGL = 0;
    }
    
    // Handle LVGL tasks (every 5ms)
    if (now - lastLVGL >= 5) {
        lv_task_handler();
        lastLVGL = now;
        esp_task_wdt_reset(); // Feed watchdog
    }
    
    // Poll EVCC data
    if (WiFi.status() == WL_CONNECTED && now - lastPoll >= POLL_INTERVAL) {
        lastPoll = now;
        if (!pollEVCCData()) {
            data.consecutiveFailures++;
            Serial.printf("Poll failed (#%d)\n", data.consecutiveFailures);
            
            // Emergency restart if too many failures
            if (data.consecutiveFailures > 50) {
                Serial.println("Too many failures, restarting...");
                delay(1000);
                ESP.restart();
            }
        }
    }
    
    // WiFi reconnection logic
    if (WiFi.status() != WL_CONNECTED) {
        static unsigned long lastReconnectAttempt = 0;
        if (now - lastReconnectAttempt > 30000) { // Try every 30 seconds
            Serial.println("WiFi disconnected, attempting reconnect...");
            WiFi.begin(ssid, password);
            lastReconnectAttempt = now;
        }
    }
    
    delay(1);
}