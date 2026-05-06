#pragma once

#include <cstdint>
#include <string>

#include "io/FileCommands.h"

// Persistent reverse-tunnel to the CrossPoint Cloud server. The device opens
// an outbound WSS connection and reuses FileCommands to serve LIST/RENAME/
// DELETE/MKDIR (and the rest of the protocol) coming back from the cloud.
//
// Config is stored in /.crosspoint/cloud.json on SD:
//   { "url": "wss://crosspoint.example/api/devices/ws",
//     "hardwareId": "aa:bb:cc:dd:ee:ff",
//     "token": "<long-lived token>" }
// The pairDevice() helper does the one-time POST to /api/devices/pair to
// obtain the token from a 6-char user-supplied code.
class CloudClient {
 public:
  static CloudClient& getInstance();

  // Spawn the connection task. Idempotent. Reads /.crosspoint/cloud.json on
  // boot; if missing or malformed the task stays idle until configure() is
  // called via the pairing flow.
  void begin();

  // True while the WebSocket is connected to the cloud relay.
  bool isConnected() const { return connected; }

  // Run the one-shot pairing exchange. Posts {code, hardwareId, name} to
  // <baseHttpUrl>/api/devices/pair, persists the returned {deviceId, token}
  // to SD, and resets the WebSocket connection so the new credentials take
  // effect. Returns true on 200 OK.
  bool pairDevice(const std::string& baseHttpUrl, const std::string& code, const std::string& deviceName);

 private:
  CloudClient() = default;

  static void taskTrampoline(void* arg);
  void taskLoop();

  bool loadConfig();
  bool saveConfig(const std::string& url, const std::string& hardwareId, const std::string& token,
                  const std::string& deviceId);
  static std::string deriveHardwareId();

  // Type-erased to avoid pulling WebSocketsClient.h into this header. Cast
  // back to WStype_t inside the .cpp.
  void onWsEvent(int type, uint8_t* payload, size_t length);

  // FrameSink callback registered with FileCommands::Context.
  static void sinkSend(void* ctx, const uint8_t* data, size_t len);

  // Persisted config
  std::string serverUrl;  // full ws://… or wss://… URL
  std::string hardwareId;
  std::string token;
  std::string deviceId;
  bool configured = false;

  // Runtime
  FileCommands::Context cmdCtx;
  char pingSuffix[16] = {};
  bool connected = false;
  bool started = false;
};
