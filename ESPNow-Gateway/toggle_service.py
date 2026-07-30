# toggle_service.py
import subprocess
import time
from SCons.Script import Import

# Importiert die von PlatformIO bereitgestellte 'env' Variable
Import("env")

def stop_service(source, target, env):
    print("\n>>> [PlatformIO] Stoppe espnow-bridge.service vor dem Upload...")
    try:
        # Führt den Stop-Befehl aus
        subprocess.run(["sudo", "systemctl", "stop", "espnow-bridge.service"], check=True)
        time.sleep(1)
        print(">>> [PlatformIO] Dienst erfolgreich gestoppt. Port ist frei!\n")
    except subprocess.CalledProcessError as e:
        print(f"\n>>> [PlatformIO] FEHLER beim Stoppen des Dienstes: {e}\n")

def start_service(source, target, env):
    print("\n>>> [PlatformIO] Starte espnow-bridge.service nach erfolgreichem Upload...")
    try:
        subprocess.run(["sudo", "systemctl", "start", "espnow-bridge.service"], check=True)
        print(">>> [PlatformIO] Dienst erfolgreich gestartet!\n")
    except subprocess.CalledProcessError as e:
        print(f"\n>>> [PlatformIO] FEHLER beim Starten des Dienstes: {e}\n")

# Registriere die Aktionen im Upload-Ablauf
env.AddPreAction("upload", stop_service)
env.AddPostAction("upload", start_service)
