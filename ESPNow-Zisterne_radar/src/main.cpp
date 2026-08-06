// src/main.cpp
#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <HardwareSerial.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ModbusMaster.h>

// Eigene Module einbinden
#include "structures.h"
#include "modbus_radar.h"
#include "web_portal.h"

// --- CONFIG DEFAULTS (Zwei Blöcke wie gewünscht!) ---
//#define DEFAULT_SSID "SSID"
//#define DEFAULT_PASS "PASSWORD"
//#define DEFAULT_GATEWAY "00:00:00:00:00:00"
//#define DEFAULT_HOSTNAME "espnow_sensor"

#define DEFAULT_SSID "SaHiCo2"
#define DEFAULT_PASS "!SicherHeit12"
#define DEFAULT_GATEWAY "90:70:69:33:73:F4"
#define DEFAULT_HOSTNAME "zisterne_radar"

#define DEFAULT_SLEEP 30 
#define RETRIES 5 
#define DEFAULT_ADC_PIN 34
#define DEFAULT_ADC_FACTOR 2.0f
#define TCP_PORT 8888       // -> Dein zentrales Wunsch-Define für den Netzwerk-Port!

#define SENSOR_TYPE 2       
#define JUMPER_PIN 13       
#define SEN_POWER_PIN 4

const uint8_t channels[] = {6, 1, 11}; 
const uint8_t numChannels = 3;

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

// Verwende das eingetragene Define für den Server
WiFiServer tcpServer(TCP_PORT);
WiFiClient tcpClient;

// --- PROTOTYPEN / VORAB-DEKLARATIONEN FÜR DEN COMPILER ---
void parseMacAddress(String macStr, uint8_t *macArray) {
    unsigned int m[6];
    if (sscanf(macStr.c_str(), "%x:%x:%x:%x:%x:%x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) == 6) {
        for (int i = 0; i < 6; ++i) macArray[i] = (uint8_t)m[i];
    }
}

void setWifiChannel(uint8_t channel) {
    esp_wifi_set_promiscuous(true); esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE); esp_wifi_set_promiscuous(false);
}

void sendOtaConfirmation() {
    // Wir setzen im RAM-Speicher einfach nur den OTA-Status auf 1
    myData.ota_state = 1;
    myData.jumper = (digitalRead(JUMPER_PIN) == LOW) ? 1 : 0;
    
    // Batteriespannung kurz aktualisieren (ohne Modbus-Stress)
    int rawAnalog = analogRead(adc_pin); 
    myData.battery_voltage = ((rawAnalog / 4096.0f) * 3.3f) * adc_factor;

    // Header befüllen
    outPacket.sensor_type = SENSOR_TYPE; 
    outPacket.firmware_ver = FIRMWARE_VERSION;
    memcpy(outPacket.payload, &myData, sizeof(myData));

    // Paket blitzschnell auf den bekannten Kanälen abfeuern
    for (uint8_t c = 0; c < numChannels; c++) {
        uint8_t targetChannel = channels[c]; 
        setWifiChannel(targetChannel);
        
        esp_now_peer_info_t peerInfo = {}; 
        memcpy(peerInfo.peer_addr, gatewayMac, 6);
        peerInfo.channel = targetChannel; 
        peerInfo.encrypt = false;
        
        if (esp_now_is_peer_exist(gatewayMac)) esp_now_del_peer(gatewayMac);
        esp_now_add_peer(&peerInfo);

        // Ein einzelner, schneller Schuss reicht als Bestätigung völlig aus!
        esp_now_send(gatewayMac, (uint8_t *)&outPacket, 2 + sizeof(myData));
        delay(5); // Kurze Pause für die Hardware-Endstufe
    }
    Serial.println("[Funk] Schnelle OTA-Bestaetigung wurde gesendet!");
}

// --- SYSTEM CALLBACKS ---
void OnDataRecv(const uint8_t * mac_addr, const uint8_t *incoming, int len) {
    char buffer[32] = {0}; memcpy(buffer, incoming, (len < 31) ? len : 31);
    String msg = String(buffer); msg.trim();
    if (msg.equalsIgnoreCase("ACK")) ackReceived = true;
    else if (msg.equalsIgnoreCase("OTA=1")) {
        ackReceived = true; 
        stayAwakeForOTA = true; 
        sendOtaConfirmation(); 
        Serial.println("OTA=1 Befehl erhalten!");

    }
    else if (msg.equalsIgnoreCase("OTA=0")) { 
        ackReceived = true; 
        stayAwakeForOTA = false; 
        Serial.println("OTA=0 Befehl erhalten!");
    }
    else if (msg.startsWith("SLEEP=")) {
        ackReceived = true; int parsedVal = msg.substring(6).toInt();
        Serial.println("SLEEP=" + String(sleepTimeSeconds) + " Befehl erhalten!");
        if (parsedVal >= 10 && parsedVal < 86400) {
            sleepTimeSeconds = parsedVal;
            if (preferences.begin("sensor_cfg", false)) { 
                preferences.putUInt("sleep_sec", sleepTimeSeconds); 
                preferences.end(); 
            }
        } else {
            Serial.println("[Warnung] Ungültiger SLEEP-Wert empfangen: " + String(parsedVal));
        }

    }
}

void runMeasurementCycle() {
    ackReceived = false; 
    float sensorValues[5] = {0.0f};
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

    outPacket.sensor_type = SENSOR_TYPE; outPacket.firmware_ver = FIRMWARE_VERSION;
    //memcpy(outPacket.payload, &myData, sizeof(myData));
    memcpy(&outPacket.payload[0], &myData, sizeof(myData));
    
    for (uint8_t c = 0; c < numChannels; c++) {
        uint8_t targetChannel = channels[c]; setWifiChannel(targetChannel);
        esp_now_peer_info_t peerInfo = {}; memcpy(peerInfo.peer_addr, gatewayMac, 6);
        peerInfo.channel = targetChannel; peerInfo.encrypt = false;
        if (esp_now_is_peer_exist(gatewayMac)) esp_now_del_peer(gatewayMac);
        esp_now_add_peer(&peerInfo);

        for (int retry = 1; retry <= RETRIES; retry++) {
            Serial.printf("Sende Daten an Gateway %s auf Kanal %d (Versuch %d/%d)...\n", gateway_mac_str.c_str(), targetChannel, retry, RETRIES);
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

void setup() {
    Serial.begin(115200); pinMode(JUMPER_PIN, INPUT_PULLUP); pinMode(SEN_POWER_PIN, OUTPUT);
    digitalWrite(SEN_POWER_PIN, LOW);

    if (!preferences.begin("sensor_cfg", true)) {
        preferences.begin("sensor_cfg", false); preferences.end(); preferences.begin("sensor_cfg", true);
    }
    wifi_ssid = preferences.getString("wifi_ssid", DEFAULT_SSID); wifi_pass = preferences.getString("wifi_pass", DEFAULT_PASS);
    gateway_mac_str = preferences.getString("gateway_mac", DEFAULT_GATEWAY); device_hostname = preferences.getString("dev_hostname", DEFAULT_HOSTNAME);
    sleepTimeSeconds = preferences.getUInt("sleep_sec", DEFAULT_SLEEP); 
    adc_pin = preferences.getUChar("adc_pin", DEFAULT_ADC_PIN); adc_factor = preferences.getFloat("adc_factor", DEFAULT_ADC_FACTOR);
    preferences.end();

    parseMacAddress(gateway_mac_str, gatewayMac);
    
    SensorSerial.begin(115200, SERIAL_8N1, 16, 17);
    node.begin(1, SensorSerial);

    bool isFactoryReset = (gateway_mac_str == "00:00:00:00:00:00" || wifi_ssid == "SSID" || wifi_ssid == "KEIN_WLAN");

    if (!isFactoryReset) {
        WiFi.mode(WIFI_STA);
        if (esp_now_init() == ESP_OK) { esp_now_register_recv_cb((esp_now_recv_cb_t)OnDataRecv); runMeasurementCycle(); }
        if (digitalRead(JUMPER_PIN) == HIGH && !stayAwakeForOTA) enterDeepSleep(); 
    }

    Serial.println("\nWachmodus aktiv!"); esp_now_deinit(); 
    String ap_name = device_hostname + "_ap"; WiFi.setHostname(device_hostname.c_str()); WiFi.begin(wifi_ssid.c_str(), wifi_pass.c_str());
    uint32_t wifiTimeout = millis(); while (WiFi.status() != WL_CONNECTED && millis() - wifiTimeout < 10000) { delay(500); Serial.print("."); }

    if (WiFi.status() != WL_CONNECTED) {
        WiFi.disconnect(true, true); delay(500); WiFi.mode(WIFI_AP); String ap_pass = ap_name; while (ap_pass.length() < 8) ap_pass += "1"; 
        if (WiFi.softAP(ap_name.c_str(), ap_pass.c_str())) dnsServer.start(53, "*", WiFi.softAPIP());
    } else {
        Serial.println("\n[WLAN] Verbunden mit Router! IP: " + WiFi.localIP().toString());
    }

    setupWebserver(); 
    server.begin(); 
    ArduinoOTA.setHostname(device_hostname.c_str()); ArduinoOTA.begin();

    tcpServer.begin();
    Serial.printf("[System] WLAN-Serial-Extender lauscht auf Port %d.\n", TCP_PORT);
}

void loop() {
    if (WiFi.getMode() == WIFI_MODE_AP) dnsServer.processNextRequest();
    server.handleClient(); 
    ArduinoOTA.handle();

    // 1. Prüfen, ob ein PC eine Verbindung aufbauen möchte
    if (!tcpClient || !tcpClient.connected()) {
        tcpClient = tcpServer.available(); 
        if (tcpClient) {
            Serial.println("\n[Extender] >>> PC erfolgreich verbunden! Schalte Radar auf DAUERSTROM. <<<");
            // ZWECKS NETZWERK-BRIDGE: Sensor dauerhaft einschalten, damit er für den PC antwortet!
            digitalWrite(SEN_POWER_PIN, HIGH); 
        }
    }

    // 2. Wenn der PC verbunden ist: Daten 1:1 durchreichen
    bool isBridgeBusy = false;
    if (tcpClient && tcpClient.connected()) {
        isBridgeBusy = true; // Blockiert die automatische Hintergrundmessung fest!
        
        // Daten vom PC (WLAN) -> an den Radar-Sensor (UART)
        while (tcpClient.available()) { 
            SensorSerial.write(tcpClient.read()); 
        }
        // Daten vom Radar-Sensor (UART) -> zurück an den PC (WLAN)
        while (SensorSerial.available()) { 
            tcpClient.write(SensorSerial.read()); 
        }
    }

    // 3. Wenn der PC die Verbindung trennt: Strom wieder sparen
    static bool lastBridgeState = false;
    if (!isBridgeBusy && lastBridgeState) {
        Serial.println("\n[Extender] PC getrennt. Schalte Radar-Strom wieder aus.");
        digitalWrite(SEN_POWER_PIN, LOW);
    }
    lastBridgeState = isBridgeBusy;

    // 4. Hintergrund-Messung NUR wenn das Portal inaktiv UND kein PC verbunden ist
    bool isPortalActive = (WiFi.getMode() == WIFI_MODE_AP || gateway_mac_str == "00:00:00:00:00:00");

    // FIX: lastMeasurementTime wird jetzt absolut sauber zurückgesetzt
    if (!isPortalActive && !isBridgeBusy) {
        if (millis() - lastMeasurementTime > 60000) { 
            lastMeasurementTime = millis(); 
            Serial.println("[System] Starte automatische Intervall-Hintergrundmessung...");
            float backgroundValues[5] = {0.0f}; 
            readModbusSensor(backgroundValues); 
        }
    } else {
        // Hält den Timer im Labor aktuell, damit er nicht sofort losschießt, wenn die Bridge frei wird
        lastMeasurementTime = millis(); 
    }
    
    delay(1); 
}
