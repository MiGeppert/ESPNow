#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <HardwareSerial.h>
#include <ArduinoOTA.h>
#include <Preferences.h> // Ermöglicht das dauerhafte Speichern der Schlafzeit im Flash-Speicher

// --- KONFIGURATION ---
uint8_t gatewayMac[] = {0x90, 0x70, 0x69, 0x33, 0x73, 0xF4}; // Echte MAC deines Gateways
#define FIRMWARE_VERSION 25 
#define SENSOR_TYPE 1       
#define JUMPER_PIN 13       
#define US_POWER_PIN 4      

// Die 3 überlappungsfreien Hauptkanäle im 2.4GHz Band für die Notfall-Suche
const uint8_t channels[] = {6, 1, 11}; 
const uint8_t numChannels = 3;

// --- STRUKTUREN ---
typedef struct __attribute__((__packed__)) struct_distance {
    float distance;  
    uint8_t ok;      
    uint8_t jumper;  
    uint8_t ota_state; 
} struct_distance;

typedef struct __attribute__((__packed__)) struct_universal_message {
    uint8_t sensor_type;
    uint8_t firmware_ver;
    uint8_t payload[32];
} struct_universal_message;

// --- GLOBALE VARIABLEN ---
HardwareSerial JSNSerial(1); 
struct_distance myData;
struct_universal_message outPacket;
volatile bool ackReceived = false;
bool stayAwakeForOTA = false;

uint32_t sleepTimeSeconds = 900; // Standard-Fallback (15 Min), falls der Flash leer sein sollte
Preferences preferences;          // Speicher-Objekt für den NVS-Flash-Speicher

// Callback wenn das Gateway antwortet
void OnDataRecv(const uint8_t * mac_addr, const uint8_t *incoming, int len) {
    String msg = String((char*)incoming).substring(0, len);
    msg.trim();

    if (msg.equalsIgnoreCase("ACK")) {
        ackReceived = true; // Einfaches ACK, wir nutzen die geladene Zeit weiter
    } 
    else if (msg.equalsIgnoreCase("OTA=1")) {
        ackReceived = true;
        stayAwakeForOTA = true;
    } 
    else if (msg.equalsIgnoreCase("OTA=0")) {
        ackReceived = true;
        stayAwakeForOTA = false;
    }
    // Empfängt den variablen Sekundenwert vom Server (z.B. "SLEEP=20")
    else if (msg.startsWith("SLEEP=")) {
        ackReceived = true;
        String valStr = msg.substring(6); 
        int parsedVal = valStr.toInt();
        if (parsedVal >= 10 && parsedVal < 86400) { // Erlaubt 10 Sek. bis 24 Std. Schlafzeit
            sleepTimeSeconds = parsedVal;
            
            // Wert dauerhaft im Flash-Speicher des ESP32 abspeichern
            preferences.begin("zisterne_cfg", false); 
            preferences.putUInt("sleep_sec", sleepTimeSeconds); 
            preferences.end(); 
            
            Serial.printf("[Speicher] Neues Intervall (%d Sek.) im Flash gesichert.\n", sleepTimeSeconds);
        }
    }
}

// Hilfsfunktion zum Wechseln des WLAN-Kanals im laufenden Betrieb
void setWifiChannel(uint8_t channel) {
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);
}

// Liest den JSN-SR04T Ultraschall-Sensor über UART aus
bool readUltrasonicSensor(float &resultDistance) {
    digitalWrite(US_POWER_PIN, HIGH); 
    delay(500); 

    while(JSNSerial.available()) JSNSerial.read();
    JSNSerial.write(0x55);

    uint32_t start_time = millis();
    uint8_t buf[4];
    int bytesRead = 0;

    while ((millis() - start_time < 150) && (bytesRead < 4)) {
        if (JSNSerial.available()) {
            buf[bytesRead++] = JSNSerial.read();
        }
    }
    digitalWrite(US_POWER_PIN, LOW); // Strom sparen!

    if (bytesRead != 4 || buf[0] != 0xFF) return false;
    uint8_t checksum = (buf[0] + buf[1] + buf[2]) & 0xFF;
    if (checksum != buf[3]) return false;

    uint16_t distance_mm = (buf[1] << 8) | buf[2];
    resultDistance = distance_mm / 10.0f;
    return true;
}

// Führt den Sendezyklus inklusive automatischer Kanalsuche durch
void runMeasurementCycle() {
    ackReceived = false;
    float measuredDistance = 0.0f;
    bool measurementOk = readUltrasonicSensor(measuredDistance);

    if (measurementOk) {
        myData.distance = 202.8f - measuredDistance; 
        myData.ok = 1;
    } else {
        myData.distance = 0.0f;
        myData.ok = 0;
    }
    myData.jumper = (digitalRead(JUMPER_PIN) == LOW) ? 1 : 0;
    myData.ota_state = stayAwakeForOTA ? 1 : 0;

    outPacket.sensor_type = SENSOR_TYPE; // Setzt die 1
    outPacket.firmware_ver = FIRMWARE_VERSION; // Setzt die 25

    // Verwende die ermittelte sleepTimeSeconds, um sie an HA zurückzumelden!
    // Wir nutzen das Feld "fw_version" oder erweitern das Paket im Python-Skript.
    memcpy(outPacket.payload, &myData, sizeof(myData));

    // Kanalsuche starten: Wir testen nacheinander 6, 1 und 11
    for (uint8_t c = 0; c < numChannels; c++) {
        uint8_t targetChannel = channels[c];
        
        // 1. WLAN-Hardware-Kanal VOR dem Registrieren des Partners umstellen!
        setWifiChannel(targetChannel);
        delay(10); // Kurze Pause, damit die Antenne sich einschwingt

        // 2. Partner-Struktur absolut sauber für diesen spezifischen Kanal aufbauen
        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, gatewayMac, 6);
        peerInfo.channel = targetChannel; // KORREKTUR: Muss exakt mit dem Hardware-Kanal übereinstimmen!
        peerInfo.encrypt = false;

        // Alten Peer löschen und frisch mit dem neuen Kanal registrieren
        if (esp_now_is_peer_exist(gatewayMac)) {
            esp_now_del_peer(gatewayMac);
        }
        esp_now_add_peer(&peerInfo);

        // 3. Sendeversuche auf diesem Kanal starten
        for (int retry = 1; retry <= 5; retry++) {
            Serial.printf("Sende an Gateway auf Kanal %d (Versuch %d/5)...\n", targetChannel, retry);
            
            esp_err_t result = esp_now_send(gatewayMac, (uint8_t *)&outPacket, 2 + sizeof(myData));
            
            if (result != ESP_OK) {
                Serial.printf("[Fehler] ESP-NOW Senden fehlgeschlagen: %d\n", result);
                continue;
            }
            
            // Warte 100ms auf das Funk-ACK vom Gateway
            uint32_t waitStart = millis();
            while (millis() - waitStart < 100) {
                if (ackReceived) {
                    Serial.println("[Funk] Antwort vom Gateway erfolgreich empfangen.");
                    return; // Paket kam an, Funktion sofort beenden!
                }
                delay(5);
            }
        }
        Serial.printf("Keine Antwort auf Kanal %d. Probiere nächsten Kanal...\n", targetChannel);
    }
    Serial.println("[Funk] Warnung: Gateway auf keinem Kanal erreichbar.");
}

// Berechnet die korrekten Zeiten für das Log und schickt den ESP32 schlafen
void enterDeepSleep() {
    uint32_t minutes = sleepTimeSeconds / 60;
    uint32_t seconds = sleepTimeSeconds % 60;
    
    Serial.println("\n-------------------------------------------------------------------------");
    if (minutes > 0) {
        Serial.printf(">>> [Zisterne] Gehe in den Deep Sleep für %d Min. und %d Sek. (Gesamt: %d Sek.) <<<\n", 
                      minutes, seconds, sleepTimeSeconds);
    } else {
        Serial.printf(">>> [Zisterne] Gehe in den Deep Sleep für %d Sekunden <<<\n", sleepTimeSeconds);
    }
    Serial.println("-------------------------------------------------------------------------\n");
    
    esp_sleep_enable_timer_wakeup((uint64_t)sleepTimeSeconds * 1000000ULL); 
    esp_deep_sleep_start();
}

void setup() {
    Serial.begin(115200);
    JSNSerial.begin(9600, SERIAL_8N1, 16, 17); // Hardware UART für JSN-Sensor
    
    pinMode(JUMPER_PIN, INPUT_PULLUP);
    pinMode(US_POWER_PIN, OUTPUT);

    // BEIM BOOTEN DEN LETZTEN GESPEICHERTEN WERT AUS DEM FLASH LADEN
    preferences.begin("zisterne_cfg", true); // Lese-Modus
    sleepTimeSeconds = preferences.getUInt("sleep_sec", 900); // 900 als Fallback bei leerem Flash
    preferences.end();
    Serial.printf("\n[Speicher] Bootvorgang. Geladenes Schlafintervall: %d Sekunden.\n", sleepTimeSeconds);

    // Initialisiere ESPNow-Netzwerk
    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) enterDeepSleep();
    esp_now_register_recv_cb((esp_now_recv_cb_t)OnDataRecv);

    // Mess- und Sendezyklus ausführen
    runMeasurementCycle();

    // ENTSCHEIDUNG: Dürfen wir schlafen gehen oder blockiert uns Jumper / OTA?
    if (digitalRead(JUMPER_PIN) == HIGH && !stayAwakeForOTA) {
        enterDeepSleep();
    }

    // --- WACHMODUS-SCHUTZ: AKTIVIERE ROUTER-WLAN FÜR DAS VS CODE UPDATE ---
    Serial.println("Sensor bleibt wach! Initialisiere volles Router-WLAN für ArduinoOTA...");
    esp_now_deinit(); 
    
    WiFi.begin("SaHiCo2", "DEIN_WLAN_PASSWORT"); // Trage hier dein echtes WLAN-Passwort ein!
    
    uint32_t wifiTimeout = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - wifiTimeout < 10000) {
        delay(500);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWLAN verbunden! IP-Adresse: " + WiFi.localIP().toString());
        ArduinoOTA.setHostname("zisterne-cplusplus");
        ArduinoOTA.begin();
    }
}

uint32_t lastMeasurementTime = 0;

void loop() {
    if (WiFi.status() == WL_CONNECTED) {
        ArduinoOTA.handle(); // Hält den VS Code Drahtlos-Update-Kanal offen
    }

    // Taktet die Messung bei gestecktem Service-Jumper im 60-Sekunden-Intervall
    if (millis() - lastMeasurementTime > 60000) {
        lastMeasurementTime = millis();
        Serial.println("[Service] 60s Intervall-Messung im Wachmodus gestartet:");
        runMeasurementCycle();
    }
    delay(10);
}
