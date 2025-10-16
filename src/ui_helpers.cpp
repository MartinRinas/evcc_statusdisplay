// ui_helpers.cpp - Implementation of UI helper functions
#include "ui_helpers.h"

// Global UI instance defined elsewhere
extern UIElements ui;

// Helper: Convert RGB565 to RGB888
static void rgb565_to_rgb888(uint16_t rgb565, uint8_t& r, uint8_t& g, uint8_t& b) {
    r = ((rgb565 >> 11) & 0x1F) * 255 / 31;  // 5 bits red
    g = ((rgb565 >> 5) & 0x3F) * 255 / 63;   // 6 bits green
    b = (rgb565 & 0x1F) * 255 / 31;          // 5 bits blue
}

// Helper: Calculate relative luminance from lv_color_t
static float getLuminanceFromLvColor(lv_color_t color) {
    uint8_t r, g, b;
    // LVGL stores colors in RGB565 format in the 'full' field
    rgb565_to_rgb888(color.full, r, g, b);
    // Simplified luminance formula (ITU BT.709)
    return (0.2126f * r + 0.7152f * g + 0.0722f * b) / 255.0f;
}

// Helper: Choose text color (BS_GRAY_DARK or white) based on background luminance
static uint32_t getContrastTextColor(lv_color_t bgColor) {
    uint8_t r, g, b;
    rgb565_to_rgb888(bgColor.full, r, g, b);
    float lum = getLuminanceFromLvColor(bgColor);
    uint32_t textColor = (lum > 0.5f) ? BS_GRAY_DARK : 0xFFFFFF;
    
    return textColor;
}

void styleLabel(lv_obj_t* label, const lv_font_t* font, lv_color_t color) {
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
}

void styleLabelPrimary(lv_obj_t* label) { styleLabel(label, FONT_SECONDARY, lv_color_hex(COLOR_TEXT_PRIMARY)); }
void styleLabelSecondary(lv_obj_t* label) { styleLabel(label, FONT_SMALL, lv_color_hex(COLOR_TEXT_SECONDARY)); }
void styleLabelValue(lv_obj_t* label) { styleLabel(label, FONT_SECONDARY, lv_color_hex(COLOR_TEXT_VALUE)); }
void styleLabelHeader(lv_obj_t* label) { styleLabel(label, FONT_PRIMARY, lv_color_hex(COLOR_TEXT_PRIMARY)); }

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

void stylePhaseBar(lv_obj_t* bar, uint32_t color, lv_opa_t opacity) {
    lv_obj_set_style_bg_color(bar, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(bar, opacity, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 2, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
}

lv_obj_t* createPhaseBar(lv_obj_t* parent, int x, int y, int width, int height, uint32_t color, lv_opa_t opacity, bool startHidden) {
    lv_obj_t* bar = lv_obj_create(parent);
    lv_obj_set_pos(bar, x, y);
    lv_obj_set_size(bar, width, height);
    stylePhaseBar(bar, color, opacity);
    if (startHidden) lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
    return bar;
}

lv_obj_t* createCompositeBar(lv_obj_t* parent, int x, int y, int width, int height) {
    if (!parent) return nullptr;
    lv_obj_t* container = lv_obj_create(parent);
    if (!container) return nullptr;
    lv_obj_set_pos(container, x, y);
    lv_obj_set_size(container, width, height);
    lv_obj_set_style_bg_color(container, lv_color_hex(COLOR_BAR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_radius(container, 8, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_style_clip_corner(container, false, 0);  // Allow labels to overflow border
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(container, LV_OBJ_FLAG_HIDDEN);
    return container;
}

lv_obj_t* createBarSegment(lv_obj_t* parent, lv_color_t color, lv_obj_t** label_out) {
    if (!parent) return nullptr;
    lv_obj_t* segment = lv_obj_create(parent);
    if (!segment) return nullptr;
    int parentHeight = lv_obj_get_height(parent);
    lv_obj_set_pos(segment, 0, 0);
    lv_obj_set_size(segment, 0, parentHeight);
    lv_obj_set_style_bg_color(segment, color, 0);
    lv_obj_set_style_bg_opa(segment, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(segment, 0, 0);
    lv_obj_set_style_radius(segment, 0, 0);
    lv_obj_set_style_pad_all(segment, 0, 0);
    lv_obj_set_scrollbar_mode(segment, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(segment, LV_OBJ_FLAG_HIDDEN);
    if (label_out) {
        *label_out = lv_label_create(segment);
        lv_obj_set_style_text_font(*label_out, &lv_font_montserrat_12, 0);
        // Dynamic contrast: choose text color based on segment background
        uint32_t textColor = getContrastTextColor(color);
        lv_obj_set_style_text_color(*label_out, lv_color_hex(textColor), 0);
        lv_obj_set_style_text_align(*label_out, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(*label_out, "");
        lv_obj_center(*label_out);
        lv_obj_add_flag(*label_out, LV_OBJ_FLAG_HIDDEN);
    }
    return segment;
}

void updateCompositeBar(lv_obj_t* container, lv_obj_t** segments, lv_obj_t** labels, float* values, int segmentCount, int barWidth) {
    if (!container || !segments || !values) return;
    float totalPower = 0;
    for (int i = 0; i < segmentCount; i++) if (values[i] > 0) totalPower += values[i];
    if (totalPower < 1.0f) {
        lv_obj_add_flag(container, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < segmentCount; i++) {
            if (segments[i]) lv_obj_add_flag(segments[i], LV_OBJ_FLAG_HIDDEN);
            if (labels && labels[i]) lv_obj_add_flag(labels[i], LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    lv_obj_clear_flag(container, LV_OBJ_FLAG_HIDDEN);
    int containerHeight = lv_obj_get_height(container);
    // Determine if we are using abbreviation mode (IN/OUT bars) vs value mode (overlay)
    extern UIElements ui; // global
    bool abbrevMode = (container == ui.in_bar.container) || (container == ui.out_bar.container);
    // For numeric (overlay) segments we require a reasonable width; for abbreviation mode we dynamically
    // calculate the pixel width required for the short label and only show it if it fits fully.
    const int minWidthForValueText = 40; // overlay numeric labels
    const char* inNames[4] = {"pv","bat","grid",""};
    const char* outNames[4] = {"home","chg","bat","grid"};
    int currentX = 0;
    for (int i = 0; i < segmentCount; i++) {
        if (!segments[i]) continue;
        if (values[i] > 0) {
            int segmentWidth = (int)((values[i] / totalPower) * barWidth);
            if (segmentWidth < 1) segmentWidth = 1;
            lv_obj_set_pos(segments[i], currentX, 0);
            lv_obj_set_size(segments[i], segmentWidth, containerHeight);
            lv_obj_clear_flag(segments[i], LV_OBJ_FLAG_HIDDEN);
            if (labels && labels[i]) {
                if (abbrevMode) {
                    // Determine the abbreviation text for this segment.
                    const char* txt = (container == ui.in_bar.container) ? inNames[i] : outNames[i];
                    if (txt == nullptr) txt = "";
                    // Measure required pixel width for the text using the label's current font.
                    const lv_font_t* font = lv_obj_get_style_text_font(labels[i], LV_PART_MAIN);
                    lv_point_t sizeReq; 
                    lv_txt_get_size(&sizeReq, txt, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
                    int requiredWidth = sizeReq.x + 4; // small padding so we don't clip
                    if (segmentWidth >= requiredWidth && txt[0] != '\0') {
                        lv_label_set_text(labels[i], txt);
                        lv_obj_set_style_text_color(labels[i], lv_color_hex(BS_GRAY_DARK), 0);
                        if (container == ui.in_bar.container) {
                            lv_obj_align_to(labels[i], segments[i], LV_ALIGN_TOP_MID, 0, 0);
                        } else {
                            lv_obj_align_to(labels[i], segments[i], LV_ALIGN_BOTTOM_MID, 0, 0);
                        }
                        lv_obj_clear_flag(labels[i], LV_OBJ_FLAG_HIDDEN);
                    } else {
                        lv_obj_add_flag(labels[i], LV_OBJ_FLAG_HIDDEN); // hide if it doesn't fully fit
                    }
                } else {
                    if (segmentWidth >= minWidthForValueText) {
                        lv_label_set_text(labels[i], formatPower(values[i]).c_str());
                        lv_color_t segmentColor = lv_obj_get_style_bg_color(segments[i], 0);
                        uint32_t textColor = getContrastTextColor(segmentColor);
                        lv_obj_set_style_text_color(labels[i], lv_color_hex(textColor), 0);
                        lv_obj_center(labels[i]);
                        lv_obj_clear_flag(labels[i], LV_OBJ_FLAG_HIDDEN);
                    } else {
                        lv_obj_add_flag(labels[i], LV_OBJ_FLAG_HIDDEN);
                    }
                }
            }
            currentX += segmentWidth;
        } else {
            lv_obj_add_flag(segments[i], LV_OBJ_FLAG_HIDDEN);
            if (labels && labels[i]) lv_obj_add_flag(labels[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void createEnergyRow(lv_obj_t* parent, const char* description, const char* value1, const char* value2,
                     int y_pos, lv_obj_t** desc_out, lv_obj_t** val1_out, lv_obj_t** val2_out) {
    *desc_out = lv_label_create(parent);
    lv_label_set_text(*desc_out, description);
    styleLabelPrimary(*desc_out);
    lv_obj_set_pos(*desc_out, 0, y_pos);
    *val1_out = lv_label_create(parent);
    lv_label_set_text(*val1_out, value1);
    styleLabelValue(*val1_out);
    positionAndAlign(*val1_out, 110, y_pos, 65, LV_TEXT_ALIGN_RIGHT);
    *val2_out = lv_label_create(parent);
    lv_label_set_text(*val2_out, value2);
    styleLabelValue(*val2_out);
    positionAndAlign(*val2_out, 161, y_pos, 60, LV_TEXT_ALIGN_RIGHT);
}

lv_obj_t* createColumn(lv_obj_t* parent, const char* title, int x_pos, bool showHeader) {
    lv_obj_t* column = lv_obj_create(parent);
    lv_obj_set_pos(column, x_pos, 0);
    lv_obj_set_size(column, COLUMN_WIDTH, UPPER_SECTION_HEIGHT - (2 * PADDING));
    styleContainer(column);
    if (showHeader) {
        lv_obj_t* header = lv_label_create(column);
        lv_label_set_text(header, title);
        styleLabelHeader(header);
        lv_obj_set_pos(header, 0, 0);
    }
    return column;
}

void createCarSection(lv_obj_t* parent, const char* title, const char* car_name) {
    lv_obj_t* container = lv_obj_create(parent);
    lv_obj_set_pos(container, PADDING, 0);
    lv_obj_set_size(container, SCREEN_WIDTH - (2 * PADDING), LOWER_SECTION_HEIGHT - 8);
    styleContainer(container);
    ui.car.title_label = lv_label_create(container);
    lv_label_set_text(ui.car.title_label, title);
    styleLabelPrimary(ui.car.title_label);
    lv_obj_set_pos(ui.car.title_label, 0, 0);
    ui.car.car_label = lv_label_create(container);
    lv_label_set_text_fmt(ui.car.car_label, "%s", car_name);
    styleLabelSecondary(ui.car.car_label);
    positionAndAlign(ui.car.car_label, SCREEN_WIDTH-(4*PADDING)-16-120, 0, 120, LV_TEXT_ALIGN_RIGHT);
    ui.car.power_label = lv_label_create(container);
    lv_label_set_text(ui.car.power_label, "0W");
    styleLabelSecondary(ui.car.power_label);
    lv_obj_set_pos(ui.car.power_label, 0, 25);
    ui.car.ladedauer_value = lv_label_create(container);
    lv_label_set_text(ui.car.ladedauer_value, "--:--");
    styleLabelSecondary(ui.car.ladedauer_value);
    positionAndAlign(ui.car.ladedauer_value, SCREEN_WIDTH-(4*PADDING)-16-120, 25, 120, LV_TEXT_ALIGN_RIGHT);
    int phaseBarY = 50; int phaseBarWidth = 30; int phaseBarHeight = 4; int phaseBarSpacing = 2;
    for (int i = 0; i < 3; i++) {
        int phaseBarX = i * (phaseBarWidth + phaseBarSpacing);
        ui.car.phase_bg_bars[i] = createPhaseBar(container, phaseBarX, phaseBarY, phaseBarWidth, phaseBarHeight, 0xE0E0E0, LV_OPA_COVER, true);
        ui.car.phase_offered_bars[i] = createPhaseBar(container, phaseBarX, phaseBarY, phaseBarWidth, phaseBarHeight, 0x8BC34A, LV_OPA_40, true);
        ui.car.phase_bars[i] = createPhaseBar(container, phaseBarX, phaseBarY, 0, phaseBarHeight, COLOR_BAR_GENERATION, LV_OPA_COVER, true);
    }
    ui.car.soc_bar = lv_bar_create(container);
    lv_obj_set_size(ui.car.soc_bar, SCREEN_WIDTH - (4 * PADDING) - 16, 20);
    lv_obj_set_pos(ui.car.soc_bar, 0, 65);
    lv_bar_set_value(ui.car.soc_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(ui.car.soc_bar, lv_color_hex(0xE0E0E0), LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui.car.soc_bar, lv_color_hex(0x4CAF50), LV_PART_INDICATOR);
    lv_obj_set_style_radius(ui.car.soc_bar, 8, LV_PART_MAIN);
    lv_obj_set_style_radius(ui.car.soc_bar, 8, LV_PART_INDICATOR);
    ui.car.plan_soc_marker = lv_obj_create(container);
    lv_obj_set_size(ui.car.plan_soc_marker, 2, 20);
    lv_obj_set_pos(ui.car.plan_soc_marker, 0, 65);
    lv_obj_set_style_bg_color(ui.car.plan_soc_marker, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_width(ui.car.plan_soc_marker, 0, 0);
    lv_obj_set_style_radius(ui.car.plan_soc_marker, 1, 0);
    lv_obj_add_flag(ui.car.plan_soc_marker, LV_OBJ_FLAG_HIDDEN);
    ui.car.limit_soc_marker = lv_obj_create(container);
    lv_obj_set_size(ui.car.limit_soc_marker, 6, 28);
    lv_obj_set_pos(ui.car.limit_soc_marker, 0, 61);
    lv_obj_set_style_bg_color(ui.car.limit_soc_marker, lv_color_hex(0x4CAF50), 0);
    lv_obj_set_style_border_width(ui.car.limit_soc_marker, 0, 0);
    lv_obj_set_style_radius(ui.car.limit_soc_marker, 3, 0);
    lv_obj_add_flag(ui.car.limit_soc_marker, LV_OBJ_FLAG_HIDDEN);
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
    ui.car.soc_value = lv_label_create(container);
    lv_label_set_text(ui.car.soc_value, "0%");
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
    lv_label_set_text(ui.car.range_value, "-- km");
    styleLabelSecondary(ui.car.range_value);
    positionAndAlign(ui.car.range_value, 0, 130, 120, LV_TEXT_ALIGN_LEFT);
    ui.car.ladelimit_value = lv_label_create(container);
    lv_label_set_text(ui.car.ladelimit_value, "---");
    styleLabelPrimary(ui.car.ladelimit_value);
    positionAndAlign(ui.car.ladelimit_value, SCREEN_WIDTH-(4*PADDING)-16-120, 110, 120, LV_TEXT_ALIGN_RIGHT);
}
