# /opt/espnow-gateway/src/config.py

SERIAL_PORT = "/dev/ttyESPNowGateway"
BAUD_RATE = 115200

MQTT_BROKER = "192.168.2.11"
MQTT_PORT = 1883
MQTT_USER = "mqttuser"
MQTT_PASSWORD = "mDie100%sV!r"

DISCOVERY_PREFIX = "homeassistant"
BASE_TOPIC = "espnow"

DEVICE_MAPPING = {
    "3c610532519c": ("Zisterne Ultraschall", "espnow/zisterne_us"),
    "aabbccddeeff": ("Zisterne Radar", "espnow/zisterne_radar")
    "aabbccddeeff": ("Garagen Parksensor", "espnow/parksensor"),
}
