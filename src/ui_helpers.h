// ui_helpers.h - UI styling and creation helper declarations
#pragma once

#include <lvgl.h>
#include <Arduino.h>
#include "config.h"

// Forward declaration for power formatting (implemented in main sketch)
String formatPower(float watts);

// Forward declarations / shared UI structs
struct UIElements {
    lv_obj_t* screen;
    lv_obj_t* upper_container;
    lv_obj_t* lower_container;

    struct { lv_obj_t* desc; lv_obj_t* value1; lv_obj_t* value2; } generation, battery_discharge, grid_feed, consumption, loadpoint, battery_charge, grid_feedin;

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

    // Overlay bar (aggregated flows overlapping IN/OUT bars)
    struct {
        lv_obj_t* container;
        lv_obj_t* selfpv_segment;       // Portion of consumption covered directly by PV
        lv_obj_t* selfbattery_segment;  // Portion of consumption covered by battery discharge
        lv_obj_t* grid_import_segment;  // Grid import powering consumption
        lv_obj_t* pv_export_segment;    // PV exported to grid
        lv_obj_t* selfpv_label;
        lv_obj_t* selfbattery_label;
        lv_obj_t* grid_import_label;
        lv_obj_t* pv_export_label;
    } overlay_bar;

    struct {
        lv_obj_t* title_label;
        lv_obj_t* car_label;
        lv_obj_t* power_desc;    // "LEISTUNG" label
        lv_obj_t* power_label;
        lv_obj_t* duration_desc; // "RESTZEIT" label
        lv_obj_t* phase_bg_bars[3];
        lv_obj_t* phase_offered_bars[3];
        lv_obj_t* phase_bars[3];
        lv_obj_t* lightning_icon; // Lightning icon placed to the right of phase indicators
        lv_obj_t* soc_bar;
        lv_obj_t* limit_indicator; // Background bar showing charging limit (EVCC_GREEN), visible only when charging
        lv_obj_t* plan_soc_marker;
        lv_obj_t* limit_soc_marker;
        lv_obj_t* soc_desc;
        lv_obj_t* plan_desc;
        lv_obj_t* limit_desc;
        lv_obj_t* charged_desc;  // "Geladen" label
        lv_obj_t* soc_value;
        lv_obj_t* charged_value; // Charged energy value
        lv_obj_t* range_value;
        lv_obj_t* ladedauer_value;
        lv_obj_t* plan_value;
        lv_obj_t* plan_soc_value;
        lv_obj_t* ladelimit_value;
    } car;
};

// Extern global UI instance (defined in main sketch)
extern UIElements ui;

// Styling helpers
void styleLabel(lv_obj_t* label, const lv_font_t* font, lv_color_t color);
void styleLabelPrimary(lv_obj_t* label);
void styleLabelSecondary(lv_obj_t* label);
void styleLabelValue(lv_obj_t* label);
void styleLabelHeader(lv_obj_t* label);
void styleContainer(lv_obj_t* container);
void styleContainerWithBorder(lv_obj_t* container);
void positionAndAlign(lv_obj_t* obj, int x, int y, int width, lv_text_align_t align);

// Phase bar helpers
void stylePhaseBar(lv_obj_t* bar, uint32_t color, lv_opa_t opacity);
lv_obj_t* createPhaseBar(lv_obj_t* parent, int x, int y, int width, int height, uint32_t color, lv_opa_t opacity, bool startHidden);

// Composite bar helpers
lv_obj_t* createCompositeBar(lv_obj_t* parent, int x, int y, int width, int height);
lv_obj_t* createBarSegment(lv_obj_t* parent, lv_color_t color, lv_obj_t** label_out);
void updateCompositeBar(lv_obj_t* container, lv_obj_t** segments, lv_obj_t** labels, float* values, int segmentCount, int barWidth);

// Row / column creation
void createEnergyRow(lv_obj_t* parent, const char* description, const char* value1, const char* value2,
                     int y_pos, lv_obj_t** desc_out, lv_obj_t** val1_out, lv_obj_t** val2_out);
lv_obj_t* createColumn(lv_obj_t* parent, const char* title, int x_pos, bool showHeader);

// Car section creation (uses global ui)
void createCarSection(lv_obj_t* parent, const char* title, const char* car_name);
