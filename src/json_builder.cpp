#include "json_builder.h"

JsonBuilder::JsonBuilder()
{
    begin();
}

void JsonBuilder::begin()
{
    json = "{";

    first = true;
}

void JsonBuilder::begin(
    const String &sensorId)
{
    begin();

    add(
        "ID",
        sensorId);
}

bool JsonBuilder::isNumber(
    const String &value)
{
    bool decimalPoint = false;

    if (value.length() == 0)
        return false;

    for (uint16_t i = 0; i < value.length(); i++)
    {
        char c = value[i];

        if (c >= '0' && c <= '9')
            continue;

        if (c == '.')
        {
            if (decimalPoint)
                return false;

            decimalPoint = true;

            continue;
        }

        if (i == 0 && c == '-')
            continue;

        return false;
    }

    return true;
}

void JsonBuilder::add(
    const String &key,
    const String &value)
{
    if (!first)
        json += ",";

    first = false;

    json += "\"";
    json += key;
    json += "\":";

    if (isNumber(value))
    {
        json += value;
    }
    else
    {
        json += "\"";
        json += value;
        json += "\"";
    }
}

String JsonBuilder::end()
{
    json += "}";

    return json;
}