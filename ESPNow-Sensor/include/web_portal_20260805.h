// include/web_portal.h
#ifndef WEB_PORTAL_H
#define WEB_PORTAL_H

#include <Arduino.h>
#include <WebServer.h>
#include <Preferences.h>

extern WebServer server;
extern Preferences preferences;
extern String wifi_ssid, wifi_pass, gateway_mac_str, device_hostname;
extern uint32_t sleepTimeSeconds;
extern uint8_t adc_pin;
extern float adc_factor;

void handleRoot() {
    String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<style>body{font-family:Arial;margin:20px;background:#f0f2f5;} .card{background:white;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1);max-width:400px;margin:auto;} input{width:100%;padding:10px;margin:10px 0;box-sizing:border-box;} button{width:100%;padding:12px;background:#28a745;color:white;border:none;border-radius:4px;font-size:16px;cursor:pointer;}</style>";
    html += "<title>Config</title></head><body><div class='card'><h2>Radar Setup v3.1</h2><form action='/save' method='POST'>";
    html += "<label>Name:</label><input type='text' name='hostname' value='" + device_hostname + "'>";
    html += "<label>SSID:</label><input type='text' name='ssid' value='" + wifi_ssid + "'>";
    html += "<label>Passwort:</label><input type='password' name='pass' value='" + wifi_pass + "'>";
    html += "<label>Gateway MAC:</label><input type='text' name='mac' value='" + gateway_mac_str + "'>";
    html += "<label>Schlafzeit (s):</label><input type='number' name='sleep' value='" + String(sleepTimeSeconds) + "'>";
    html += "<label>ADC Pin:</label><input type='number' name='adc_pin' value='" + String(adc_pin) + "'>";
    html += "<label>ADC Faktor:</label><input type='number' step='any' name='adc_factor' value='" + String(adc_factor) + "'>";
    html += "<button type='submit'>Speichern</button></form></div></body></html>";
    server.send(200, "text/html", html);
}

void handleSave() {
    if (server.hasArg("ssid") && server.hasArg("pass") && server.hasArg("mac") && server.hasArg("sleep") && server.hasArg("hostname") && server.hasArg("adc_pin") && server.hasArg("adc_factor")) {
        if (preferences.begin("sensor_cfg", false)) {
            preferences.putString("wifi_ssid", server.arg("ssid")); preferences.putString("wifi_pass", server.arg("pass"));
            preferences.putString("gateway_mac", server.arg("mac")); preferences.putString("dev_hostname", server.arg("hostname"));
            preferences.putUInt("sleep_sec", server.arg("sleep").toInt()); 
            preferences.putUChar("adc_pin", (uint8_t)server.arg("adc_pin").toInt()); preferences.putFloat("adc_factor", server.arg("adc_factor").toFloat());
            preferences.end();
        }
        server.send(200, "text/html", "<h3>Gespeichert! Starte neu...</h3>"); delay(2000); esp_restart();
    } else { server.send(400, "text/plain", "Fehler"); }
}

void setupWebserver() {
    server.on("/", handleRoot); 
    server.on("/save", HTTP_POST, handleSave); 
    server.on("/favicon.ico", []() { server.send(204); });
    server.on("/generate_204", []() { server.send(204); });
    server.on("/success.txt", []() { server.send(200, "text/plain", "success"); });
    server.on("/ncsi.txt", []() { server.send(200, "text/plain", "success"); });
    server.on("/hotspot-detect.html", []() { server.send(200, "text/html", "success"); });
    server.on("/connecttest.txt", []() { server.send(200, "text/plain", "success"); });
    server.on("/wpad.dat", []() { server.send(204); });
    server.onNotFound(handleRoot);
}

#endif
