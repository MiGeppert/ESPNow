#pragma once

#include <Arduino.h>
#include <vector>

#define MAX_SENSORS 20

struct SensorInfo
{
    String id;

    uint8_t mac[6];

    String pendingCommand;

    int rssi;

    unsigned long lastSeen;

    std::vector<String> knownKeys;
};

void sensorManagerInit();

SensorInfo *findSensor(
    const String &id);

SensorInfo *addSensor(
    const String &id,
    const uint8_t *mac,
    int rssi);

SensorInfo *updateSensor(
    const String &id,
    const uint8_t *mac,
    int rssi);

bool keyKnown(
    SensorInfo *sensor,
    const String &key);

void addKnownKey(
    SensorInfo *sensor,
    const String &key);

void printSensors();