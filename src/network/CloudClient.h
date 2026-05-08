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

  // True once a token + url + hardwareId are all present (i.e. paired).
  bool isPaired() const { return configured; }

  const std::string& getServerUrl() const { return serverUrl; }
  const std::string& getDeviceId() const { return deviceId; }
  const std::string& getHardwareId() const { return hardwareId; }

  // Persist the WS server URL on its own (no token yet). Used when the user
  // enters/edits the URL before getting a pairing code. Returns false if the
  // string is not a valid ws:// or wss:// URL.
  bool setServerUrl(const std::string& wsUrl);

  // Convenience wrapper around pairDevice(): derives the base HTTP URL from
  // the currently-configured WS serverUrl. Returns false if no URL set yet.
  bool pairWithCode(const std::string& code, const std::string& deviceName);

  // Erase the saved config and drop the live socket. Returns true on success.
  bool forgetDevice();

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

  // FrameSink callback registered with FileCommands::Context. Returns
  // false when the WS lib couldn't accept the bytes (typically TLS
  // record alloc failure under heap pressure) so streaming pumps can
  // retry instead of advancing past lost data.
  static bool sinkSend(void* ctx, const uint8_t* data, size_t len);

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
  uint32_t connectedAtMs = 0;  // millis() at WStype_CONNECTED, 0 when down

  // Set by setServerUrl()/pairWithCode()/forgetDevice() to nudge the task
  // loop into reopening the WebSocket with new credentials. Cleared by the
  // task once it has torn down the previous connection.
  volatile bool needsRestart = false;
};
