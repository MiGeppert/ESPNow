#pragma once

#include <Arduino.h>

class JsonBuilder
{
public:

    JsonBuilder();

    void begin();

    void begin(
        const String &sensorId);

    void add(
        const String &key,
        const String &value);

    String end();

private:

    String json;

    bool first;

    bool isNumber(
        const String &value);
};