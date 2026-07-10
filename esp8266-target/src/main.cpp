#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>

#ifndef WIFI_SSID
#error WIFI_SSID is not defined. Fill esp8266-target/.env before building.
#endif

#ifndef WIFI_PASSWORD
#error WIFI_PASSWORD is not defined. Fill esp8266-target/.env before building.
#endif

#ifndef OTA_HOSTNAME
#error OTA_HOSTNAME is not defined. Fill esp8266-target/.env before building.
#endif

#ifndef SERIAL_BAUD
#define SERIAL_BAUD 115200
#endif

#ifndef WIFI_CONNECT_TIMEOUT_MS
#define WIFI_CONNECT_TIMEOUT_MS 15000UL
#endif

#ifndef REMOTE_SIGNAL_BAUD
#define REMOTE_SIGNAL_BAUD 9600
#endif

#ifndef REMOTE_SIGNAL_RX_PIN
#define REMOTE_SIGNAL_RX_PIN 3
#endif

#ifndef FALLBACK_AP_SSID
#define FALLBACK_AP_SSID "ifan04-setup"
#endif

#ifndef FALLBACK_AP_PASSWORD
#define FALLBACK_AP_PASSWORD ""
#endif

namespace
{
  struct Config
  {
    String wifiSsid;
    String wifiPassword;
    String otaHostname;
    String fallbackApSsid;
    String fallbackApPassword;
  };

  struct SignalFrame
  {
    unsigned long receivedAtMs;
    String hexPayload;
    String textPayload;
  };

  const unsigned long kConnectTimeoutMs = WIFI_CONNECT_TIMEOUT_MS;
  constexpr char kConfigPath[] = "/config.txt";
  constexpr size_t kMaxSignalFrames = 24;
  constexpr size_t kMaxSignalBufferBytes = 128;
  constexpr unsigned long kSignalFrameGapMs = 40;

  Config activeConfig = {
      WIFI_SSID,
      WIFI_PASSWORD,
      OTA_HOSTNAME,
      FALLBACK_AP_SSID,
      FALLBACK_AP_PASSWORD,
  };
  ESP8266WebServer webServer(80);
  SignalFrame signalFrames[kMaxSignalFrames];
  size_t signalFrameCount = 0;
  size_t signalFrameWriteIndex = 0;
  String signalBuffer;
  unsigned long lastSignalByteAtMs = 0;
  unsigned long scheduledRestartAtMs = 0;
  bool wifiConnected = false;
  bool fallbackApActive = false;

  String htmlEscape(const String &value)
  {
    String escaped;
    escaped.reserve(value.length() + 16);

    for (size_t index = 0; index < value.length(); ++index)
    {
      switch (value[index])
      {
      case '&':
        escaped += F("&amp;");
        break;
      case '<':
        escaped += F("&lt;");
        break;
      case '>':
        escaped += F("&gt;");
        break;
      case '"':
        escaped += F("&quot;");
        break;
      case '\'':
        escaped += F("&#39;");
        break;
      default:
        escaped += value[index];
        break;
      }
    }

    return escaped;
  }

  String jsonEscape(const String &value)
  {
    String escaped;
    escaped.reserve(value.length() + 16);

    for (size_t index = 0; index < value.length(); ++index)
    {
      const char current = value[index];

      switch (current)
      {
      case '\\':
        escaped += F("\\\\");
        break;
      case '"':
        escaped += F("\\\"");
        break;
      case '\n':
        escaped += F("\\n");
        break;
      case '\r':
        escaped += F("\\r");
        break;
      case '\t':
        escaped += F("\\t");
        break;
      default:
        escaped += current;
        break;
      }
    }

    return escaped;
  }

  String printablePayload(const String &payload)
  {
    String text;
    text.reserve(payload.length());

    for (size_t index = 0; index < payload.length(); ++index)
    {
      const uint8_t current = static_cast<uint8_t>(payload[index]);
      text += (current >= 32 && current <= 126) ? static_cast<char>(current) : '.';
    }

    return text;
  }

  String hexPayload(const String &payload)
  {
    static const char *digits = "0123456789ABCDEF";
    String text;
    text.reserve(payload.length() * 3);

    for (size_t index = 0; index < payload.length(); ++index)
    {
      const uint8_t current = static_cast<uint8_t>(payload[index]);
      if (index > 0)
      {
        text += ' ';
      }
      text += digits[current >> 4];
      text += digits[current & 0x0F];
    }

    return text;
  }

  String currentIpAddress()
  {
    return fallbackApActive ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  }

  String currentNetworkMode()
  {
    if (wifiConnected)
    {
      return F("wifi");
    }

    if (fallbackApActive)
    {
      return F("ap");
    }

    return F("offline");
  }

  String fallbackApSsidWithChipId()
  {
    String ssid = activeConfig.fallbackApSsid;
    if (ssid.length() == 0)
    {
      ssid = F("ifan04");
    }

    ssid += '-';
    ssid += String(ESP.getChipId(), HEX);
    return ssid;
  }

  void persistConfig()
  {
    File configFile = LittleFS.open(kConfigPath, "w");
    if (!configFile)
    {
      Serial.println(F("Failed to open config file for writing"));
      return;
    }

    configFile.printf("WIFI_SSID=%s\n", activeConfig.wifiSsid.c_str());
    configFile.printf("WIFI_PASSWORD=%s\n", activeConfig.wifiPassword.c_str());
    configFile.printf("OTA_HOSTNAME=%s\n", activeConfig.otaHostname.c_str());
    configFile.printf("FALLBACK_AP_SSID=%s\n", activeConfig.fallbackApSsid.c_str());
    configFile.printf("FALLBACK_AP_PASSWORD=%s\n", activeConfig.fallbackApPassword.c_str());
    configFile.close();
  }

  void loadPersistedConfig()
  {
    if (!LittleFS.exists(kConfigPath))
    {
      return;
    }

    File configFile = LittleFS.open(kConfigPath, "r");
    if (!configFile)
    {
      Serial.println(F("Failed to open persisted config"));
      return;
    }

    while (configFile.available())
    {
      String line = configFile.readStringUntil('\n');
      line.trim();
      if (line.length() == 0 || line.startsWith("#"))
      {
        continue;
      }

      const int separator = line.indexOf('=');
      if (separator <= 0)
      {
        continue;
      }

      const String key = line.substring(0, separator);
      const String value = line.substring(separator + 1);

      if (key == F("WIFI_SSID"))
      {
        activeConfig.wifiSsid = value;
      }
      else if (key == F("WIFI_PASSWORD"))
      {
        activeConfig.wifiPassword = value;
      }
      else if (key == F("OTA_HOSTNAME"))
      {
        activeConfig.otaHostname = value;
      }
      else if (key == F("FALLBACK_AP_SSID"))
      {
        activeConfig.fallbackApSsid = value;
      }
      else if (key == F("FALLBACK_AP_PASSWORD"))
      {
        activeConfig.fallbackApPassword = value;
      }
    }

    configFile.close();
  }

  void clearPersistedConfig()
  {
    if (LittleFS.exists(kConfigPath))
    {
      LittleFS.remove(kConfigPath);
    }
  }

  void storeSignalFrame(const String &payload)
  {
    if (payload.length() == 0)
    {
      return;
    }

    SignalFrame &frame = signalFrames[signalFrameWriteIndex];
    frame.receivedAtMs = millis();
    frame.hexPayload = hexPayload(payload);
    frame.textPayload = printablePayload(payload);

    signalFrameWriteIndex = (signalFrameWriteIndex + 1) % kMaxSignalFrames;
    if (signalFrameCount < kMaxSignalFrames)
    {
      ++signalFrameCount;
    }

    Serial.print(F("Signal frame: "));
    Serial.println(frame.hexPayload);
  }

  void flushSignalBuffer()
  {
    if (signalBuffer.length() == 0)
    {
      return;
    }

    storeSignalFrame(signalBuffer);
    signalBuffer = "";
  }

  bool connectToWifi()
  {
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.hostname(activeConfig.otaHostname);
    WiFi.begin(activeConfig.wifiSsid.c_str(), activeConfig.wifiPassword.c_str());

    Serial.print(F("Connecting to WiFi"));

    const unsigned long startMs = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startMs < kConnectTimeoutMs)
    {
      delay(500);
      Serial.print('.');
    }

    Serial.println();

    wifiConnected = WiFi.status() == WL_CONNECTED;
    fallbackApActive = false;

    if (!wifiConnected)
    {
      Serial.println(F("WiFi connection failed, switching to fallback AP"));
      return false;
    }

    Serial.print(F("WiFi connected, IP: "));
    Serial.println(WiFi.localIP());
    Serial.print(F("OTA target IP: "));
    Serial.println(WiFi.localIP());
    return true;
  }

  void startFallbackAp()
  {
    const String fallbackSsid = fallbackApSsidWithChipId();
    const String fallbackPassword = activeConfig.fallbackApPassword;

    WiFi.disconnect();
    WiFi.mode(WIFI_AP);

    const bool started = fallbackPassword.length() >= 8
                             ? WiFi.softAP(fallbackSsid.c_str(), fallbackPassword.c_str())
                             : WiFi.softAP(fallbackSsid.c_str());

    wifiConnected = false;
    fallbackApActive = started;

    if (!started)
    {
      Serial.println(F("Failed to start fallback AP"));
      return;
    }

    Serial.print(F("Fallback AP started: "));
    Serial.println(fallbackSsid);
    Serial.print(F("AP IP: "));
    Serial.println(WiFi.softAPIP());

    if (fallbackPassword.length() >= 8)
    {
      Serial.println(F("AP uses the configured password"));
    }
    else
    {
      Serial.println(F("AP is open because FALLBACK_AP_PASSWORD is empty or too short"));
    }
  }

  void setupOta()
  {
    ArduinoOTA.setHostname(activeConfig.otaHostname.c_str());
    ArduinoOTA.onStart([]()
                       { Serial.println(F("OTA update started")); });
    ArduinoOTA.onEnd([]()
                     { Serial.println(F("OTA update finished")); });
    ArduinoOTA.onError([](ota_error_t error)
                       { Serial.printf("OTA error: %u\n", static_cast<unsigned>(error)); });
    ArduinoOTA.begin();

    Serial.print(F("ArduinoOTA ready as: "));
    Serial.println(activeConfig.otaHostname);
  }

  String renderSignalsHtml()
  {
    if (signalFrameCount == 0)
    {
      return F("<p>No remote frames received yet.</p>");
    }

    String html = F("<div class='signals'>");
    for (size_t offset = 0; offset < signalFrameCount; ++offset)
    {
      const size_t index = (signalFrameWriteIndex + kMaxSignalFrames - 1 - offset) % kMaxSignalFrames;
      const SignalFrame &frame = signalFrames[index];
      html += F("<article class='frame'><div><strong>t=</strong> ");
      html += String(frame.receivedAtMs);
      html += F(" ms</div><div><strong>HEX</strong> ");
      html += htmlEscape(frame.hexPayload);
      html += F("</div><div><strong>TXT</strong> ");
      html += htmlEscape(frame.textPayload);
      html += F("</div></article>");
    }
    html += F("</div>");
    return html;
  }

  void handleRoot()
  {
    const String apName = fallbackApSsidWithChipId();
    String html = F(
        "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>iFan04 Control</title><style>body{font-family:Arial,sans-serif;background:#f4f1ea;color:#1d1d1d;margin:0;padding:24px;}"
        ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:18px;}"
        ".card{background:#fff;border-radius:16px;padding:18px;box-shadow:0 8px 24px rgba(0,0,0,.08);}"
        "h1,h2{margin-top:0;}label{display:block;margin:10px 0 4px;font-weight:600;}input{width:100%;padding:10px;border:1px solid #c9c3b8;border-radius:10px;box-sizing:border-box;}"
        "button{margin-top:14px;padding:10px 14px;border:0;border-radius:10px;background:#145a32;color:#fff;font-weight:700;cursor:pointer;}"
        ".danger{background:#922b21;}pre{white-space:pre-wrap;word-break:break-word;}article.frame{padding:10px;border-top:1px solid #ece7de;}small{color:#666;}</style>"
        "<script>async function refreshStatus(){const r=await fetch('/api/status');const d=await r.json();document.getElementById('status').textContent=d.mode;document.getElementById('ip').textContent=d.ip;document.getElementById('ssid').textContent=d.connectedSsid;document.getElementById('ota').textContent=d.otaHostname;document.getElementById('signals').innerHTML=d.signalsHtml;}setInterval(refreshStatus,1500);window.addEventListener('load',refreshStatus);</script>"
        "</head><body><h1>Sonoff iFan04</h1><div class='grid'>");

    html += F("<section class='card'><h2>Status</h2><p><strong>Mode:</strong> <span id='status'>");
    html += htmlEscape(currentNetworkMode());
    html += F("</span></p><p><strong>IP:</strong> <span id='ip'>");
    html += htmlEscape(currentIpAddress());
    html += F("</span></p><p><strong>SSID/AP:</strong> <span id='ssid'>");
    html += htmlEscape(wifiConnected ? activeConfig.wifiSsid : apName);
    html += F("</span></p><p><strong>OTA Hostname:</strong> <span id='ota'>");
    html += htmlEscape(activeConfig.otaHostname);
    html += F("</span></p><p><strong>Signal source:</strong> UART RX GPIO");
    html += String(REMOTE_SIGNAL_RX_PIN);
    html += F(" @ ");
    html += String(REMOTE_SIGNAL_BAUD);
    html += F(" baud</p><small>Saved values in flash override .env defaults until cleared.</small></section>");

    html += F("<section class='card'><h2>WiFi / OTA Config</h2><form method='post' action='/config'>");
    html += F("<label for='wifiSsid'>WiFi SSID</label><input id='wifiSsid' name='wifiSsid' value='");
    html += htmlEscape(activeConfig.wifiSsid);
    html += F("'><label for='wifiPassword'>WiFi Password</label><input id='wifiPassword' name='wifiPassword' type='password' value='");
    html += htmlEscape(activeConfig.wifiPassword);
    html += F("'><label for='otaHostname'>OTA Hostname</label><input id='otaHostname' name='otaHostname' value='");
    html += htmlEscape(activeConfig.otaHostname);
    html += F("'><label for='fallbackApSsid'>Fallback AP SSID Prefix</label><input id='fallbackApSsid' name='fallbackApSsid' value='");
    html += htmlEscape(activeConfig.fallbackApSsid);
    html += F("'><label for='fallbackApPassword'>Fallback AP Password</label><input id='fallbackApPassword' name='fallbackApPassword' type='password' value='");
    html += htmlEscape(activeConfig.fallbackApPassword);
    html += F("'><button type='submit'>Save and Restart</button></form><form method='post' action='/clear-config'><button class='danger' type='submit'>Clear Saved Config and Reboot</button></form></section>");

    html += F("<section class='card'><h2>Received Remote Frames</h2><div id='signals'>");
    html += renderSignalsHtml();
    html += F("</div></section></div></body></html>");

    webServer.send(200, F("text/html; charset=utf-8"), html);
  }

  void handleStatusApi()
  {
    const String apName = fallbackApSsidWithChipId();
    String json = F("{");
    json += F("\"mode\":\"");
    json += jsonEscape(currentNetworkMode());
    json += F("\",\"ip\":\"");
    json += jsonEscape(currentIpAddress());
    json += F("\",\"connectedSsid\":\"");
    json += jsonEscape(wifiConnected ? activeConfig.wifiSsid : apName);
    json += F("\",\"otaHostname\":\"");
    json += jsonEscape(activeConfig.otaHostname);
    json += F("\",\"signalsHtml\":\"");
    json += jsonEscape(renderSignalsHtml());
    json += F("\"}");
    webServer.send(200, F("application/json"), json);
  }

  void handleSaveConfig()
  {
    const String wifiSsid = webServer.arg("wifiSsid");
    const String otaHostname = webServer.arg("otaHostname");
    const String fallbackApPassword = webServer.arg("fallbackApPassword");

    if (wifiSsid.length() == 0 || otaHostname.length() == 0)
    {
      webServer.send(400, F("text/plain"), F("wifiSsid and otaHostname are required"));
      return;
    }

    if (fallbackApPassword.length() > 0 && fallbackApPassword.length() < 8)
    {
      webServer.send(400, F("text/plain"), F("fallbackApPassword must be empty or at least 8 characters"));
      return;
    }

    activeConfig.wifiSsid = wifiSsid;
    activeConfig.wifiPassword = webServer.arg("wifiPassword");
    activeConfig.otaHostname = otaHostname;
    activeConfig.fallbackApSsid = webServer.arg("fallbackApSsid");
    activeConfig.fallbackApPassword = fallbackApPassword;

    persistConfig();
    scheduledRestartAtMs = millis() + 1500;
    webServer.send(200, F("text/html; charset=utf-8"), F("<html><body><h1>Saved</h1><p>Configuration stored in flash. The device will restart now.</p></body></html>"));
  }

  void handleClearConfig()
  {
    clearPersistedConfig();
    scheduledRestartAtMs = millis() + 1500;
    webServer.send(200, F("text/html; charset=utf-8"), F("<html><body><h1>Cleared</h1><p>Stored configuration removed. The device will restart and use .env defaults again.</p></body></html>"));
  }

  void setupWebServer()
  {
    webServer.on(F("/"), HTTP_GET, handleRoot);
    webServer.on(F("/api/status"), HTTP_GET, handleStatusApi);
    webServer.on(F("/config"), HTTP_POST, handleSaveConfig);
    webServer.on(F("/clear-config"), HTTP_POST, handleClearConfig);
    webServer.begin();

    Serial.println(F("Web UI ready"));
  }
} // namespace

void setup()
{
  LittleFS.begin();
  loadPersistedConfig();
  Serial.begin(SERIAL_BAUD);
  Serial.println();
  Serial.println(F("ESP8266 OTA bootstrap starting"));
  Serial.printf("Listening for remote bytes on GPIO%d at %d baud\n", REMOTE_SIGNAL_RX_PIN, REMOTE_SIGNAL_BAUD);

  if (!connectToWifi())
  {
    startFallbackAp();
  }

  setupOta();
  setupWebServer();
}

void loop()
{
  while (Serial.available())
  {
    const char current = static_cast<char>(Serial.read());
    signalBuffer += current;
    if (signalBuffer.length() >= kMaxSignalBufferBytes || current == '\n' || current == '\r')
    {
      flushSignalBuffer();
    }
    lastSignalByteAtMs = millis();
  }

  if (signalBuffer.length() > 0 && millis() - lastSignalByteAtMs > kSignalFrameGapMs)
  {
    flushSignalBuffer();
  }

  ArduinoOTA.handle();
  webServer.handleClient();

  if (scheduledRestartAtMs != 0 && millis() >= scheduledRestartAtMs)
  {
    ESP.restart();
  }
}
