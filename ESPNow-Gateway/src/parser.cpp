#include "parser.h"

#include "sensor_manager.h"
#include "json_builder.h"
#include "mqtt_manager.h"
#include "discovery.h"
#include "serial_protocol.h"

SensorInfo* parseMessage(
    const String &payload,
    const uint8_t *mac,
    int rssi)
{
    //--------------------------------------------------
    // Sensor-ID suchen
    //--------------------------------------------------

    String sensorId;

    JsonBuilder json;

    json.begin();

    int start = 0;

    while (start < payload.length())
    {
        int end = payload.indexOf(';', start);

        String token;

        if (end < 0)
            token = payload.substring(start);
        else
            token = payload.substring(start, end);

        int eq = token.indexOf('=');

        if (eq > 0)
        {
            String key =
                token.substring(0, eq);

            String value =
                token.substring(eq + 1);

            //--------------------------------------------------
            // Sensor-ID
            //--------------------------------------------------

            if (key == "ID")
            {
                sensorId = value;
            }
            else
            {
                json.add(
                    key,
                    value);
            }
        }

        if (end < 0)
            break;

        start = end + 1;
    }

    //--------------------------------------------------
    // Ohne ID keine Verarbeitung
    //--------------------------------------------------

    if (sensorId.isEmpty())
        return nullptr;

    //--------------------------------------------------
    // Sensor aktualisieren
    //--------------------------------------------------

    SensorInfo *sensor =
        updateSensor(
            sensorId,
            mac,
            rssi);

    if (sensor == nullptr)
        return nullptr;

    //--------------------------------------------------
    // JSON erzeugen
    //--------------------------------------------------

    String state = json.end();

    //--------------------------------------------------
    // MQTT (vorläufig)
    //--------------------------------------------------

    mqttPublish(
        "espnow/" +
        sensorId +
        "/state",
        state,
        true);

    //--------------------------------------------------
    // UART (V5)
    //--------------------------------------------------

    serialSendJson(state);

    //--------------------------------------------------

    Serial.println();
    Serial.println("JSON:");
    Serial.println(state);

    //--------------------------------------------------
    // Discovery
    //--------------------------------------------------

    start = 0;

    while (start < payload.length())
    {
        int end = payload.indexOf(';', start);

        String token;

        if (end < 0)
            token = payload.substring(start);
        else
            token = payload.substring(start, end);

        int eq = token.indexOf('=');

        if (eq > 0)
        {
            String key =
                token.substring(0, eq);

            if (key != "ID")
            {

                if (!keyKnown(sensor, key))
                {
                    sendDiscovery(
                        sensorId,
                        key);

                    addKnownKey(
                        sensor,
                        key);
                }
            }
        }

        if (end < 0)
            break;

        start = end + 1;
    }


    //--------------------------------------------------
    // Sensorliste ausgeben
    //--------------------------------------------------

    printSensors();

    return sensor;
}