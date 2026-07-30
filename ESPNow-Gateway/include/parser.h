#pragma once

#include <Arduino.h>

struct SensorInfo; 

SensorInfo* parseMessage(
    const String &payload,
    const uint8_t *mac,
    int rssi);