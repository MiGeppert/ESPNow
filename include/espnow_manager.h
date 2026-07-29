#pragma once

#include <Arduino.h>

void espnowSetup();

void espnowLoop();

bool espnowSend(
    const uint8_t *mac,
    const String &message);

void registerPeerIfNeeded(
    const uint8_t *mac);