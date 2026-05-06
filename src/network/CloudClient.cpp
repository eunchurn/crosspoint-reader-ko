#include "CloudClient.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <HalStorage.h>
#include <Logging.h>
#include <WebSocketsClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <cstdio>
#include <cstring>

namespace {

constexpr const char* CONFIG_PATH = "/.crosspoint/cloud.json";

// One global is unavoidable because WebSocketsClient's onEvent callback
// has no userdata pointer.
WebSocketsClient ws;
CloudClient* gInstance = nullptr;

struct ParsedUrl {
  bool ssl = false;
  std::string host;
  uint16_t port = 0;
  std::string path = "/";
  bool valid = false;
};

ParsedUrl parseUrl(const std::string& url) {
  ParsedUrl out;
  size_t i = 0;
  if (url.rfind("wss://", 0) == 0) {
    out.ssl = true;
    out.port = 443;
    i = 6;
  } else if (url.rfind("ws://", 0) == 0) {
    out.ssl = false;
    out.port = 80;
    i = 5;
  } else {
    return out;  // invalid scheme
  }
  size_t pathStart = url.find('/', i);
  std::string authority = pathStart == std::string::npos ? url.substr(i) : url.substr(i, pathStart - i);
  size_t colon = authority.find(':');
  if (colon != std::string::npos) {
    out.host = authority.substr(0, colon);
    out.port = static_cast<uint16_t>(strtoul(authority.c_str() + colon + 1, nullptr, 10));
  } else {
    out.host = authority;
  }
  if (pathStart != std::string::npos) out.path = url.substr(pathStart);
  out.valid = !out.host.empty() && out.port != 0;
  return out;
}

std::string toBaseHttpUrl(const std::string& wsUrl) {
  // wss://host[:port]/api/devices/ws → https://host[:port]
  auto u = parseUrl(wsUrl);
  if (!u.valid) return {};
  std::string scheme = u.ssl ? "https://" : "http://";
  char buf[256];
  if ((u.ssl && u.port == 443) || (!u.ssl && u.port == 80)) {
    snprintf(buf, sizeof(buf), "%s%s", scheme.c_str(), u.host.c_str());
  } else {
    snprintf(buf, sizeof(buf), "%s%s:%u", scheme.c_str(), u.host.c_str(), u.port);
  }
  return buf;
}

}  // namespace

CloudClient& CloudClient::getInstance() {
  static CloudClient inst;
  return inst;
}

void CloudClient::begin() {
  if (started) return;
  started = true;
  gInstance = this;

  hardwareId = deriveHardwareId();
  loadConfig();

  snprintf(pingSuffix, sizeof(pingSuffix), "cloud");
  cmdCtx.pingTagSuffix = pingSuffix;
  cmdCtx.sink = {.ctx = this, .send = &CloudClient::sinkSend};

  xTaskCreate(&CloudClient::taskTrampoline, "CloudClient", 8192, this, 1, nullptr);
  LOG_INF("CLOUD", "CloudClient task started; configured=%d", configured ? 1 : 0);
}

void CloudClient::taskTrampoline(void* arg) { static_cast<CloudClient*>(arg)->taskLoop(); }

void CloudClient::taskLoop() {
  cmdCtx.txScratch.reserve(2048);

  // Wait for WiFi + a valid config before doing anything.
  while (true) {
    if (!configured) {
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }
    if (WiFi.status() != WL_CONNECTED) {
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }
    break;
  }

  ParsedUrl url = parseUrl(serverUrl);
  if (!url.valid) {
    LOG_ERR("CLOUD", "Invalid server URL: %s", serverUrl.c_str());
    vTaskDelete(nullptr);
    return;
  }

  // Auth: hardwareId.token in Sec-WebSocket-Protocol (matches server.ts).
  std::string proto = std::string("cpr.v1, ") + hardwareId + "." + token;
  ws.setExtraHeaders((std::string("Sec-WebSocket-Protocol: ") + proto).c_str());
  ws.setReconnectInterval(5000);  // 5s between reconnect attempts
  ws.enableHeartbeat(15000, 3000, 2);
  ws.onEvent([](WStype_t type, uint8_t* payload, size_t length) {
    if (gInstance) gInstance->onWsEvent(static_cast<int>(type), payload, length);
  });

  if (url.ssl) {
    ws.beginSSL(url.host.c_str(), url.port, url.path.c_str());
  } else {
    ws.begin(url.host.c_str(), url.port, url.path.c_str());
  }
  LOG_INF("CLOUD", "Connecting to %s:%u%s (ssl=%d)", url.host.c_str(), url.port, url.path.c_str(), url.ssl ? 1 : 0);

  while (true) {
    ws.loop();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void CloudClient::onWsEvent(int type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      connected = true;
      LOG_INF("CLOUD", "WS connected");
      break;
    case WStype_DISCONNECTED:
      connected = false;
      LOG_INF("CLOUD", "WS disconnected");
      break;
    case WStype_BIN:
      // [opcode][reqid LE][payload] — straight to FileCommands.
      FileCommands::dispatch(cmdCtx, payload, length);
      break;
    case WStype_ERROR:
      LOG_ERR("CLOUD", "WS error");
      break;
    default:
      break;
  }
}

void CloudClient::sinkSend(void* /*ctx*/, const uint8_t* data, size_t len) {
  // const_cast: WebSocketsClient takes non-const buffer but does not modify.
  ws.sendBIN(const_cast<uint8_t*>(data), len);
}

bool CloudClient::loadConfig() {
  HalFile f;
  if (!Storage.openFileForRead("CLOUD", CONFIG_PATH, f)) {
    LOG_INF("CLOUD", "no config at %s", CONFIG_PATH);
    return false;
  }
  String body = Storage.readFile(CONFIG_PATH);
  f.close();
  if (body.length() == 0) return false;

  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    LOG_ERR("CLOUD", "config json parse failed");
    return false;
  }
  serverUrl = doc["url"] | "";
  hardwareId = doc["hardwareId"] | hardwareId.c_str();
  token = doc["token"] | "";
  deviceId = doc["deviceId"] | "";
  configured = !serverUrl.empty() && !token.empty() && !hardwareId.empty();
  if (configured) {
    LOG_INF("CLOUD", "loaded config: url=%s deviceId=%s", serverUrl.c_str(), deviceId.c_str());
  }
  return configured;
}

bool CloudClient::saveConfig(const std::string& url, const std::string& hwId, const std::string& tok,
                             const std::string& devId) {
  Storage.ensureDirectoryExists("/.crosspoint");
  JsonDocument doc;
  doc["url"] = url;
  doc["hardwareId"] = hwId;
  doc["token"] = tok;
  doc["deviceId"] = devId;
  String out;
  serializeJson(doc, out);
  return Storage.writeFile(CONFIG_PATH, out);
}

std::string CloudClient::deriveHardwareId() {
  // WiFi.macAddress() returns the STA MAC formatted as "AA:BB:CC:DD:EE:FF".
  // Lowercase it for stable comparison (cloud server stores it as-sent).
  String mac = WiFi.macAddress();
  mac.toLowerCase();
  return mac.c_str();
}

bool CloudClient::pairDevice(const std::string& baseHttpUrl, const std::string& code, const std::string& deviceName) {
  if (WiFi.status() != WL_CONNECTED) {
    LOG_ERR("CLOUD", "pair: WiFi not connected");
    return false;
  }
  if (hardwareId.empty()) hardwareId = deriveHardwareId();

  HTTPClient http;
  WiFiClientSecure secureClient;
  bool ssl = baseHttpUrl.rfind("https://", 0) == 0;
  std::string url = baseHttpUrl + "/api/devices/pair";

  bool ok;
  if (ssl) {
    secureClient.setInsecure();  // dev only — replace with cert for prod
    ok = http.begin(secureClient, url.c_str());
  } else {
    ok = http.begin(url.c_str());
  }
  if (!ok) {
    LOG_ERR("CLOUD", "pair: http begin failed");
    return false;
  }
  http.addHeader("Content-Type", "application/json");

  JsonDocument req;
  req["code"] = code;
  req["hardwareId"] = hardwareId;
  if (!deviceName.empty()) req["deviceName"] = deviceName;
  req["firmwareVersion"] = CROSSPOINT_VERSION;
  String body;
  serializeJson(req, body);

  int status = http.POST(body);
  if (status != 200) {
    LOG_ERR("CLOUD", "pair: HTTP %d", status);
    http.end();
    return false;
  }
  String resp = http.getString();
  http.end();

  JsonDocument res;
  if (deserializeJson(res, resp)) {
    LOG_ERR("CLOUD", "pair: bad response json");
    return false;
  }
  std::string newDeviceId = res["deviceId"] | "";
  std::string newToken = res["token"] | "";
  if (newDeviceId.empty() || newToken.empty()) {
    LOG_ERR("CLOUD", "pair: response missing fields");
    return false;
  }

  // Default the WS URL to the base host's wss endpoint.
  std::string wsUrl;
  if (ssl) {
    wsUrl = "wss://" + baseHttpUrl.substr(8) + "/api/devices/ws";
  } else {
    wsUrl = "ws://" + baseHttpUrl.substr(7) + "/api/devices/ws";
  }
  if (!saveConfig(wsUrl, hardwareId, newToken, newDeviceId)) {
    LOG_ERR("CLOUD", "pair: failed to save config");
    return false;
  }
  serverUrl = wsUrl;
  token = newToken;
  deviceId = newDeviceId;
  configured = true;

  if (connected) ws.disconnect();  // task will reconnect with new credentials
  LOG_INF("CLOUD", "paired: deviceId=%s", deviceId.c_str());
  return true;
}
