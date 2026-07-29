#include "discovery.h"

#include "mqtt_manager.h"

static void publishDiscovery(
    DiscoveryType type,
    const String &sensorId,
    const String &key,
    const String &name,
    const String &deviceClass,
    const String &unit,
    const String &stateClass)
{
    String component;

    switch (type)
    {
        case DISC_SENSOR:
            component = "sensor";
            break;

        case DISC_BINARY_SENSOR:
            component = "binary_sensor";
            break;

        case DISC_SWITCH:
            component = "switch";
            break;
    }

    String configTopic =
        "homeassistant/" +
        component +
        "/" +
        sensorId +
        "/" +
        key +
        "/config";

    String payload =
        "{";

    payload +=
        "\"name\":\"" + name + "\",";

    payload +=
        "\"uniq_id\":\"espnow_" +
        sensorId +
        "_" +
        key +
        "\",";

    //----------------------------------------------------
    // Sensoren
    //----------------------------------------------------

    if (type == DISC_SENSOR)
    {
        payload +=
            "\"stat_t\":\"espnow/" +
            sensorId +
            "/state\",";

        payload +=
            "\"val_tpl\":\"{{ value_json." +
            key +
            " }}\",";
    }

    //----------------------------------------------------
    // Binary Sensor
    //----------------------------------------------------

    if (type == DISC_BINARY_SENSOR)
    {
        payload +=
            "\"stat_t\":\"espnow/" +
            sensorId +
            "/state\",";

        payload +=
            "\"val_tpl\":\"{{ value_json." +
            key +
            " }}\",";

        payload +=
            "\"pl_on\":\"1\","
            "\"pl_off\":\"0\",";
    }

    //----------------------------------------------------
    // Switch
    //----------------------------------------------------

    if (type == DISC_SWITCH)
    {
        payload +=
            "\"cmd_t\":\"espnow/" +
            sensorId +
            "/cmd\",";

        payload +=
            "\"stat_t\":\"espnow/" +
            sensorId +
            "/state\",";

        payload +=
            "\"val_tpl\":\"{{ value_json." +
            key +
            " }}\",";

        payload +=
            "\"pl_on\":\"OTA=1\","
            "\"pl_off\":\"OTA=0\","
            "\"stat_on\":\"1\","
            "\"stat_off\":\"0\",";
    }

    //----------------------------------------------------

    if (unit.length())
        payload +=
            "\"unit_of_meas\":\"" +
            unit +
            "\",";

    if (deviceClass.length())
        payload +=
            "\"dev_cla\":\"" +
            deviceClass +
            "\",";

    if (stateClass.length())
        payload +=
            "\"stat_cla\":\"" +
            stateClass +
            "\",";

    payload +=
        "\"device\":{"
            "\"ids\":[\"" + sensorId + "\"],"
            "\"name\":\"" + sensorId + "\","
            "\"manufacturer\":\"ESPNow\","
            "\"model\":\"ESPNow Sensor\""
        "}"
        "}";

    mqttPublish(
        configTopic,
        payload,
        true);
}

void sendDiscovery(
    const String &sensorId,
    const String &key)
{
    //--------------------------------------------------

    if (key == "LEVEL")
    {
        publishDiscovery(
            DISC_SENSOR,
            sensorId,
            key,
            "Füllstand",
            "distance",
            "cm",
            "measurement");

        return;
    }

    //--------------------------------------------------

    if (key == "TEMP")
    {
        publishDiscovery(
            DISC_SENSOR,
            sensorId,
            key,
            "Temperatur",
            "temperature",
            "°C",
            "measurement");

        return;
    }

    //--------------------------------------------------

    if (key == "HUM")
    {
        publishDiscovery(
            DISC_SENSOR,
            sensorId,
            key,
            "Luftfeuchte",
            "humidity",
            "%",
            "measurement");

        return;
    }

    //--------------------------------------------------

    if (key == "BAT")
    {
        publishDiscovery(
            DISC_SENSOR,
            sensorId,
            key,
            "Batterie",
            "voltage",
            "V",
            "measurement");

        return;
    }

    //--------------------------------------------------

    if (key == "RSSI")
    {
        publishDiscovery(
            DISC_SENSOR,
            sensorId,
            key,
            "RSSI",
            "signal_strength",
            "dBm",
            "measurement");

        return;
    }

    //--------------------------------------------------

    if (key == "OK")
    {
        publishDiscovery(
            DISC_BINARY_SENSOR,
            sensorId,
            key,
            "OK",
            "connectivity",
            "",
            "");        

        return;
    }

    //--------------------------------------------------

    if (key == "JUMPER")
    {
        publishDiscovery(
            DISC_BINARY_SENSOR,
            sensorId,
            key,
            "OTA Jumper",
            "plug",
            "",
            "");

        return;
    }

    //--------------------------------------------------

    if (key == "OTA")
    {
        publishDiscovery(
            DISC_SWITCH,
            sensorId,
            key,
            "OTA",
            "",
            "",
            "");

        return;
    }

    //--------------------------------------------------
    // unbekannter Wert
    //--------------------------------------------------

    publishDiscovery(
        DISC_SENSOR,
        sensorId,
        key,
        key,
        "",
        "",
        "");
}