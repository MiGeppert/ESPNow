#include "serial_protocol.h"

void serialProtocolSetup()
{
    Serial1.begin(
        115200,
        SERIAL_8N1,
        18,
        17);
}

void serialProtocolLoop()
{
    while (Serial1.available())
    {
        String line =
            Serial1.readStringUntil('\n');

        line.trim();

        if (line.isEmpty())
            continue;

        //-------------------------------------------------
        // PC -> Gateway
        //-------------------------------------------------

        Serial.print("[UART RX] ");
        Serial.println(line);

        //-------------------------------------------------
        // Kommandos
        //-------------------------------------------------

        if (line.startsWith("CMD "))
        {
            String cmd =
                line.substring(4);

            Serial.print("CMD: ");
            Serial.println(cmd);

            //-------------------------------------------------
            // später:
            // OTA=1
            // OTA=0
            //-------------------------------------------------

            continue;
        }

        //-------------------------------------------------

        Serial.println("Unbekannter UART-Befehl");
    }
}

void serialSendJson(
    const String &json)
{
    Serial1.print("JSON ");
    Serial1.println(json);

    Serial.print("[UART JSON] ");
    Serial.println(json);
}

void serialSendLog(
    const String &text)
{
    Serial1.print("LOG ");
    Serial1.println(text);

    Serial.print("[UART LOG] ");
    Serial.println(text);
}

void serialSendAck(
    const String &text)
{
    Serial1.print("ACK ");
    Serial1.println(text);

    Serial.print("[UART ACK] ");
    Serial.println(text);
}