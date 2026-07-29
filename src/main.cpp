#include <Arduino.h>

#include "serial_protocol.h"
#include "sensor_manager.h"
#include "espnow_manager.h"
#include "wifi_manager.h"
#include "mqtt_manager.h"

void setup()
{
    Serial.begin(115200);
    serialProtocolSetup();
    delay(1000);

    Serial.println();
    Serial.println("--------------------------------");
    Serial.println("ESPNow Gateway V5");
    Serial.println("--------------------------------");

    wifiSetup();

    mqttSetup();

    sensorManagerInit();

    espnowSetup();
}

void loop()
{
    wifiLoop();

    mqttLoop();

    espnowLoop();

    serialProtocolLoop();
}