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

// Eigene, neu strukturierte Module einbinden
#include "structures.h"
#include "modbus_radar.h"
#include "web_portal.h"

// --- CONFIG DEFAULTS ---
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
#define TCP_PORT 8888       

#define JUMPER_PIN 13       
#define SEN_POWER_PIN 4

// AUTOMATISCHE HARDWARE-WEICHE FÜR DEN UNIFIED SENSOR TYPE
#if defined(IS_ULTRASCHALL)
  #define SENSOR_TYPE 1
#elif defined(IS_RADAR)
  #define SENSOR_TYPE 2
#else
  #error "SENSOR_TYPE konnte nicht zugewiesen werden! Pruefe platformio.ini."
#endif

const uint8_t channels[] = {6, 1, 11}; 
const uint8_t numChannels = 3;

// Globale Objekte und Infrastruktur-Variablen
HardwareSerial SensorSerial(1); 
ModbusMaster node;
struct_sensor_payload myData;
struct_universal_message outPacket;
volatile bool ackReceived = false;
bool stayAwakeForOTA = false;
uint32_t lastMeasurementTime = 0;
RTC_DATA_ATTR float dynamicRssi = -99.0f;

String wifi_ssid, wifi_pass, gateway_mac_str, device_hostname;
uint8_t gatewayMac[6]; 
uint32_t sleepTimeSeconds;
uint8_t adc_pin;
float adc_factor;

Preferences preferences; WebServer server(80); DNSServer dnsServer;

WiFiServer tcpServer(TCP_PORT);
WiFiClient tcpClient;

// --- HILFSFUNKTIONEN ---
void parseMacAddress(String macStr, uint8_t *macArray) {
    unsigned int m[6];
    if (sscanf(macStr.c_str(), "%x:%x:%x:%x:%x:%x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) == 6) {
        for (int i = 0; i < 6; ++i) macArray[i] = (uint8_t)m[i];
    }
}

void setWifiChannel(uint8_t channel) {
    esp_wifi_set_promiscuous(true); esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE); esp_wifi_set_promiscuous(false);
}

// =========================================================================
// V3.2 UNIVERSELLE EXPRESS-RÜCKMELDUNG (STROM- UND ZEITSPAREND!)
// =========================================================================
void sendExpressConfirmation() {

    myData.ok = 1; 

    // Die neuen Infrastruktur-Werte live einpflegen
    myData.sleep_seconds = sleepTimeSeconds; 
    myData.jumper = (digitalRead(JUMPER_PIN) == LOW) ? 1 : 0;
    myData.ota_state = stayAwakeForOTA ? 1 : 0;
    
    int rawAnalog = analogRead(adc_pin); 
    myData.battery_voltage = ((rawAnalog / 4096.0f) * 3.3f) * adc_factor;

    myData.last_rssi = dynamicRssi;

    // Das Paket wie gewohnt packen
    outPacket.sensor_type = SENSOR_TYPE; 
    outPacket.firmware_ver = FIRMWARE_VERSION;
    memcpy(outPacket.payload, &myData, sizeof(myData));

    for (uint8_t c = 0; c < numChannels; c++) {
        uint8_t targetChannel = channels[c]; 
        setWifiChannel(targetChannel);
        
        esp_now_peer_info_t peerInfo = {}; 
        memcpy(peerInfo.peer_addr, gatewayMac, 6);
        peerInfo.channel = targetChannel; 
        
        if (esp_now_is_peer_exist(gatewayMac)) esp_now_del_peer(gatewayMac);
        esp_now_add_peer(&peerInfo);

        esp_now_send(gatewayMac, (uint8_t *)&outPacket, 2 + sizeof(myData));
        delay(10); 
    }
    delay(100); 
    Serial.println("[Funk] Universelle Express-Bestaetigung (mit Werten & OK=1) wurde gesendet!");
}

// --- ESP-NOW EMPFANGS CALLBACK ---
void OnDataRecv(const uint8_t * mac_addr, const uint8_t *incoming, int len) {
    char buffer[32] = {0}; 
    memcpy(buffer, incoming, (len < 31) ? len : 31);
    String msg = String(buffer); 
    msg.trim();
    
    int8_t currentRssi = -95;
    #if defined(ARDUINO_ARCH_ESP32)
        // Nutzt die interne WiFi-Struktur von Espressif, um den RSSI des letzten Pakets abzugreifen
        currentRssi = WiFi.RSSI(); 
    #endif

    if (currentRssi < 0 && currentRssi > -120) {
        dynamicRssi = currentRssi; // RAM-Puffer fuer das naechste Senden aktualisieren
    }

    Serial.printf("\n[Funk-Inbound] Paket empfangen! Inhalt: '%s' (Signal: %d dBm)\n", msg.c_str(), dynamicRssi);

    if (msg.equalsIgnoreCase("ACK")) {
        ackReceived = true;
//        Serial.println("  -> Status: Gateway hat Datenpaket quittiert (ACK).");
    }
    else if (msg.equalsIgnoreCase("OTA=1")) { 
        ackReceived = true; 
        stayAwakeForOTA = true; 
        Serial.println("  -> Kommando erkannt: OTA-Modus AKTIVIERT. Bleibe dauerhaft wach!");
//        sendExpressConfirmation(); // Zündet Express-Rückkanal
    }
    else if (msg.equalsIgnoreCase("OTA=0")) { 
        ackReceived = true; 
        stayAwakeForOTA = false; 
        Serial.println("  -> Kommando erkannt: OTA-Modus DEAKTIVIERT. Gehe wieder schlafen.");
//        sendExpressConfirmation(); // Zündet Express-Rückkanal
    }
    else if (msg.startsWith("SLEEP=")) {
        ackReceived = true; 
        int parsedVal = msg.substring(6).toInt();
        Serial.printf("  -> Kommando erkannt: Neue Schlafzeit angefordert: %d Sekunden.\n", parsedVal);
        
        if (parsedVal >= 10 && parsedVal < 86400) {

            sleepTimeSeconds = parsedVal;
            
            preferences.end(); // Vorherige Lese-Verbindungen sicherheitshalber kappen
            if (preferences.begin("sensor_cfg", false)) { 
                preferences.putUInt("sleep_sec", sleepTimeSeconds); 
                preferences.end(); 
                Serial.println("  -> Erfolg: Neue Schlafzeit dauerhaft im NVS-Flash gespeichert!");
            }
            
            myData.sleep_seconds = sleepTimeSeconds;
            
//            sendExpressConfirmation(); 
            
            Serial.printf("  -> Sensor schlaeft gleich fuer %d Sekunden ein.\n", sleepTimeSeconds);
        }
    }
}

// --- REGULÄRER MESS- UND SENDEZYKLUS ---
void runMeasurementCycle() {
    ackReceived = false; 
    
    // Weist das universelle Auslese-Modul an, Daten zu erfassen
    bool measurementOk = readSensorHardware(myData);
    
    myData.ok = measurementOk ? 1 : 0;
    myData.sleep_seconds = sleepTimeSeconds; // Synchronisiert den echten Wert permanent mit dem Server
    myData.jumper = (digitalRead(JUMPER_PIN) == LOW) ? 1 : 0;
    myData.ota_state = stayAwakeForOTA ? 1 : 0;

    int rawAnalog = analogRead(adc_pin); 
    myData.battery_voltage = ((rawAnalog / 4096.0f) * 3.3f) * adc_factor;

    myData.last_rssi = dynamicRssi;

    outPacket.sensor_type = SENSOR_TYPE; 
    outPacket.firmware_ver = FIRMWARE_VERSION;
    memcpy(outPacket.payload, &myData, sizeof(myData));

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
            while (millis() - waitStart < 100) { 
                if (ackReceived) {
                    if (stayAwakeForOTA || myData.sleep_seconds != sleepTimeSeconds) {
                        sendExpressConfirmation();
                    }
                    return; 
                }
                delay(5); 
            }
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
    
    // UART1 initialisieren (wird im Ultraschall-Modus einfach ignoriert)
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

    // TCP-Server fuer Modbus-Extender (nur im Radar-Modus aktiv nutzbar)
    tcpServer.begin();
    Serial.printf("[System] WLAN-Serial-Extender lauscht auf Port %d.\n", TCP_PORT);
}

void loop() {
    if (WiFi.getMode() == WIFI_MODE_AP) dnsServer.processNextRequest();
    server.handleClient(); ArduinoOTA.handle();

    if (!tcpClient || !tcpClient.connected()) {
        tcpClient = tcpServer.available(); 
        if (tcpClient) {
            Serial.println("[Extender] PC verbunden! Schalte Sensor auf DAUERSTROM.");
            digitalWrite(SEN_POWER_PIN, HIGH);
        }
    }

    bool isBridgeBusy = false;
    if (tcpClient && tcpClient.connected()) {
        isBridgeBusy = true;
        while (tcpClient.available()) { SensorSerial.write(tcpClient.read()); }
        while (SensorSerial.available()) { tcpClient.write(SensorSerial.read()); }
    }

    static bool lastBridgeState = false;
    if (!isBridgeBusy && lastBridgeState) {
        Serial.println("[Extender] PC getrennt. Schalte Sensor-Strom aus.");
        digitalWrite(SEN_POWER_PIN, LOW);
    }
    lastBridgeState = isBridgeBusy;

    bool isPortalActive = (WiFi.getMode() == WIFI_MODE_AP || gateway_mac_str == "00:00:00:00:00:00");

    if (!isPortalActive && !isBridgeBusy) {
        if (millis() - lastMeasurementTime > 60000) { 
            lastMeasurementTime = millis(); 
            
            // KORREKTUR: Nutzt jetzt den universellen Funktionsnamen aus der v3.2
            Serial.println("[System] Starte automatische Intervall-Hintergrundmessung...");
            runMeasurementCycle(); 
        }
    } else {
        lastMeasurementTime = millis(); 
    }
    delay(1); 
}
