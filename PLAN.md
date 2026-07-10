## Plan: ESP32 Bridge for ESP8266 OTA Bootstrap

Das bestehende ESP32-Projekt bleibt eine serielle Bridge. Der erste Flash des ESP8266 wird nicht vollautomatisch, weil laut Anforderung nur RX/TX zwischen ESP32 und ESP8266 verdrahtet sind. Daher ist der praktikable Weg: PlatformIO auf dem Mac startet den Upload, der ESP32 leitet UART-Daten transparent weiter, und der ESP8266 wird einmalig manuell in den ROM-Bootloader versetzt. Danach enthält die Ziel-Firmware OTA, versucht sich zuerst mit einem vorhandenen WLAN zu verbinden und öffnet bei Fehlschlag automatisch einen eigenen Access Point.

**Steps**
1. Phase 1: Bridge-Anforderungen festziehen. Dokumentieren, dass der ESP32 in diesem Repo nur als USB-zu-UART-Bridge für den ESP8266 dient und keine eigene Flash-Logik oder Bootloader-Protokollimplementierung benötigt.
2. Phase 1: Serielle Parameter des Zielpfads definieren. Entscheiden, ob die Bridge dauerhaft mit 115200 arbeitet oder für den Initial-Flash ein konservativerer Upload-Takt genutzt wird; die Bridge selbst bleibt transparent und puffert Byte-für-Byte in beide Richtungen.
3. Phase 1: Betriebsgrenzen explizit aufnehmen. Festhalten, dass ohne zusätzliche Leitungen zu GPIO0 und RESET/EN des ESP8266 kein automatischer Eintritt in den Bootloader möglich ist; der Benutzer bringt den ESP8266 für den Erst-Flash manuell in den Bootloader. Dieser Schritt blockiert alle Automatisierungsoptionen.
4. Phase 2: Host-Upload-Workflow planen. Eine separate PlatformIO-Umgebung oder ein zweites Projekt für die ESP8266-Ziel-Firmware vorsehen, deren Upload-Port auf den USB-Port des ESP32 zeigt. Der Upload nutzt normale esptool-/PlatformIO-Mechanik, weil der ESP32 nur die UART-Strecke durchreicht. *depends on 1-3*
5. Phase 2: Bestehendes ESP32-Projekt minimal härten. In /Users/lasarkolja/Code/ESP2Serial/src/main.cpp optional Statusausgaben, Flush-Verhalten und gegebenenfalls Baud-Konfiguration so anpassen, dass der Bridge-Betrieb während Flash und serieller Logs stabil ist. Keine protokollspezifische Intelligenz einbauen. *depends on 1-3*
6. Phase 2: Ziel-Firmware-Konfiguration definieren. Eine `.env`-Datei für die ESP8266-Ziel-Firmware vorsehen, in der mindestens SSID, WLAN-Passwort, OTA-Hostname und optional ein AP-Name samt AP-Passwort hinterlegt werden. Der Build der Ziel-Firmware muss diese Werte vor dem Flashen einlesen und in Compile-Time-Defines oder eine generierte Konfigurationsdatei überführen. *depends on 4-5*
7. Phase 2: WLAN- und Fallback-Verhalten der Ziel-Firmware festlegen. Beim Boot versucht der ESP8266 innerhalb eines klar begrenzten Zeitfensters, sich mit dem in der `.env` definierten WLAN zu verbinden. Falls das scheitert, startet er automatisch einen eigenen Access Point, damit Konfiguration, Diagnose oder OTA im Fallback-Betrieb weiter möglich bleiben. *depends on 6*
8. Phase 2: Bediensequenz für den Erst-Flash definieren. Reihenfolge: ESP32-Bridge starten, `.env` für die Ziel-Firmware befüllen, ESP8266 manuell mit GPIO0 low und Reset in den UART-Bootloader bringen, PlatformIO-Upload der Ziel-Firmware ausführen, danach normal booten und WLAN-/OTA-Erreichbarkeit prüfen. *depends on 6-7*
9. Phase 3: OTA-Zielzustand absichern. Die ESP8266-Ziel-Firmware muss OTA explizit initialisieren, Netzwerkzugang bereitstellen und einen erfolgreichen OTA-Port oder Web-Updater anbieten, sowohl im bestehenden WLAN als auch im Fallback-AP-Modus, falls dieser Pfad gewünscht ist. Diese Firmware liegt außerhalb dieses Repos, muss aber als Voraussetzung im Plan benannt werden. *parallel with 5 once 4 is defined*
10. Phase 3: Dokumentation ergänzen. Eine kurze README oder Projektanleitung aufnehmen: Verdrahtung ESP32↔ESP8266, `.env`-Format, manueller Bootloader-Einstieg, PlatformIO-Upload-Befehl für den Erst-Flash, Verhalten bei WLAN-Fehlschlag und anschließender OTA-Workflow. *depends on 8-9*

**Relevant files**
- /Users/lasarkolja/Code/ESP2Serial/platformio.ini — bestehende ESP32-Umgebung; hier nur Bridge-bezogene Parameter beibehalten oder anpassen, nicht den ESP8266-Upload selbst hineinzwängen, falls die Ziel-Firmware separat liegt.
- /Users/lasarkolja/Code/ESP2Serial/src/main.cpp — aktueller UART-Passthrough; Referenz für die Bridge, die bewusst simpel bleiben sollte.

**Target firmware requirements**
- Die ESP8266-Ziel-Firmware liest vor dem Build WLAN- und OTA-Parameter aus einer `.env`-Datei ein.
- Pflichtwerte in `.env`: `WIFI_SSID`, `WIFI_PASSWORD`, `OTA_HOSTNAME`.
- Optionale Fallback-Werte in `.env`: `FALLBACK_AP_SSID`, `FALLBACK_AP_PASSWORD`.
- Wenn keine optionalen AP-Werte gesetzt sind, sollte die Ziel-Firmware sinnvolle Defaults verwenden, zum Beispiel einen AP-Namen mit Gerätekennung.
- Die `.env`-Datei darf nicht versioniert werden; stattdessen sollte ein `.env.example` für das Schema dokumentiert werden.

**Verification**
1. ESP32-Bridge kompilieren und auf das DevKit laden.
2. Mit einem seriellen Loopback- oder Echo-Test prüfen, dass Daten stabil vom Mac über den ESP32 zum ESP8266-UART gelangen und zurückkommen.
3. Den ESP8266 manuell in den Bootloader bringen und einen kleinen Test-Build per PlatformIO über den USB-Port des ESP32 hochladen.
4. Nach dem Erst-Flash verifizieren, dass die Ziel-Firmware sich mit dem in `.env` hinterlegten WLAN verbindet und der OTA-Endpunkt oder Netzwerk-Port erreichbar ist.
5. Einen Fehlerszenario-Test durchführen: absichtlich ungültige WLAN-Daten setzen oder das Ziel-WLAN deaktivieren und prüfen, dass der ESP8266 stattdessen seinen eigenen AP startet.
6. Einen zweiten Upload ausschließlich per OTA durchführen, idealerweise sowohl im normalen WLAN-Betrieb als auch, falls unterstützt, im Fallback-AP-Betrieb.

**Decisions**
- Im Scope: Planung für ESP32-Bridge, manueller Erst-Flash, anschließender OTA-fähiger Workflow.
- Außerhalb des Scopes: Vollautomatische Bootloader-Steuerung des ESP8266 ohne zusätzliche Leitungen; Implementierung der eigentlichen ESP8266-Anwendungsfirmware in diesem Repo.
- Empfohlene Architektur: ESP32 bleibt ein transparenter Transportpfad; die Flash-Logik bleibt auf dem Host in PlatformIO/esptool.
- Konfigurationsstrategie: WLAN- und OTA-Credentials werden vor dem Flashen aus einer lokalen `.env` in die ESP8266-Zielfirmware injiziert.
- Netzstrategie: Station-Mode zuerst, Fallback-AP nur bei Verbindungsfehler oder Timeout.

**Further Considerations**
1. Falls später doch RESET/EN und GPIO0 verdrahtbar sind, kann der Plan auf automatisches Bootloader-Toggling erweitert werden; dann wäre ein deutlich komfortablerer Ein-Klick-Upload möglich.
2. Wenn die Ziel-Firmware noch keine OTA-Funktion hat, sollte sie in einem separaten ESP8266-Projekt mit sauberem Erst-Flash/OTA-Migrationspfad vorbereitet werden.
3. Falls der Fallback-AP später ein Konfigurationsportal bekommen soll, sollte der Plan um persistente Speicherung neuer WLAN-Daten erweitert werden; das ist vom jetzigen Minimalziel getrennt.

Generated by Copilot
