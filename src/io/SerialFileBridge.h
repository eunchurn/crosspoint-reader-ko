#pragma once

#include <HalStorage.h>

#include <cstdint>
#include <string>
#include <vector>

// USB-CDC bridge that exposes SD-card file operations to a host running in a
// browser via the WebSerial API. Wire format is SLIP-framed binary; logging
// (text) and frames (binary) coexist on the same Serial because text never
// contains the SLIP delimiter (0xC0) or escape (0xDB).
//
// Frame layout (after SLIP decoding):
//   [u8 opcode][u16 reqid LE][payload...]
// Response opcode = request opcode | 0x80, except errors which use 0xFF.
// All multi-byte integers are little-endian.
class SerialFileBridge {
 public:
  static SerialFileBridge& getInstance();

  // Spawn the FreeRTOS task. Safe to call once during setup() after
  // Storage.begin(). No-op if already started.
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
  void handleFrame();
  void dispatch(uint8_t opcode, uint16_t reqid, const uint8_t* payload, size_t len);

  void handlePing(uint16_t reqid);
  void handleListDir(uint16_t reqid, const uint8_t* p, size_t n);
  void handleStat(uint16_t reqid, const uint8_t* p, size_t n);
  void handleRead(uint16_t reqid, const uint8_t* p, size_t n);
  void handleWriteBegin(uint16_t reqid, const uint8_t* p, size_t n);
  void handleWriteChunk(uint16_t reqid, const uint8_t* p, size_t n);
  void handleWriteEnd(uint16_t reqid, const uint8_t* p, size_t n);
  void handleDelete(uint16_t reqid, const uint8_t* p, size_t n);
  void handleMkdir(uint16_t reqid, const uint8_t* p, size_t n);
  void handleRename(uint16_t reqid, const uint8_t* p, size_t n);
  void abortActiveWrite();

  void sendOk(uint8_t opcode, uint16_t reqid, const uint8_t* payload, size_t len);
  void sendError(uint16_t reqid, uint8_t code, const char* msg);
  void writeFrame(const uint8_t* data, size_t len);

  // SLIP decode state. rxFrame buffer accumulates decoded payload (header +
  // body). Hard cap protects RAM if host sends garbage.
  static constexpr size_t MAX_FRAME_SIZE = 8192;
  std::vector<uint8_t> rxFrame;
  bool rxEscape = false;
  bool rxStarted = false;

  // Reusable transmit scratch (avoids per-call heap churn).
  std::vector<uint8_t> txScratch;

  // Single in-flight upload session. Keep simple: one writer at a time.
  struct WriteSession {
    bool active = false;
    uint16_t id = 0;
    uint64_t totalSize = 0;
    uint64_t writtenSize = 0;
    HalFile file;
  };
  WriteSession writeSession;
  uint16_t nextSessionId = 1;

  bool started = false;

  static size_t rxBufferReportedSize;
};
