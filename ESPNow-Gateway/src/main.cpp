#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h> // Zwingend erforderlich für die Kanal- und Promiscuous-Steuerung
#include <ArduinoJson.h>

// Universelle Paketstruktur (TLV-Format für die Zukunft)
#define MAX_PAYLOAD_SIZE 240
typedef struct struct_universal_message {
    uint8_t sensor_type;     // 1 = Abstand, 2 = Präsenz, etc.
    uint8_t firmware_ver;    // Firmware Version
    uint8_t payload[MAX_PAYLOAD_SIZE];
} struct_universal_message;

struct_universal_message incomingUniversalData;

// Globale Variablen für die Befehlswarteschlange (Queue)
String pendingCommand = "ACK"; // Standardmäßig antworten wir nur mit ACK
String pendingMac = "";

// Callback-Funktion: Wird ausgeführt, wenn ein Client per ESPNow sendet
void OnDataRecv(const uint8_t * mac_addr, const uint8_t *incoming, int len) {
    memcpy(&incomingUniversalData, incoming, sizeof(incomingUniversalData));

    // MAC-Adresse für den Abgleich formatieren
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x", 
             mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);

    // 1. Peer registrieren (falls nötig für den Rückkanal)
    if (!esp_now_is_peer_exist(mac_addr)) {
        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, mac_addr, 6);
        peerInfo.channel = 6;
        peerInfo.encrypt = false;
        esp_now_add_peer(&peerInfo);
    }

    // 2. Prüfen, ob für diese MAC ein Befehl in der Warteschlange liegt
    String responseMsg = "ACK";
    if (pendingMac.equalsIgnoreCase(String(macStr))) {
        responseMsg = pendingCommand; // Sende z.B. "OTA=1" statt nur "ACK"
        pendingCommand = "ACK";       // Befehl verbraucht, zurücksetzen
        pendingMac = "";
    }

    // Antwort (ACK oder Befehl) exakt in der Millisekunde zurückfunken
    esp_now_send(mac_addr, (uint8_t *) responseMsg.c_str(), responseMsg.length());

    // 3. Das JSON-Paket atomar in einem festen Puffer zusammenbauen (Interrupt-sicher)
    char jsonBuffer[512];
    int pos = 0;
    pos += snprintf(jsonBuffer + pos, sizeof(jsonBuffer) - pos,
                    "{\"type\":\"data\",\"mac\":\"%s\",\"sensor_id\":%d,\"fw\":%d,\"raw\":[",
                    macStr, incomingUniversalData.sensor_type, incomingUniversalData.firmware_ver);

    int payload_len = len - 2;
    for (int i = 0; i < payload_len; i++) {
        pos += snprintf(jsonBuffer + pos, sizeof(jsonBuffer) - pos, "%d", incomingUniversalData.payload[i]);
        if (i < payload_len - 1) pos += snprintf(jsonBuffer + pos, sizeof(jsonBuffer) - pos, ",");
    }
    snprintf(jsonBuffer + pos, sizeof(jsonBuffer) - pos, "]}");
    
    // 4. Abschicken über das USB-Kabel an den Ubuntu Server
    Serial.println(jsonBuffer);
}

// Funktion für den Rückkanal (Downlink): Liest Befehle von Python über USB
void checkSerialCommands() {
    if (Serial.available() > 0) {
        String input = Serial.readStringUntil('\n');
        input.trim();
        if (input.length() == 0) return;

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, input);
        if (error) return;

        const char* type = doc["type"];
        if (type && strcmp(type, "cmd") == 0) {
            const char* macTarget = doc["mac"];
            const char* payload = doc["payload"];

            // Befehl in die Queue legen
            pendingMac = String(macTarget);
            pendingCommand = String(payload);

            // Bestätige Python den Erhalt in der Queue
            Serial.print("{\"type\":\"ack\",\"mac\":\"");
            Serial.print(macTarget);
            Serial.print("\",\"cmd\":\"queue\",\"status\":1}\n");
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(500); 

    // WLAN-Hardware-Chip initialisieren (Fehlerfrei für den S3 Compiler)
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    
    // Nur den Funkchip für ESP-NOW vorbereiten (ohne Router-WLAN)
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    
    // Promiscuous-Mode aktivieren und fest auf Kanal 6 binden
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(6, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    // ESPNow initialisieren
    if (esp_now_init() != ESP_OK) {
        Serial.println("{\"type\":\"log\",\"level\":\"ERROR\",\"msg\":\"ESPNow Init fehlgeschlagen!\"}");
        return;
    }

    // Empfangs-Callback registrieren
    esp_now_register_recv_cb((esp_now_recv_cb_t)OnDataRecv);
    
    Serial.println("{\"type\":\"log\",\"level\":\"INFO\",\"msg\":\"ESPNow Gateway erfolgreich gestartet!\"}");
}

void loop() {
    checkSerialCommands();
    delay(10);
}
