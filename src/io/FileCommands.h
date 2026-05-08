#pragma once

#include <HalStorage.h>

#include <cstdint>
#include <vector>

// Shared file-operation command handlers used by both:
//   - SerialFileBridge (USB-CDC, SLIP-framed)
//   - CloudClient    (WebSocket, native-framed)
//
// Wire payload (post-transport-decoding) is identical on both:
//   [u8 opcode][u16 reqid LE][per-opcode payload]
// Response opcode = request opcode | 0x80; errors use opcode 0xFF.
namespace FileCommands {

constexpr uint8_t OP_PING = 0x01;
constexpr uint8_t OP_LIST_DIR = 0x02;
constexpr uint8_t OP_STAT = 0x03;
constexpr uint8_t OP_READ = 0x04;
constexpr uint8_t OP_WRITE_BEGIN = 0x05;
constexpr uint8_t OP_WRITE_CHUNK = 0x06;
constexpr uint8_t OP_WRITE_END = 0x07;
constexpr uint8_t OP_DELETE = 0x08;
constexpr uint8_t OP_MKDIR = 0x09;
constexpr uint8_t OP_RENAME = 0x0A;
constexpr uint8_t OP_ERROR = 0xFF;
constexpr uint8_t OP_RESPONSE_BIT = 0x80;

constexpr uint8_t ERR_BAD_REQUEST = 0x01;
constexpr uint8_t ERR_NOT_FOUND = 0x02;
constexpr uint8_t ERR_IO = 0x03;
constexpr uint8_t ERR_BUSY = 0x04;
constexpr uint8_t ERR_NO_SESSION = 0x05;
constexpr uint8_t ERR_NOT_DIR = 0x06;
constexpr uint8_t ERR_IS_DIR = 0x07;
constexpr uint8_t ERR_OOM = 0x08;

// 2 KiB per chunk: each chunk is one full RTT (browser → cloud → device →
// cloud → browser), so throughput is ~chunkSize / RTT. Bigger is faster
// but the transient peak (WS RX buffer + scratch + SD write) has to fit
// into MaxAlloc, which dips below 3 KiB during cloud uploads on a loaded
// reader. 4 KiB caused mid-upload disconnects (allocation failure inside
// the WS lib closes the socket); 2 KiB sits comfortably under that floor
// at the cost of halving throughput.
constexpr size_t MAX_READ_CHUNK = 2048;
constexpr size_t MAX_WRITE_CHUNK = 2048;

// Output frame sink. Receives a raw response frame (opcode|RESPONSE_BIT,
// reqid LE, payload). The sink is responsible for any transport framing.
struct FrameSink {
  void* ctx = nullptr;
  void (*send)(void* ctx, const uint8_t* data, size_t len) = nullptr;
};

struct WriteSession {
  bool active = false;
  uint16_t id = 0;
  uint64_t totalSize = 0;
  uint64_t writtenSize = 0;
  HalFile file;
};

// Per-transport state. Each transport (SerialFileBridge, CloudClient) owns
// one Context. Not thread-safe — give each transport its own.
struct Context {
  FrameSink sink;
  WriteSession writeSession;
  uint16_t nextSessionId = 1;
  std::vector<uint8_t> txScratch;
  // Optional firmware-tag suffix returned in PING (e.g. "rxNNNN" for
  // SerialFileBridge to expose the USB-CDC RX queue size).
  const char* pingTagSuffix = nullptr;
};

// Process a single decoded request frame. The frame argument is the full
// request: [opcode][reqid LE][payload]. Sends a response (or error) via the
// context's sink. Length must include the 3-byte header.
void dispatch(Context& ctx, const uint8_t* frame, size_t frameLen);

// Drop any in-flight write session (closes the open SD file handle).
// Call this when the underlying transport (cloud WS, USB CDC) disconnects
// mid-upload — without it the leaked HalFile keeps an SdFat sector
// buffer pinned, slowly bleeding heap across reconnect cycles.
void reset(Context& ctx);

}  // namespace FileCommands
