#include "sensor_manager.h"
#include "espnow_manager.h"

#include "command_manager.h"

#include <esp_now.h>

void commandManagerInit()
{
}

void processCommand(
    const String &topic,
    const String &payload)
{
    Serial.println();
    Serial.println("========== MQTT COMMAND ==========");
    Serial.println(topic);
    Serial.println(payload);

    //------------------------------------------------------
    // Sensor-ID aus Topic holen
    //------------------------------------------------------

    // espnow/ZISTERNE/cmd

    int p1 = topic.indexOf('/');
    int p2 = topic.lastIndexOf('/');

    if (p1 < 0 || p2 < 0 || p2 <= p1)
        return;

    String sensorId =
        topic.substring(
            p1 + 1,
            p2);

    Serial.print("Sensor : ");
    Serial.println(sensorId);

    //------------------------------------------------------
    // TODO:
    // MAC-Adresse über sensor_manager suchen
    // anschließend ESPNow senden
    //------------------------------------------------------

SensorInfo *sensor =
    findSensor(sensorId);

if (sensor == nullptr)
{
    Serial.println("Sensor unbekannt");

    return;
}

sensor->pendingCommand = payload;
/* espnowSend(
    sensor->mac,
    payload); */

}