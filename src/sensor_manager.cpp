#include "sensor_manager.h"

static SensorInfo sensors[MAX_SENSORS];

static uint8_t sensorCount = 0;

void sensorManagerInit()
{
    sensorCount = 0;
}

SensorInfo *findSensor(
    const String &id)
{
    for (uint8_t i = 0; i < sensorCount; i++)
    {
        if (sensors[i].id == id)
            return &sensors[i];
    }

    return nullptr;
}

SensorInfo *addSensor(
    const String &id,
    const uint8_t *mac,
    int rssi)
{
    if (sensorCount >= MAX_SENSORS)
        return nullptr;

    SensorInfo *s =
        &sensors[sensorCount++];

    s->id = id;

    memcpy(
        s->mac,
        mac,
        6);

    s->rssi = rssi;

    s->lastSeen = millis();

    Serial.println();

    Serial.print("Neuer Sensor: ");

    Serial.println(id);

    return s;
}

SensorInfo *updateSensor(
    const String &id,
    const uint8_t *mac,
    int rssi)
{
    SensorInfo *s =
        findSensor(id);

    if (s == nullptr)
        s = addSensor(
            id,
            mac,
            rssi);

    if (s == nullptr)
        return nullptr;

    memcpy(
        s->mac,
        mac,
        6);

    s->rssi = rssi;

    s->lastSeen = millis();

    return s;
}

bool keyKnown(
    SensorInfo *sensor,
    const String &key)
{
    for (auto &k : sensor->knownKeys)
    {
        if (k == key)
            return true;
    }

    return false;
}

void addKnownKey(
    SensorInfo *sensor,
    const String &key)
{
    if (!keyKnown(sensor, key))
        sensor->knownKeys.push_back(key);
}

void printSensors()
{
    Serial.println();

    Serial.println("========== Sensorliste ==========");

    for (uint8_t i = 0; i < sensorCount; i++)
    {
        SensorInfo *s = &sensors[i];

        Serial.printf(
            "%2d %-14s RSSI=%3d %02X:%02X:%02X:%02X:%02X:%02X\n",
            i + 1,
            s->id.c_str(),
            s->rssi,
            s->mac[0],
            s->mac[1],
            s->mac[2],
            s->mac[3],
            s->mac[4],
            s->mac[5]);
    }

    Serial.println("================================");
}