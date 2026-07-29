#include "wifi_manager.h"

#include <WiFi.h>

#include "config.h"

static bool connected = false;

void wifiSetup()
{
    WiFi.mode(WIFI_STA);

    WiFi.setHostname(HOSTNAME);

    WiFi.begin(
        WIFI_SSID,
        WIFI_PASSWORD,
        WIFI_CHANNEL);

    Serial.print("Verbinde WLAN");

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    connected = true;

    Serial.println();
    Serial.println("WLAN verbunden über Kanal " + String(WiFi.channel()));
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("MAC: ");
    Serial.println(WiFi.macAddress());
}

void wifiLoop()
{
    if (WiFi.status() == WL_CONNECTED)
        return;

    if (connected)
    {
        Serial.println("WLAN verloren");
        connected = false;
    }

    WiFi.reconnect();
}