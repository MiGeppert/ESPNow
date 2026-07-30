#pragma once

#include <Arduino.h>

void commandManagerInit();

void processCommand(
    const String &topic,
    const String &payload);