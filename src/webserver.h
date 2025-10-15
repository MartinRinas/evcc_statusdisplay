/*
 * Web server for EVCC Display status and logging
 * Provides HTTP endpoints for monitoring and debugging
 */

#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include "config.h"
#include "logging.h"

// Forward declaration of EVCCData
extern EVCCData data;

// Setup web server endpoints
void setupWebServer(AsyncWebServer& server) {
    // Root endpoint - simple status page
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        String html = "<!DOCTYPE html><html><head><title>EVCC Display</title>";
        html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
        html += "<style>body{font-family:Arial;margin:20px;background:#f0f0f0;}";
        html += "h1{color:#333;}.card{background:white;padding:15px;margin:10px 0;border-radius:5px;box-shadow:0 2px 4px rgba(0,0,0,0.1);}";
        html += "a{color:#2196F3;text-decoration:none;font-weight:bold;}.btn{display:inline-block;padding:10px 20px;background:#2196F3;color:white;border-radius:5px;margin:5px;}";
        html += ".status{color:#4CAF50;font-weight:bold;}</style></head><body>";
        html += "<h1>EVCC Display Status</h1>";
        html += "<div class='card'><h2>System Info</h2>";
        html += "<p><strong>IP Address:</strong> " + WiFi.localIP().toString() + "</p>";
        html += "<p><strong>Free Heap:</strong> " + String(ESP.getFreeHeap()) + " bytes</p>";
        html += "<p><strong>Uptime:</strong> " + String(millis() / 1000) + " seconds</p>";
        html += "<p><strong>Debug Mode:</strong> <span class='status'>" + String(debugEnabled ? "ON" : "OFF") + "</span></p>";
        html += "</div><div class='card'><h2>Quick Links</h2>";
        html += "<a href='/logs' class='btn'>View Logs</a> ";
        html += "<a href='/status' class='btn'>JSON Status</a> ";
        html += "<a href='/debug/toggle' class='btn'>Toggle Debug</a>";
        html += "</div></body></html>";
        request->send(200, "text/html", html);
    });
    
    // Status endpoint - JSON format
    server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request){
        StaticJsonDocument<1024> doc;
        doc["uptime"] = millis() / 1000;
        doc["freeHeap"] = ESP.getFreeHeap();
        doc["debugEnabled"] = debugEnabled;
        doc["wifiConnected"] = WiFi.status() == WL_CONNECTED;
        doc["ipAddress"] = WiFi.localIP().toString();
        doc["logBufferSize"] = logCount;
        JsonObject logStats = doc.createNestedObject("log");
        logStats["total"] = logTotal;
        logStats["count"] = logCount;
        logStats["overwrites"] = logOverwrites;
        logStats["dropped"] = logDropped;
        logStats["minLevel"] = LOG_MIN_LEVEL;
        
        // Add current EVCC data
        JsonObject evcc = doc.createNestedObject("evcc");
        evcc["gridPower"] = data.gridPower;
        evcc["pvPower"] = data.pvPower;
        evcc["homePower"] = data.homePower;
        evcc["batteryPower"] = data.batteryPower;
        evcc["batterySoc"] = data.batterySoc;
        
        String output;
        serializeJson(doc, output);
        request->send(200, "application/json", output);
    });
    
    // Logs endpoint - HTML format (client converts epoch to local time)
    server.on("/logs", HTTP_GET, [](AsyncWebServerRequest *request){
        // Determine minimum level filter from query (?level=error|warn|info|debug|verbose)
        uint8_t filterLevel = LOG_MIN_LEVEL;
        if (request->hasParam("level")) {
            String lvl = request->getParam("level")->value(); lvl.toLowerCase();
            if (lvl == "error") filterLevel = LOG_LEVEL_ERROR; else
            if (lvl == "warn") filterLevel = LOG_LEVEL_WARN; else
            if (lvl == "info") filterLevel = LOG_LEVEL_INFO; else
            if (lvl == "debug") filterLevel = LOG_LEVEL_DEBUG; else
            if (lvl == "verbose") filterLevel = LOG_LEVEL_VERBOSE;
        }

        // Snapshot under lock
        LogEntry snapshot[LOG_BUFFER_SIZE];
        int snapCount = 0;
        int snapHead = 0;
        portENTER_CRITICAL(&logMux);
        snapCount = logCount;
        snapHead = logHead;
        for (int i = 0; i < snapCount; i++) {
            int idx = (logHead - snapCount + i + LOG_BUFFER_SIZE) % LOG_BUFFER_SIZE;
            snapshot[i] = logBuffer[idx];
        }
        portEXIT_CRITICAL(&logMux);

        String html = "<!DOCTYPE html><html><head><title>EVCC Display Logs</title>";
        html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
        html += "<meta http-equiv='refresh' content='10'>";
        html += "<style>body{font-family:Arial,monospace;margin:20px;background:#1e1e1e;color:#d4d4d4;}";
        html += "h1{color:#4CAF50;margin-top:0;} .log{background:#2d2d30;padding:6px 10px;margin:4px 0;border-left:3px solid #4CAF50;font-size:12px;line-height:1.4;}";
        html += ".timestamp{color:#8ab4f8;font-weight:bold;margin-right:6px;} .lvl{display:inline-block;font-size:10px;padding:2px 4px;border-radius:3px;margin-right:4px;}";
        html += ".lvl.ERR{background:#b71c1c;color:#fff;} .lvl.WRN{background:#ff9800;color:#000;} .lvl.INF{background:#2196f3;color:#fff;} .lvl.DBG{background:#455a64;color:#fff;} .lvl.VRB{background:#607d8b;color:#fff;}";
        html += ".message{color:#e0e0e0;white-space:pre-wrap;word-break:break-word;} a{color:#4CAF50;text-decoration:none;display:inline-block;margin:10px 0;} .meta{font-size:11px;color:#888;margin-bottom:10px;}";
        html += "</style></head><body>";
        html += "<h1>Debug Logs</h1>";
        html += "<div class='meta'><a href='/'>&larr; Back</a> | <a href='/debug/toggle'>Toggle Debug</a><br>";
        html += "Filter: <a href='/logs?level=error'>ERR</a> <a href='/logs?level=warn'>WRN</a> <a href='/logs?level=info'>INF</a> <a href='/logs?level=debug'>DBG</a> <a href='/logs?level=verbose'>VRB</a><br>";
        html += "Times shown in your local timezone; unsynced entries show relative ms.</div>";
        html += "<p>Total:" + String(logTotal) + " Visible:" + String(snapCount) + " Overwrites:" + String(logOverwrites) + " Dropped:" + String(logDropped) + " MinLevel:" + String(LOG_MIN_LEVEL) + "</p>";

        for (int i = 0; i < snapCount; i++) {
            const LogEntry &e = snapshot[i];
            if (e.level < filterLevel) continue;
            html += "<div class='log' data-epoch='" + String((unsigned long)e.epoch) + "' data-ms='" + String(e.timestamp) + "'>";
            html += "<span class='timestamp'>[loading]</span><span class='lvl " + String(levelToStr(e.level)) + "'>" + String(levelToStr(e.level)) + "</span>";
            html += "<span class='message'>" + String(e.message) + "</span></div>";
        }

        html += "<script>(function(){function fmt(e){if(!e)return null; if(e<100000)return null; return new Date(e*1000);}";
        html += "document.querySelectorAll('.log').forEach(function(row){var epoch=parseInt(row.dataset.epoch);var ms=row.dataset.ms;var span=row.querySelector('.timestamp');var d=fmt(epoch);if(d){span.textContent='['+d.toLocaleString()+']';}else{span.textContent='['+ms+' ms]';}});})();</script>";
        html += "</body></html>";
        request->send(200, "text/html", html);
    });
    
    // Debug toggle endpoint
    server.on("/debug/toggle", HTTP_GET, [](AsyncWebServerRequest *request){
        debugEnabled = !debugEnabled;
        String message = "Debug mode is now " + String(debugEnabled ? "ON" : "OFF");
        logMessage(message, true); // Force to serial
        
        String html = "<!DOCTYPE html><html><head><title>Debug Toggle</title>";
        html += "<meta http-equiv='refresh' content='2;url=/'>";
        html += "<style>body{font-family:Arial;margin:50px;text-align:center;background:#f0f0f0;}";
        html += "h1{color:#4CAF50;}</style></head><body>";
        html += "<h1>✓ " + message + "</h1>";
        html += "<p>Redirecting to status page...</p></body></html>";
        request->send(200, "text/html", html);
    });
    
    // 404 handler
    server.onNotFound([](AsyncWebServerRequest *request){
        request->send(404, "text/plain", "Not found");
    });
}

#endif // WEBSERVER_H
