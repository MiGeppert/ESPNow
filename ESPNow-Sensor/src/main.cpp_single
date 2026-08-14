#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <HardwareSerial.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ModbusMaster.h>

// --- CONFIG DEFAULTS ---
//#define DEFAULT_SSID "SaHiCo2"
//#define DEFAULT_PASS "!SicherHeit12"
//#define DEFAULT_GATEWAY "90:70:69:33:73:F4"
#define DEFAULT_HOSTNAME "zisterne_radar"
#define DEFAULT_SSID "SSID"
#define DEFAULT_PASS "PASSWORD"
#define DEFAULT_GATEWAY "00:00:00:00:00:00"
//#define DEFAULT_HOSTNAME "espnow_sensor"
#define DEFAULT_SLEEP 30 
#define RETRIES 5 
#define DEFAULT_ADC_PIN 34
#define DEFAULT_ADC_FACTOR 2.0f

#define FIRMWARE_VERSION 30 
#define SENSOR_TYPE 2       
#define JUMPER_PIN 13       
#define SEN_POWER_PIN 4

const uint8_t channels[] = {6, 1, 11}; 
const uint8_t numChannels = 3;

typedef struct __attribute__((__packed__)) struct_radar {
    float pv1; float pv2; float pv3; float pv4; float pv5;
    uint8_t ok; uint8_t jumper; uint8_t ota_state; float battery_voltage;
} struct_radar;

typedef struct __attribute__((__packed__)) struct_universal_message {
    uint8_t sensor_type; uint8_t firmware_ver; uint8_t payload[]; 
} struct_universal_message;

HardwareSerial SensorSerial(1); 
ModbusMaster node;
struct_radar myData;
struct_universal_message outPacket;
volatile bool ackReceived = false;
bool stayAwakeForOTA = false;
uint32_t lastMeasurementTime = 0;



String wifi_ssid, wifi_pass, gateway_mac_str, device_hostname;
uint8_t gatewayMac[6]; 
uint32_t sleepTimeSeconds;
uint8_t adc_pin;
float adc_factor;

Preferences preferences; WebServer server(80); DNSServer dnsServer;

union RegisterFloat {
    uint16_t registers[2]; // Ein Array aus zwei 16-Bit-Werten (= 32 Bit Gesamtgröße)
    float value;           // Ein 32-Bit-Gleitkommawert (= 32 Bit Gesamtgröße)
};

void parseMacAddress(String macStr, uint8_t *macArray) {
    unsigned int m[6];
    if (sscanf(macStr.c_str(), "%x:%x:%x:%x:%x:%x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) == 6) {
        for (int i = 0; i < 6; ++i) macArray[i] = (uint8_t)m[i];
    }
}

void OnDataRecv(const uint8_t * mac_addr, const uint8_t *incoming, int len) {
    char buffer[32] = {0}; memcpy(buffer, incoming, (len < 31) ? len : 31);
    String msg = String(buffer); msg.trim();
    if (msg.equalsIgnoreCase("ACK")) ackReceived = true;
    else if (msg.equalsIgnoreCase("OTA=1")) { ackReceived = true; stayAwakeForOTA = true; }
    else if (msg.equalsIgnoreCase("OTA=0")) { ackReceived = true; stayAwakeForOTA = false; }
    else if (msg.startsWith("SLEEP=")) {
        ackReceived = true; int parsedVal = msg.substring(6).toInt();
        if (parsedVal >= 10 && parsedVal < 86400) {
            sleepTimeSeconds = parsedVal;
            if (preferences.begin("sensor_cfg", false)) { preferences.putUInt("sleep_sec", sleepTimeSeconds); preferences.end(); }
        }
    }
}

void setWifiChannel(uint8_t channel) {
    esp_wifi_set_promiscuous(true); esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE); esp_wifi_set_promiscuous(false);
}

bool readModbusSensor(float *pvs) {
    digitalWrite(SEN_POWER_PIN, HIGH);
    delay(1500); 

    uint8_t result = 0;
    const int maxModbusRetries = 10; 

    for (int attempt = 1; attempt <= maxModbusRetries; attempt++) {
        
        while(SensorSerial.available()) SensorSerial.read();

        Serial.printf("[Modbus] Sende Request ab (FC=03, Register=910)... (Versuch %d/%d)\n", attempt, maxModbusRetries);
        result = node.readHoldingRegisters(910, 10);

        // Wenn der Befehl erfolgreich war, brechen wir die Retry-Schleife sofort ab!
        if (result == node.ku8MBSuccess) {
            break;
        }

        Serial.printf("[Modbus-Warnung] Versuch %d fehlgeschlagen mit Code: 0x%02X. Warte auf neuen Versuch...\n", attempt, result);
        delay(1000);
    }

    digitalWrite(SEN_POWER_PIN, LOW); 

    if (result == node.ku8MBSuccess) {
        Serial.println("[Modbus-Erfolg] Daten erfolgreich empfangen. Wandle Register in Floats...");
        
        for (int i = 0; i < 5; i++) {
            uint16_t reg1 = node.getResponseBuffer(i * 2);
            uint16_t reg2 = node.getResponseBuffer((i * 2) + 1);

            RegisterFloat regFloat;
            regFloat.registers[0] = reg2;
            regFloat.registers[1] = reg1;
            
            pvs[i] = regFloat.value;
            Serial.printf("  -> PV%d: %f\n", i + 1, pvs[i]);
        }
        return true;
    } else {
        Serial.printf("[Modbus-HART-FEHLER] Alle %d Versuche fehlgeschlagen! Letzter Code: 0x%02X\n", maxModbusRetries, result);
        return false;
    }
}

void runMeasurementCycle() {
    ackReceived = false; 
    float sensorValues[5] = {0.0f};
    
    // Führt die Modbus-Messung mit den erweiterten Debugs aus
    bool measurementOk = readModbusSensor(sensorValues);
    
    myData.pv1 = sensorValues[0]; 
    myData.pv2 = sensorValues[1];
    myData.pv3 = sensorValues[2]; 
    myData.pv4 = sensorValues[3]; 
    myData.pv5 = sensorValues[4];
    
    myData.ok = measurementOk ? 1 : 0;
    myData.jumper = (digitalRead(JUMPER_PIN) == LOW) ? 1 : 0;
    myData.ota_state = stayAwakeForOTA ? 1 : 0;

    int rawAnalog = analogRead(adc_pin); 
    myData.battery_voltage = ((rawAnalog / 4096.0f) * 3.3f) * adc_factor;

    outPacket.sensor_type = SENSOR_TYPE; 
    outPacket.firmware_ver = FIRMWARE_VERSION;
    memcpy(outPacket.payload, &myData, sizeof(myData));

    for (uint8_t c = 0; c < numChannels; c++) {
        uint8_t targetChannel = channels[c]; 
        setWifiChannel(targetChannel);
        
        esp_now_peer_info_t peerInfo = {}; 
        memcpy(peerInfo.peer_addr, gatewayMac, 6);
        peerInfo.channel = targetChannel; 
        peerInfo.encrypt = false;
        
        if (esp_now_is_peer_exist(gatewayMac)) esp_now_del_peer(gatewayMac);
        esp_now_add_peer(&peerInfo);

        for (int retry = 1; retry <= RETRIES; retry++) {
            Serial.printf("[Funk] Sende Radar-Daten an Gateway auf Kanal %d (Versuch %d/%d)...\n", targetChannel, retry, RETRIES);
            esp_now_send(gatewayMac, (uint8_t *)&outPacket, 2 + sizeof(myData));
            uint32_t waitStart = millis();
            while (millis() - waitStart < 100) { if (ackReceived) return; delay(5); }
        }
    }
}

void enterDeepSleep() {
    Serial.printf("\n>>> Deep Sleep fuer %d Sekunden. <<<\n\n", sleepTimeSeconds);
    esp_sleep_enable_timer_wakeup((uint64_t)sleepTimeSeconds * 1000000ULL); 
    esp_deep_sleep_start();
}

void handleRoot() {
    String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<style>body{font-family:Arial;margin:20px;background:#f0f2f5;} .card{background:white;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1);max-width:400px;margin:auto;} input{width:100%;padding:10px;margin:10px 0;box-sizing:border-box;} button{width:100%;padding:12px;background:#28a745;color:white;border:none;border-radius:4px;font-size:16px;cursor:pointer;}</style>";
    html += "<title>Config</title></head><body><div class='card'><h2>Radar Setup v3.0</h2><form action='/save' method='POST'>";
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
            preferences.putString("wifi_ssid", server.arg("ssid")); 
            preferences.putString("wifi_pass", server.arg("pass"));
            preferences.putString("gateway_mac", server.arg("mac")); 
            preferences.putString("dev_hostname", server.arg("hostname"));
            preferences.putUInt("sleep_sec", server.arg("sleep").toInt()); 
            preferences.putUChar("adc_pin", (uint8_t)server.arg("adc_pin").toInt()); 
            preferences.putFloat("adc_factor", server.arg("adc_factor").toFloat());
            preferences.end();
        }
        server.send(200, "text/html", "<h3>Gespeichert! Starte neu...</h3>"); delay(2000); esp_restart();
    } else { server.send(400, "text/plain", "Fehler"); }
}

void setup() {
    Serial.begin(115200); 
    pinMode(JUMPER_PIN, INPUT_PULLUP); 
    pinMode(SEN_POWER_PIN, OUTPUT);
    digitalWrite(SEN_POWER_PIN, LOW); 

    if (!preferences.begin("sensor_cfg", true)) {
        preferences.begin("sensor_cfg", false); preferences.end(); preferences.begin("sensor_cfg", true);
    }
    wifi_ssid = preferences.getString("wifi_ssid", DEFAULT_SSID); 
    wifi_pass = preferences.getString("wifi_pass", DEFAULT_PASS);
    gateway_mac_str = preferences.getString("gateway_mac", DEFAULT_GATEWAY); 
    device_hostname = preferences.getString("dev_hostname", DEFAULT_HOSTNAME);
    sleepTimeSeconds = preferences.getUInt("sleep_sec", DEFAULT_SLEEP); 
    adc_pin = preferences.getUChar("adc_pin", DEFAULT_ADC_PIN); 
    adc_factor = preferences.getFloat("adc_factor", DEFAULT_ADC_FACTOR);
    preferences.end();

    parseMacAddress(gateway_mac_str, gatewayMac);

    SensorSerial.begin(115200, SERIAL_8N1, 16, 17);
    node.begin(1, SensorSerial);

    bool isFactoryReset = (gateway_mac_str == "00:00:00:00:00:00" || wifi_ssid == "SSID" || wifi_ssid == "KEIN_WLAN");

    if (!isFactoryReset) {
        Serial.println("[System] Normaler Betriebsmodus gestartet.");
        WiFi.mode(WIFI_STA);
        if (esp_now_init() == ESP_OK) { 
            esp_now_register_recv_cb((esp_now_recv_cb_t)OnDataRecv); 
            runMeasurementCycle(); 
        }

        if (digitalRead(JUMPER_PIN) == HIGH && !stayAwakeForOTA) {
            enterDeepSleep(); 
        }
    } else {
        Serial.println("[System] Sensor befindet sich im Werkszustand!");
        Serial.println("[System] Überspringe Messzyklus und erzwinge sofortigen Portal-Start.");
    }

    Serial.println("\nWachmodus aktiv! Initialisiere WLAN..."); 
    
    // REPARATUR: Nur deinitialisieren, wenn ESP-NOW im Normalmodus auch gestartet wurde!
    if (!isFactoryReset) {
        esp_now_deinit(); 
    }
    
    String ap_name = device_hostname + "_ap"; 
    WiFi.setHostname(device_hostname.c_str()); 
    
    // WLAN-Hardware-Cache radikal leeren für saubere Zustände
    WiFi.disconnect(true, true);
    delay(400);

    // Verbindungsversuch zum Haus-Router starten
    Serial.printf("[WLAN] Versuche Verbindung mit Router '%s' ", wifi_ssid.c_str());
    WiFi.begin(wifi_ssid.c_str(), wifi_pass.c_str());
    
    uint32_t wifiTimeout = millis(); 
    while (WiFi.status() != WL_CONNECTED && millis() - wifiTimeout < 5000) { 
        delay(500); 
        Serial.print("."); 
    }

    // FALL 1: Router ist nicht erreichbar -> Eigenen Access Point öffnen
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("\n[WLAN] Heim-WLAN nicht erreichbar. Oeffne eigenen Access Point...");
        WiFi.disconnect(true, true); 
        delay(400); 

        WiFi.mode(WIFI_AP); 
        
        // Eigener Name als Passwort, aufgefüllt auf min. 8 WPA2-Zeichen
        String ap_pass = ap_name; 
        while (ap_pass.length() < 8) ap_pass += "1"; 
        
        if (WiFi.softAP(ap_name.c_str(), ap_pass.c_str())) {
            dnsServer.start(53, "*", WiFi.softAPIP());
            Serial.printf("Hotspot erfolgreich geoeffnet!\nName: %s\nPW: %s\nIP: %s\n", 
                          ap_name.c_str(), ap_pass.c_str(), WiFi.softAPIP().toString().c_str());
        } else {
            Serial.println("[Fehler] Hotspot-Start hardwareseitig fehlgeschlagen!");
        }
    } 
    // FALL 2: Erfolgreich im bestehenden Heimnetzwerk eingeloggt
    else {
        Serial.print("\n[WLAN] Erfolgreich mit Router verbunden! ");
        Serial.print("IP-Adresse: ");
        Serial.println(WiFi.localIP().toString());
    }
    
    // Webserver Routen binden
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
    server.begin(); 
    
    // ArduinoOTA für drahtlose Updates starten
    ArduinoOTA.setHostname(device_hostname.c_str()); 
    ArduinoOTA.begin();
    Serial.println("[System] Bereit fuer OTA Updates und Weboberflaeche.");
}


void loop() {
    if (WiFi.getMode() == WIFI_MODE_AP) {
        dnsServer.processNextRequest();
    }
    server.handleClient(); 
    ArduinoOTA.handle();

    bool isPortalActive = (WiFi.getMode() == WIFI_MODE_AP || gateway_mac_str == "00:00:00:00:00:00");

    if (!isPortalActive && (millis() - lastMeasurementTime > 60000)) { 
        lastMeasurementTime = millis(); 
        float backgroundValues[5] = {0.0f}; 
        readModbusSensor(backgroundValues); 
    }
    delay(10);
}
