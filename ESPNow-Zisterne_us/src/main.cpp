#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <HardwareSerial.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>

#define DEFAULT_SSID "SaHiCo2"
#define DEFAULT_PASS "DEIN_WLAN_PASSWORT"
#define DEFAULT_GATEWAY "90:70:69:33:73:F4"
#define DEFAULT_HOSTNAME "zisterne_us"
#define DEFAULT_SLEEP 30 
#define RETRIES 5 

#define FIRMWARE_VERSION 26 
#define SENSOR_TYPE 1       
#define JUMPER_PIN 13       
#define US_POWER_PIN 4      

const uint8_t channels[] = {6, 1, 11}; 
const uint8_t numChannels = 3;

typedef struct __attribute__((__packed__)) struct_distance {
    float distance; uint8_t ok; uint8_t jumper; uint8_t ota_state; 
} struct_distance;

typedef struct __attribute__((__packed__)) struct_universal_message {
    uint8_t sensor_type; uint8_t firmware_ver; 
    uint8_t payload[240]; // KORREKTUR: Jetzt absolut sauber als echtes Array deklariert!
} struct_universal_message;

HardwareSerial JSNSerial(1); 
struct_distance myData;
struct_universal_message outPacket;
volatile bool ackReceived = false;
bool stayAwakeForOTA = false;
uint32_t lastMeasurementTime = 0;

String wifi_ssid, wifi_pass, gateway_mac_str, device_hostname;
uint8_t gatewayMac[6]; // KORREKTUR: Auch hier das 6-Byte-Array festgeschrieben
uint32_t sleepTimeSeconds;

Preferences preferences;
WebServer server(80);
DNSServer dnsServer;

void parseMacAddress(String macStr, uint8_t *macArray) {
    unsigned int m[6];
    if (sscanf(macStr.c_str(), "%x:%x:%x:%x:%x:%x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) == 6) {
        for (int i = 0; i < 6; ++i) macArray[i] = (uint8_t)m[i];
    }
}

void OnDataRecv(const uint8_t * mac_addr, const uint8_t *incoming, int len) {
    char buffer[32] = {0};
    memcpy(buffer, incoming, (len < 31) ? len : 31);
    String msg = String(buffer); msg.trim();

    if (msg.equalsIgnoreCase("ACK")) ackReceived = true;
    else if (msg.equalsIgnoreCase("OTA=1")) { ackReceived = true; stayAwakeForOTA = true; }
    else if (msg.equalsIgnoreCase("OTA=0")) { ackReceived = true; stayAwakeForOTA = false; }
    else if (msg.startsWith("SLEEP=")) {
        ackReceived = true;
        int parsedVal = msg.substring(6).toInt();
        if (parsedVal >= 10 && parsedVal < 86400) {
            sleepTimeSeconds = parsedVal;
            if (preferences.begin("zisterne_cfg", false)) {
                preferences.putUInt("sleep_sec", sleepTimeSeconds); preferences.end();
            }
        }
    }
}

void setWifiChannel(uint8_t channel) {
    esp_wifi_set_promiscuous(true); esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE); esp_wifi_set_promiscuous(false);
}

bool readUltrasonicSensor(float &resultDistance) {
    digitalWrite(US_POWER_PIN, HIGH); delay(500); 
    while(JSNSerial.available()) JSNSerial.read();
    JSNSerial.write(0x55);
    uint32_t start_time = millis(); uint8_t buf[4] = {0}; int bytesRead = 0;
    while ((millis() - start_time < 150) && (bytesRead < 4)) {
        if (JSNSerial.available()) buf[bytesRead++] = JSNSerial.read();
    }
    digitalWrite(US_POWER_PIN, LOW); 
    if (bytesRead != 4 || buf[0] != 0xFF) return false;
    if (((buf[0] + buf[1] + buf[2]) & 0xFF) != buf[3]) return false;
    resultDistance = ((buf[1] << 8) | buf[2]) / 10.0f; return true;
}

void runMeasurementCycle() {
    ackReceived = false; float measuredDistance = 0.0f;
    bool measurementOk = readUltrasonicSensor(measuredDistance);
    myData.distance = measurementOk ? (202.8f - measuredDistance) : 0.0f;
    myData.ok = measurementOk ? 1 : 0;
    myData.jumper = (digitalRead(JUMPER_PIN) == LOW) ? 1 : 0;
    myData.ota_state = stayAwakeForOTA ? 1 : 0;

    outPacket.sensor_type = SENSOR_TYPE; outPacket.firmware_ver = FIRMWARE_VERSION;
    memcpy(outPacket.payload, &myData, sizeof(myData));

    for (uint8_t c = 0; c < numChannels; c++) {
        uint8_t targetChannel = channels[c]; setWifiChannel(targetChannel);
        esp_now_peer_info_t peerInfo = {}; memcpy(peerInfo.peer_addr, gatewayMac, 6);
        peerInfo.channel = targetChannel; peerInfo.encrypt = false;
        if (esp_now_is_peer_exist(gatewayMac)) esp_now_del_peer(gatewayMac);
        esp_now_add_peer(&peerInfo);

        for (int retry = 1; retry <= RETRIES; retry++) {
            Serial.printf("Sende an Gateway %s auf Kanal %d (Versuch %d/%d)...\n", gateway_mac_str.c_str(), targetChannel, retry, RETRIES);
            esp_now_send(gatewayMac, (uint8_t *)&outPacket, 2 + sizeof(myData));
            uint32_t waitStart = millis();
            while (millis() - waitStart < 100) { if (ackReceived) return; delay(5); }
        }
    }
}

void enterDeepSleep() {
    Serial.printf("\n>>> Deep Sleep fuer %d Sekunden. <<<\n\n", sleepTimeSeconds);
    esp_sleep_enable_timer_wakeup((uint64_t)sleepTimeSeconds * 1000000ULL); esp_deep_sleep_start();
}

void handleRoot() {
    String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<style>body{font-family:Arial;margin:20px;background:#f0f2f5;} .card{background:white;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1);max-width:400px;margin:auto;} input{width:100%;padding:10px;margin:10px 0;box-sizing:border-box;} button{width:100%;padding:12px;background:#28a745;color:white;border:none;border-radius:4px;font-size:16px;cursor:pointer;}</style>";
    html += "<title>Config</title></head><body><div class='card'><h2>Sensor Setup v2.1</h2><form action='/save' method='POST'>";
    html += "<label>Name:</label><input type='text' name='hostname' value='" + device_hostname + "'>";
    html += "<label>SSID:</label><input type='text' name='ssid' value='" + wifi_ssid + "'>";
    html += "<label>Passwort:</label><input type='password' name='pass' value='" + wifi_pass + "'>";
    html += "<label>Gateway MAC:</label><input type='text' name='mac' value='" + gateway_mac_str + "'>";
    html += "<label>Schlafzeit (s):</label><input type='number' name='sleep' value='" + String(sleepTimeSeconds) + "'>";
    html += "<button type='submit'>Speichern</button></form></div></body></html>";
    server.send(200, "text/html", html);
}

void handleSave() {
    if (server.hasArg("ssid") && server.hasArg("pass") && server.hasArg("mac") && server.hasArg("sleep") && server.hasArg("hostname")) {
        if (preferences.begin("zisterne_cfg", false)) {
            preferences.putString("wifi_ssid", server.arg("ssid")); preferences.putString("wifi_pass", server.arg("pass"));
            preferences.putString("gateway_mac", server.arg("mac")); preferences.putString("dev_hostname", server.arg("hostname"));
            preferences.putUInt("sleep_sec", server.arg("sleep").toInt()); preferences.end();
        }
        server.send(200, "text/html", "<h3>Gespeichert! Starte neu...</h3>"); delay(2000); esp_restart();
    } else { server.send(400, "text/plain", "Fehler"); }
}

void setup() {
    Serial.begin(115200); JSNSerial.begin(9600, SERIAL_8N1, 16, 17); 
    pinMode(JUMPER_PIN, INPUT_PULLUP); pinMode(US_POWER_PIN, OUTPUT);

    if (!preferences.begin("zisterne_cfg", true)) {
        preferences.begin("zisterne_cfg", false); preferences.end(); preferences.begin("zisterne_cfg", true);
    }
    wifi_ssid = preferences.getString("wifi_ssid", DEFAULT_SSID); wifi_pass = preferences.getString("wifi_pass", DEFAULT_PASS);
    gateway_mac_str = preferences.getString("gateway_mac", DEFAULT_GATEWAY); device_hostname = preferences.getString("dev_hostname", DEFAULT_HOSTNAME);
    sleepTimeSeconds = preferences.getUInt("sleep_sec", DEFAULT_SLEEP); preferences.end();

    parseMacAddress(gateway_mac_str, gatewayMac);
    WiFi.mode(WIFI_STA);
    if (esp_now_init() == ESP_OK) { esp_now_register_recv_cb((esp_now_recv_cb_t)OnDataRecv); runMeasurementCycle(); }

    if (digitalRead(JUMPER_PIN) == HIGH && !stayAwakeForOTA) enterDeepSleep(); 

    Serial.println("\nWachmodus aktiv!"); esp_now_deinit(); 
    String ap_name = device_hostname + "_ap"; 
    WiFi.setHostname(device_hostname.c_str()); 
    
    // REPARATUR: Löscht die alten, intern im Chip gemerkten Zugangsdaten restlos,
    // damit ein falsches Passwort im Webformular auch wirklich als "falsch" erkannt wird!
    WiFi.disconnect(true, true);
    delay(500); // Dem Funkchip Zeit geben, die Tabellen zu leeren

    // Startet den echten Verbindungsversuch mit den Daten aus den Preferences
    WiFi.begin(wifi_ssid.c_str(), wifi_pass.c_str());
    
    uint32_t wifiTimeout = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - wifiTimeout < 10000) { 
        delay(500); 
        Serial.print("."); 
    }

    // FALL 1: Router ist mit den übergebenen Daten NICHT erreichbar -> Eigenen AP öffnen
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("\nHeim-WLAN nicht erreichbar. Oeffne eigenen Access Point...");
        WiFi.disconnect(true, true); 
        delay(500); 

        WiFi.mode(WIFI_AP); 
        String ap_pass = ap_name;
        while (ap_pass.length() < 8) ap_pass += "1"; 
        
        if (WiFi.softAP(ap_name.c_str(), ap_pass.c_str())) {
            dnsServer.start(53, "*", WiFi.softAPIP());
            Serial.printf("Hotspot erfolgreich geoeffnet!\nName: %s\nPW: %s\nIP: %s\n", 
                          ap_name.c_str(), ap_pass.c_str(), WiFi.softAPIP().toString().c_str());
        } else {
            Serial.println("[Fehler] Hotspot-Start fehlgeschlagen!");
        }
    } 
    // FALL 2: Erfolgreich im Heim-WLAN eingeloggt (mit den neuen, korrekten Daten)
    else {
        Serial.print("\n[WLAN] Erfolgreich mit Router verbunden! ");
        Serial.print("IP-Adresse: ");
        Serial.println(WiFi.localIP().toString());
    }

    server.on("/", handleRoot); 
    server.on("/save", HTTP_POST, handleSave);
    server.on("/favicon.ico", []() { server.send(204); });
    server.onNotFound(handleRoot); 
    server.begin();
    ArduinoOTA.setHostname(device_hostname.c_str()); 
    ArduinoOTA.begin();
    Serial.println("[System] Bereit fuer OTA Updates und Weboberflaeche.");
}

void loop() {
    if (WiFi.getMode() == WIFI_MODE_AP) dnsServer.processNextRequest();
    server.handleClient(); ArduinoOTA.handle();
    if (millis() - lastMeasurementTime > 60000) { lastMeasurementTime = millis(); float d; readUltrasonicSensor(d); }
    delay(10);
}
