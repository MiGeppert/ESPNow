# /opt/espnow-gateway/src/serial_client.py
import serial
import json
import time
import logging
from config import SERIAL_PORT, BAUD_RATE

logger = logging.getLogger("espnow_bridge")

class SerialGatewayClient:
    def __init__(self, on_data_callback):
        self.ser = None
        self.on_data_callback = on_data_callback

    def connect(self):
        while True:
            try:
                self.ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
                logger.info(f"Serieller Port {SERIAL_PORT} erfolgreich geöffnet.")
                break
            except Exception as e:
                logger.error(f"Verbindung zu USB fehlgeschlagen ({e}). Erneuter Versuch in 5s...")
                time.sleep(5)

    def read_loop(self):
        while True:
            if self.ser and self.ser.in_waiting > 0:
                try:
                    line = self.ser.readline().decode("utf-8").strip()
                    if not line:
                        continue

                    try:
                        # 1. Versuch: Schauen, ob es valides JSON vom Gateway ist
                        packet = json.loads(line)
                        self.on_data_callback(packet)
                    except json.JSONDecodeError:
                        # 2. Fallback: Es ist normaler Text (z.B. Bootlogs oder serielle Fehler)
                        # Wir reichen es als log-Paket an die Bridge weiter, damit es im Journalctl landet!
                        text_packet = {
                            "type": "log",
                            "level": "ESP-RAW",
                            "msg": line
                        }
                        self.on_data_callback(text_packet)

                except Exception as e:
                    logger.error(f"Fehler beim Lesen der seriellen Daten: {e}")
            time.sleep(0.01)

    def send_command(self, mac, cmd_type, payload):
        """Sendet ein strukturiertes JSON-Kommando mit Doppelpunkt-MAC zurück zum ESP32-Gateway"""
        if self.ser and self.ser.is_open:
            # Fügt die vom Gateway erwarteten Doppelpunkte wieder ein (z.B. 3c610532519c -> 3c:61:05:32:51:9c)
            mac_formatted = ":".join(mac[i:i+2] for i in range(0, len(mac), 2))

            packet = {
                "type": "cmd",
                "mac": mac_formatted,
                "cmd": cmd_type,
                "payload": payload
            }
            raw_data = json.dumps(packet) + "\n"
            self.ser.write(raw_data.encode("utf-8"))
            logger.info(f"Kommando über UART gesendet: {raw_data.strip()}")
