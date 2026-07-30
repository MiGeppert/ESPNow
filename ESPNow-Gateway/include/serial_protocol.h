#pragma once

#include <Arduino.h>

void serialProtocolSetup();

void serialProtocolLoop();

void serialSendJson(
    const String &json);

void serialSendLog(
    const String &text);

void serialSendAck(
    const String &text);