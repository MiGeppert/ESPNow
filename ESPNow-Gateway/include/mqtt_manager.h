#pragma once

#include <Arduino.h>

void mqttSetup();

void mqttLoop();

bool mqttConnected();

bool mqttPublish(
    const String &topic,
    const String &payload,
    bool retain = true);