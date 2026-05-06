#pragma once

#include <cstdint>
#include <vector>

#include "FileCommands.h"

// USB-CDC bridge that exposes SD-card file operations to a host running in a
// browser via the WebSerial API. Wire format is SLIP-framed binary; logging
// (text) and frames (binary) coexist on the same Serial because text never
// contains the SLIP delimiter (0xC0) or escape (0xDB).
//
// Frame layout (after SLIP decoding):
//   [u8 opcode][u16 reqid LE][payload...]
// Actual command processing is delegated to FileCommands::dispatch so the
// CloudClient (WebSocket transport) shares the exact same logic.
class SerialFileBridge {
 public:
  static SerialFileBridge& getInstance();
  void begin();

  // Surface the value returned by HWCDC.setRxBufferSize() so the host can
  // verify the resize actually took effect via the PING response.
  static void setRxBufferReportedSize(size_t v) { rxBufferReportedSize = v; }
  static size_t getRxBufferReportedSize() { return rxBufferReportedSize; }

 private:
  SerialFileBridge() = default;

  static void taskTrampoline(void* arg);
  void taskLoop();

  void feedByte(uint8_t b);
  void writeFrame(const uint8_t* data, size_t len);

  // FrameSink callback registered with FileCommands::Context.
  static void sinkSend(void* ctx, const uint8_t* data, size_t len);

  static constexpr size_t MAX_FRAME_SIZE = 8192;
  std::vector<uint8_t> rxFrame;
  bool rxEscape = false;
  bool rxStarted = false;

  FileCommands::Context cmdCtx;
  // Per-instance ping suffix kept alive (stored in cmdCtx.pingTagSuffix as
  // a non-owning pointer, so own the storage here).
  char pingSuffix[24] = {};

  bool started = false;
  static size_t rxBufferReportedSize;
};
