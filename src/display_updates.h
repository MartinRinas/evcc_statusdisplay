// display_updates.h - UI update logic (controller layer)
#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "config.h"
#include "ui_helpers.h"

// Extern data & UI provided by main / other modules
extern EVCCData data;
extern UIElements ui;
extern RotationState rotationState;
extern lv_style_t stripe_style;

// Accessor for current active loadpoint (rotation-aware)
LoadpointData* getActiveLoadpoint();

// Core periodic UI update
void updateUI();

// Formatting utility exposed (required by ui_helpers for segment labels)
String formatPower(float watts);
