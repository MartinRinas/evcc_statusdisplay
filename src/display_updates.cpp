// display_updates.cpp - Implements periodic UI update logic
#include "display_updates.h"
#include "logging.h"

// Internal stripe application state
static bool stripe_applied = false;

// Formatting utilities (only formatPower is exported via header; others local)
String formatPower(float watts) {
    if (abs(watts) < 1000) return String((int)watts) + "W";
    if (abs(watts) < 10000) return String(watts / 1000.0, 1) + "kW";
    return String(watts / 1000.0, 0) + "kW";
}

static String formatEnergy(float wh) {
    if (abs(wh) < 1000) return String((int)wh) + "Wh";
    if (abs(wh) < 10000) return String(wh / 1000.0, 1) + "kWh";
    return String(wh / 1000.0, 0) + "kWh";
}

static String formatPercentage(float value) { return value >= 0 ? String((int)value) + "%" : "---"; }
static String formatDistance(float value) { return value >= 0 ? String((int)value) + "km" : "-- km"; }

static String formatPlanTime(const String& isoTime) {
    if (isoTime.isEmpty() || isoTime.length() < 19) return "keiner";
    int year = isoTime.substring(0, 4).toInt();
    int month = isoTime.substring(5, 7).toInt();
    int day = isoTime.substring(8, 10).toInt();
    int hour = isoTime.substring(11, 13).toInt();
    int minute = isoTime.substring(14, 16).toInt();
    time_t now = time(nullptr);
    struct tm* currentLocal = localtime(&now);
    bool isDST = currentLocal && currentLocal->tm_isdst > 0;
    int localHour = hour + (isDST ? 2 : 1);
    int localDay = day;
    int localMonth = month;
    int localYear = year;
    if (localHour >= 24) {
        localHour -= 24; localDay++;
        int daysInMonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};
        if (localYear % 4 == 0 && (localYear % 100 != 0 || localYear % 400 == 0)) daysInMonth[1] = 29;
        if (localDay > daysInMonth[localMonth - 1]) { localDay = 1; localMonth++; if (localMonth > 12) { localMonth = 1; localYear++; } }
    }
    int todayYear = currentLocal ? currentLocal->tm_year + 1900 : localYear;
    int todayMonth = currentLocal ? currentLocal->tm_mon + 1 : localMonth;
    int todayDay = currentLocal ? currentLocal->tm_mday : localDay;
    int daysDiff = 0;
    if (localYear == todayYear && localMonth == todayMonth) daysDiff = localDay - todayDay;
    else if (localYear > todayYear || (localYear == todayYear && localMonth > todayMonth)) daysDiff = 7; else daysDiff = -7;
    const char* germanDays[] = {"Sonntag","Montag","Dienstag","Mittwoch","Donnerstag","Freitag","Samstag"};
    String dayString;
    if (daysDiff == 0) dayString = "Heute"; else if (daysDiff == 1) dayString = "Morgen"; else if (daysDiff >= 2 && daysDiff < 7) {
        struct tm targetDate = {0}; targetDate.tm_year = localYear - 1900; targetDate.tm_mon = localMonth - 1; targetDate.tm_mday = localDay; mktime(&targetDate);
        dayString = String(germanDays[targetDate.tm_wday]);
    } else dayString = String(localDay) + "." + String(localMonth) + ".";
    char timeBuf[6]; snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", localHour, minute);
    return dayString + " " + timeBuf;
}

// Loadpoint rotation logic
LoadpointData* getActiveLoadpoint() {
    bool lp1Charging = data.lp1.charging;
    bool lp2Charging = data.lp2.charging;
    if (lp1Charging && !lp2Charging) return &data.lp1;
    if (lp2Charging && !lp1Charging) return &data.lp2;
    unsigned long now = millis();
    if (now - rotationState.lastRotation >= ROTATION_INTERVAL) {
        rotationState.currentLoadpoint = !rotationState.currentLoadpoint;
        rotationState.lastRotation = now;
        logMessage("Rotating to loadpoint " + String(rotationState.currentLoadpoint ? 1 : 2));
    }
    return rotationState.currentLoadpoint ? &data.lp1 : &data.lp2;
}

// Apply / remove charging stripe pattern
static void applyStripePattern(lv_obj_t* segment, bool charging) {
    if (!segment) return;
    if (charging && !stripe_applied) {
        lv_obj_add_style(segment, &stripe_style, LV_PART_INDICATOR);
        stripe_applied = true;
        logMessage("Applied stripe pattern (charging)");
    } else if (!charging && stripe_applied) {
        lv_obj_remove_style(segment, &stripe_style, LV_PART_INDICATOR);
        stripe_applied = false;
        logMessage("Removed stripe pattern (not charging)");
    }
}

// Core UI update (mirrors original logic with minor encapsulation)
void updateUI() {
    int barMaxWidth = ui.in_bar.container ? lv_obj_get_width(ui.in_bar.container) : 360;
    lv_label_set_text(ui.generation.value2, formatPower(data.pvPower).c_str());
    float scaledSolarForecastEnergy = data.solarForecastTodayEnergy * data.solarForecastScale;
    lv_label_set_text(ui.generation.value1, formatEnergy(scaledSolarForecastEnergy).c_str());
    lv_color_t genColor = (fabs(data.pvPower) < POWER_ACTIVE_THRESHOLD) ? lv_color_hex(COLOR_TEXT_SECONDARY) : lv_color_hex(COLOR_TEXT_VALUE);
    lv_obj_set_style_text_color(ui.generation.desc, genColor, 0);
    lv_obj_set_style_text_color(ui.generation.value1, genColor, 0);
    lv_obj_set_style_text_color(ui.generation.value2, genColor, 0);
    lv_label_set_text(ui.consumption.value2, formatPower(data.homePower).c_str());
    lv_color_t consColor = (fabs(data.homePower) < POWER_ACTIVE_THRESHOLD) ? lv_color_hex(COLOR_TEXT_SECONDARY) : lv_color_hex(COLOR_TEXT_VALUE);
    lv_obj_set_style_text_color(ui.consumption.desc, consColor, 0);
    lv_obj_set_style_text_color(ui.consumption.value2, consColor, 0);
    if (data.batteryPower > POWER_ACTIVE_THRESHOLD) {
        lv_label_set_text(ui.battery_discharge.value1, formatPercentage(data.batterySoc).c_str());
        lv_label_set_text(ui.battery_discharge.value2, formatPower(data.batteryPower).c_str());
        lv_label_set_text(ui.battery_charge.value1, formatPercentage(data.batterySoc).c_str());
        lv_label_set_text(ui.battery_charge.value2, formatPower(0).c_str());
        lv_obj_set_style_text_color(ui.battery_discharge.desc, lv_color_hex(COLOR_TEXT_VALUE), 0);
        lv_obj_set_style_text_color(ui.battery_discharge.value1, lv_color_hex(COLOR_TEXT_VALUE), 0);
        lv_obj_set_style_text_color(ui.battery_discharge.value2, lv_color_hex(COLOR_TEXT_VALUE), 0);
        lv_obj_set_style_text_color(ui.battery_charge.desc, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
        lv_obj_set_style_text_color(ui.battery_charge.value1, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
        lv_obj_set_style_text_color(ui.battery_charge.value2, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    } else if (data.batteryPower < -POWER_ACTIVE_THRESHOLD) {
        float chargePower = -data.batteryPower;
        lv_label_set_text(ui.battery_charge.value1, formatPercentage(data.batterySoc).c_str());
        lv_label_set_text(ui.battery_charge.value2, formatPower(chargePower).c_str());
        lv_label_set_text(ui.battery_discharge.value1, formatPercentage(data.batterySoc).c_str());
        lv_label_set_text(ui.battery_discharge.value2, formatPower(0).c_str());
        lv_obj_set_style_text_color(ui.battery_charge.desc, lv_color_hex(COLOR_TEXT_VALUE), 0);
        lv_obj_set_style_text_color(ui.battery_charge.value1, lv_color_hex(COLOR_TEXT_VALUE), 0);
        lv_obj_set_style_text_color(ui.battery_charge.value2, lv_color_hex(COLOR_TEXT_VALUE), 0);
        lv_obj_set_style_text_color(ui.battery_discharge.desc, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
        lv_obj_set_style_text_color(ui.battery_discharge.value1, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
        lv_obj_set_style_text_color(ui.battery_discharge.value2, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    } else {
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
    if (data.gridPower > POWER_ACTIVE_THRESHOLD) {
        lv_label_set_text(ui.grid_feed.value2, formatPower(data.gridPower).c_str());
        lv_label_set_text(ui.grid_feedin.value2, formatPower(0).c_str());
        lv_obj_set_style_text_color(ui.grid_feed.desc, lv_color_hex(COLOR_TEXT_VALUE), 0);
        lv_obj_set_style_text_color(ui.grid_feed.value2, lv_color_hex(COLOR_TEXT_VALUE), 0);
        lv_obj_set_style_text_color(ui.grid_feedin.desc, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
        lv_obj_set_style_text_color(ui.grid_feedin.value2, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    } else if (data.gridPower < -POWER_ACTIVE_THRESHOLD) {
        float feedinPower = -data.gridPower;
        lv_label_set_text(ui.grid_feedin.value2, formatPower(feedinPower).c_str());
        lv_label_set_text(ui.grid_feed.value2, formatPower(0).c_str());
        lv_obj_set_style_text_color(ui.grid_feedin.desc, lv_color_hex(COLOR_TEXT_VALUE), 0);
        lv_obj_set_style_text_color(ui.grid_feedin.value2, lv_color_hex(COLOR_TEXT_VALUE), 0);
        lv_obj_set_style_text_color(ui.grid_feed.desc, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
        lv_obj_set_style_text_color(ui.grid_feed.value2, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    } else {
        lv_label_set_text(ui.grid_feed.value2, formatPower(0).c_str());
        lv_label_set_text(ui.grid_feedin.value2, formatPower(0).c_str());
        lv_obj_set_style_text_color(ui.grid_feed.desc, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
        lv_obj_set_style_text_color(ui.grid_feed.value2, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
        lv_obj_set_style_text_color(ui.grid_feedin.desc, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
        lv_obj_set_style_text_color(ui.grid_feedin.value2, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    }
    float total_lp_power = data.lp1.chargePower + data.lp2.chargePower;
    lv_label_set_text(ui.loadpoint.value2, formatPower(total_lp_power).c_str());
    lv_color_t lpColor = (fabs(total_lp_power) < POWER_ACTIVE_THRESHOLD) ? lv_color_hex(COLOR_TEXT_SECONDARY) : lv_color_hex(COLOR_TEXT_VALUE);
    lv_obj_set_style_text_color(ui.loadpoint.desc, lpColor, 0);
    lv_obj_set_style_text_color(ui.loadpoint.value2, lpColor, 0);
    float inValues[3] = { data.pvPower > 0 ? data.pvPower : 0, data.batteryPower > 0 ? data.batteryPower : 0, data.gridPower > 0 ? data.gridPower : 0 };
    lv_obj_t* inSegments[3] = { ui.in_bar.generation_segment, ui.in_bar.battery_out_segment, ui.in_bar.grid_in_segment };
    lv_obj_t* inLabels[3] = { ui.in_bar.generation_label, ui.in_bar.battery_out_label, ui.in_bar.grid_in_label };
    updateCompositeBar(ui.in_bar.container, inSegments, inLabels, inValues, 3, barMaxWidth);
    float outValues[4] = { data.homePower > 0 ? data.homePower : 0, total_lp_power > 0 ? total_lp_power : 0, data.batteryPower < 0 ? -data.batteryPower : 0, data.gridPower < 0 ? -data.gridPower : 0 };
    lv_obj_t* outSegments[4] = { ui.out_bar.consumption_segment, ui.out_bar.loadpoint_segment, ui.out_bar.battery_in_segment, ui.out_bar.grid_out_segment };
    lv_obj_t* outLabels[4] = { ui.out_bar.consumption_label, ui.out_bar.loadpoint_label, ui.out_bar.battery_in_label, ui.out_bar.grid_out_label };
    updateCompositeBar(ui.out_bar.container, outSegments, outLabels, outValues, 4, barMaxWidth);
    auto* activeLP = getActiveLoadpoint();
    if (activeLP->charging) lv_label_set_text(ui.car.power_label, formatPower(activeLP->chargePower).c_str());
    else if (activeLP->plugged) lv_label_set_text(ui.car.power_label, "Verbunden");
    else lv_label_set_text(ui.car.power_label, "Nicht verbunden");
    if (activeLP->soc >= 0) {
        lv_bar_set_value(ui.car.soc_bar, (int)activeLP->soc, LV_ANIM_OFF);
        lv_label_set_text(ui.car.soc_value, formatPercentage(activeLP->soc).c_str());
        applyStripePattern(ui.car.soc_bar, activeLP->charging);
    } else {
        lv_label_set_text(ui.car.soc_value, "---");
    }
    int phaseBarWidth = 30;
    if (activeLP->charging) {
        for (int i = 0; i < 3; i++) {
            if (i < activeLP->phasesActive && activeLP->maxCurrent > 0) {
                lv_obj_clear_flag(ui.car.phase_bg_bars[i], LV_OBJ_FLAG_HIDDEN);
                float offeredRatio = activeLP->offeredCurrent / activeLP->maxCurrent; if (offeredRatio > 1.0) offeredRatio = 1.0;
                int offeredWidth = (int)(offeredRatio * phaseBarWidth); if (offeredWidth < 1 && activeLP->offeredCurrent > 0.1) offeredWidth = 1;
                lv_obj_set_width(ui.car.phase_offered_bars[i], offeredWidth);
                lv_obj_clear_flag(ui.car.phase_offered_bars[i], LV_OBJ_FLAG_HIDDEN);
                if (activeLP->chargeCurrents[i] > 0) {
                    float currentRatio = activeLP->chargeCurrents[i] / activeLP->maxCurrent; if (currentRatio > 1.0) currentRatio = 1.0;
                    int actualWidth = (int)(currentRatio * phaseBarWidth); if (actualWidth < 1 && activeLP->chargeCurrents[i] > 0.1) actualWidth = 1;
                    lv_obj_set_width(ui.car.phase_bars[i], actualWidth);
                    lv_obj_clear_flag(ui.car.phase_bars[i], LV_OBJ_FLAG_HIDDEN);
                } else lv_obj_add_flag(ui.car.phase_bars[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(ui.car.phase_bg_bars[i], LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui.car.phase_offered_bars[i], LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui.car.phase_bars[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    } else {
        for (int i = 0; i < 3; i++) {
            lv_obj_add_flag(ui.car.phase_bg_bars[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui.car.phase_offered_bars[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui.car.phase_bars[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (activeLP->effectivePlanSoc > 0) {
        int barWidth = SCREEN_WIDTH - (4 * PADDING) - 16;
        int markerX = (activeLP->effectivePlanSoc / 100.0) * barWidth - 1;
        lv_obj_set_pos(ui.car.plan_soc_marker, markerX, 65);
        lv_obj_clear_flag(ui.car.plan_soc_marker, LV_OBJ_FLAG_HIDDEN);
    } else lv_obj_add_flag(ui.car.plan_soc_marker, LV_OBJ_FLAG_HIDDEN);
    if (activeLP->effectiveLimitSoc >= 0) {
        int barWidth = SCREEN_WIDTH - (4 * PADDING) - 16;
        int markerX = (activeLP->effectiveLimitSoc / 100.0) * barWidth - 3;
        if (markerX < 0) markerX = 0; if (markerX > barWidth - 6) markerX = barWidth - 6;
        lv_obj_set_pos(ui.car.limit_soc_marker, markerX, 61);
        lv_obj_clear_flag(ui.car.limit_soc_marker, LV_OBJ_FLAG_HIDDEN);
    } else lv_obj_add_flag(ui.car.limit_soc_marker, LV_OBJ_FLAG_HIDDEN);
    if (activeLP->vehicleRange >= 0) lv_label_set_text(ui.car.range_value, formatDistance(activeLP->vehicleRange).c_str());
    else lv_label_set_text(ui.car.range_value, formatDistance(-1).c_str());
    if (!activeLP->vehicleTitle.isEmpty()) lv_label_set_text(ui.car.car_label, activeLP->vehicleTitle.c_str());
    if (!activeLP->title.isEmpty()) lv_label_set_text(ui.car.title_label, activeLP->title.c_str());
    if (!activeLP->effectivePlanTime.isEmpty()) {
        lv_label_set_text(ui.car.plan_value, formatPlanTime(activeLP->effectivePlanTime).c_str());
        if (activeLP->effectivePlanSoc >= 0) lv_label_set_text(ui.car.plan_soc_value, formatPercentage(activeLP->effectivePlanSoc).c_str());
        else lv_label_set_text(ui.car.plan_soc_value, "");
    } else {
        lv_label_set_text(ui.car.plan_value, "keiner");
        lv_label_set_text(ui.car.plan_soc_value, "");
    }
    if (activeLP->effectiveLimitSoc >= 0) lv_label_set_text(ui.car.ladelimit_value, formatPercentage(activeLP->effectiveLimitSoc).c_str());
    else lv_label_set_text(ui.car.ladelimit_value, "---");
    if (!activeLP->planProjectedStart.isEmpty()) {
        String formattedTime = formatPlanTime(activeLP->planProjectedStart);
        char projectedDisplay[64]; snprintf(projectedDisplay, sizeof(projectedDisplay), "|--> %s", formattedTime.c_str());
        lv_label_set_text(ui.car.ladedauer_value, projectedDisplay);
    } else lv_label_set_text(ui.car.ladedauer_value, "--:--");
}
