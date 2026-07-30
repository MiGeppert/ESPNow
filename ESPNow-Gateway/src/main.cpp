#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <ArduinoJson.h> // Wichtig: In platformio.ini unter lib_deps hinzufügen!

#include <esp_wifi.h>

// 1. Universelle Paketstruktur (TLV-Format für die Zukunft)
#define MAX_PAYLOAD_SIZE 240
typedef struct struct_universal_message {
    uint8_t sensor_type;     // 1 = Abstand, 2 = Präsenz, etc.
    uint8_t firmware_ver;    // Firmware Version
    uint8_t payload[MAX_PAYLOAD_SIZE];
} struct_universal_message;

struct_universal_message incomingUniversalData;

// 2. Callback-Funktion: Wird ausgeführt, wenn ein Client per ESPNow sendet
void OnDataRecv(const uint8_t * mac_addr, const uint8_t *incoming, int len) {
    // 1. Universelle Struktur befüllen (wie gehabt)
    memcpy(&incomingUniversalData, incoming, sizeof(incomingUniversalData));

    // 2. Peer registrieren und ACK senden (Funktioniert!)
    if (!esp_now_is_peer_exist(mac_addr)) {
        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, mac_addr, 6);
        peerInfo.channel = 6;
        peerInfo.encrypt = false;
        esp_now_add_peer(&peerInfo);
    }
    const char* ackMsg = "ACK";
    esp_now_send(mac_addr, (uint8_t *) ackMsg, strlen(ackMsg));

    // 3. Einen festen, sicheren Textpuffer im Speicher reservieren (512 Bytes reichen dicke)
    char jsonBuffer[512];
    int pos = 0;

    // JSON-Anfang formatieren
    pos += snprintf(jsonBuffer + pos, sizeof(jsonBuffer) - pos,
                    "{\"type\":\"data\",\"mac\":\"%02x:%02x:%02x:%02x:%02x:%02x\",\"sensor_id\":%d,\"fw\":%d,\"raw\":[",
                    mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5],
                    incomingUniversalData.sensor_type,
                    incomingUniversalData.firmware_ver);

    // Rohe Bytes der Payload als Zahlenliste in den Puffer schreiben
    int payload_len = len - 2;
    for (int i = 0; i < payload_len; i++) {
        pos += snprintf(jsonBuffer + pos, sizeof(jsonBuffer) - pos, "%d", incomingUniversalData.payload[i]);
        if (i < payload_len - 1) {
            pos += snprintf(jsonBuffer + pos, sizeof(jsonBuffer) - pos, ",");
        }
    }

    // JSON schließen
    snprintf(jsonBuffer + pos, sizeof(jsonBuffer) - pos, "]}");

    // 4. In einer einzigen Zeile ohne Verzögerung über den UART-Chip jagen!
    Serial.println(jsonBuffer);
}

// 3. Funktion für den Rückkanal (Downlink): Liest Befehle von Python über USB
void checkSerialCommands() {
    if (Serial.available() > 0) {
        String input = Serial.readStringUntil('\n');
        input.trim();
        if (input.length() == 0) return;

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, input);
        if (error) return; // Müll ignorieren

        const char* type = doc["type"];
        if (type && strcmp(type, "cmd") == 0) {
            const char* macTarget = doc["mac"];
            const char* cmd = doc["cmd"];
            const char* payload = doc["payload"];

            // MAC-String zurück in Bytes wandeln
            uint8_t targetAddress[6];
            sscanf(macTarget, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx", 
                   &targetAddress[0], &targetAddress[1], &targetAddress[2], 
                   &targetAddress[3], &targetAddress[4], &targetAddress[5]);

            // Befehl per ESPNow an den Sensor senden (Wir senden hier beispielhaft den payload-Text)
            esp_err_t result = esp_now_send(targetAddress, (uint8_t *) payload, strlen(payload));
            
            // Rückmeldung (ACK) an Python senden
            Serial.print("{\"type\":\"ack\",\"mac\":\"");
            Serial.print(macTarget);
            Serial.print("\",\"cmd\":\"");
            Serial.print(cmd);
            Serial.print("\",\"status\":");
            Serial.print(result == ESP_OK ? 1 : 0);
            Serial.println("}");
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(500); 

    // 1. WLAN-Modus setzen
    WiFi.mode(WIFI_STA);
    
    // 2. Promiscuous-Mode KORREKT für den ESP32-S3 aktivieren
    // Wir müssen promiscuous einschalten UND dem Chip sagen, auf welchem Kanal (6) er starten soll.
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(6, WIFI_SECOND_CHAN_NONE);
    
    WiFi.disconnect();

    // 3. ESPNow initialisieren
    if (esp_now_init() != ESP_OK) {
        Serial.println("{\"type\":\"log\",\"level\":\"ERROR\",\"msg\":\"ESPNow Init fehlgeschlagen!\"}");
        return;
    }

    // 4. Empfangs-Callback mit dem korrekten Typ-Cast für Espressif V5/S3 Cores registrieren
    esp_now_register_recv_cb((esp_now_recv_cb_t)OnDataRecv);
    
    // Start-Meldung an Python
    Serial.println("{\"type\":\"log\",\"level\":\"INFO\",\"msg\":\"Gateway-Hauptprogramm erfolgreich gestartet!\"}");
}

void setup_org() {
    Serial.begin(115200);

    // Wartet beim Booten maximal 2 Sekunden, bis der USB-Port des Servers bereit ist
    // Verhindert, dass die ersten Log-Meldungen verschluckt werden
    delay(2000); 

    // WLAN initialisieren
    WiFi.mode(WIFI_STA);
    
    // WICHTIG: Das Gateway MUSS auf denselben Kanal (6) wie der Sensor gezwungen werden!
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(6, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);
    
    WiFi.disconnect();

    // ESPNow initialisieren
    if (esp_now_init() != ESP_OK) {
        Serial.println("{\"type\":\"log\",\"level\":\"ERROR\",\"msg\":\"ESPNow Init Failed\"}");
        return;
    }

    // Empfangs-Callback registrieren
    esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
}

void loop() {
    // Ständig prüfen, ob Befehle von Ubuntu via USB reinkommen
    checkSerialCommands();
    delay(10);
}
