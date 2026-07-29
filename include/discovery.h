#pragma once

#include <Arduino.h>

enum DiscoveryType
{
    DISC_SENSOR,
    DISC_BINARY_SENSOR,
    DISC_SWITCH
};

void sendDiscovery(
    const String &sensorId,
    const String &key);