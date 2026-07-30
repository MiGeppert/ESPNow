#include "espnow_manager.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

#include "parser.h"
#include "sensor_manager.h"
#include "config.h"

static int lastRSSI = 0;

// Hilfsfunktion: Trägt den Sensor fest in die ESP32-Hardware ein
void registerPeerIfNeeded(const uint8_t *mac) {
    // Wenn der Chip den Peer schon kennt, müssen wir nichts tun
    if (esp_now_is_peer_exist(mac)) {
        return; 
    }

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, mac, 6);
    
    // WICHTIG: Hier muss exakt der WLAN-Kanal deines Gateways/Routers stehen!
    peerInfo.channel = WIFI_CHANNEL; 
    peerInfo.ifidx = WIFI_IF_STA; // Da das Gateway im Station-Modus am Router hängt
    peerInfo.encrypt = false;

    esp_err_t err = esp_now_add_peer(&peerInfo);
    if (err == ESP_OK) {
        Serial.println("Sensor erfolgreich in ESP32-Hardware-Peerliste eingetragen.");
    } else {
        Serial.printf("Fehler beim Hardware-Eintrag des Peers: %d\n", err);
    }
}


static void onReceive(
    const uint8_t *mac,
    const uint8_t *data,
    int len)
{
    char message[250];

    if (len >= sizeof(message))
        len = sizeof(message) - 1;

    memcpy(message, data, len);

    message[len] = 0;

    Serial.println();
    Serial.println("----------------------------");
    Serial.println("ESP-NOW Paket");
    Serial.println(message);
    Serial.println("----------------------------");

    SensorInfo *sensor =parseMessage(
        String(message),
        mac,
        lastRSSI);

    if (sensor != nullptr)
    {
        registerPeerIfNeeded(mac);

        delay(15); // Kurze Pause, damit der Sensor empfangsbereit ist

        if (sensor->pendingCommand.length() > 0)
        {
            Serial.printf("Wartendes HA-Kommando gesendet: %s\n", sensor->pendingCommand.c_str());
            espnowSend(sensor->mac, sensor->pendingCommand);
            sensor->pendingCommand = ""; // Speicher leeren
        }
        else
        {
            // Fallback für ESPHome, damit kein Timeout-Fehler kommt
            espnowSend(sensor->mac, "ACK");
        }
    }

}

void espnowSetup()
{
    WiFi.mode(WIFI_STA);

    if (esp_now_init() != ESP_OK)
    {
        Serial.println("ESP-NOW Init FEHLER");

        while (true)
            delay(1000);
    }

    esp_now_register_recv_cb(onReceive);

    Serial.println("ESP-NOW gestartet");
}

void espnowLoop()
{
    // derzeit nichts erforderlich
}

bool espnowSend(
    const uint8_t *mac,
    const String &message)
{
    esp_err_t result =
        esp_now_send(
            mac,
            (const uint8_t *)message.c_str(),
            message.length() + 1);

    if (result == ESP_OK)
    {
        Serial.println();

        Serial.print("ESPNow SEND ");

        Serial.println(message);

        return true;
    }

    Serial.println();

    Serial.print("ESPNow SEND Fehler: ");

    Serial.println(result);

    return false;
}