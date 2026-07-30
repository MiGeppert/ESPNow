#include <Arduino.h>

#include "command_manager.h"
#include "mqtt_manager.h"

#include <WiFi.h>
#include <PubSubClient.h>

#include "config.h"

WiFiClient wifi;

PubSubClient mqtt(wifi);

static unsigned long lastReconnect = 0;

void mqttCallback(
    char *topic,
    byte *payload,
    unsigned int length)
{
    String msg;

    for (unsigned int i = 0; i < length; i++)
        msg += (char)payload[i];

    processCommand(
        String(topic),
        msg);
}

void mqttSetup()
{

    mqtt.setServer(
        MQTT_SERVER,
        MQTT_PORT);

    mqtt.setCallback(
        mqttCallback);

    mqtt.setBufferSize(1024);

}

bool mqttConnected()
{
    return mqtt.connected();
}

bool mqttPublish(
    const String &topic,
    const String &payload,
    bool retain)
{
    if (!mqtt.connected())
    {
        Serial.println("MQTT nicht verbunden");
        return false;
    }

    bool ok = mqtt.publish(
        topic.c_str(),
        payload.c_str(),
        retain);

    Serial.printf(
        "[%s] %s\n",
        ok ? "MQTT" : "FAIL",
        topic.c_str());

    if (!ok)
    {
        Serial.printf(
            "Payloadlaenge: %u\n",
            payload.length());

        Serial.println(payload);
    }

    return ok;
}
void mqttLoop()
{
    if (!mqtt.connected())
    {
        if (millis() - lastReconnect > 5000)
        {
            lastReconnect = millis();

            Serial.print("MQTT verbinden... ");

            if (mqtt.connect(
                    HOSTNAME,
                    MQTT_USER,
                    MQTT_PASSWORD))
            {
                Serial.println("OK");

                mqtt.subscribe("espnow/+/cmd");

                Serial.println("MQTT Subscribe: espnow/+/cmd");

                mqtt.publish(
                    "espnow-gateway/status",
                    "online",
                    true);
            }
            else
            {
                Serial.printf(
                    "Fehler %d\n",
                    mqtt.state());
            }
        }
    }

    mqtt.loop();
}