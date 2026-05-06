#include "SerialFileBridge.h"

#include <Arduino.h>
#include <HardwareSerial.h>
#include <Logging.h>
#include <common/FsApiConstants.h>
#include <esp_system.h>

#include <cstring>

namespace {

constexpr uint8_t SLIP_END = 0xC0;
constexpr uint8_t SLIP_ESC = 0xDB;
constexpr uint8_t SLIP_ESC_END = 0xDC;
constexpr uint8_t SLIP_ESC_ESC = 0xDD;

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

// 1024 B per chunk balances: small enough that USB-Serial-JTAG truncations
// stay in single-digit-percent range, large enough that round-trip count
// stays manageable for multi-MB uploads.
constexpr size_t MAX_READ_CHUNK = 1024;
constexpr size_t MAX_WRITE_CHUNK = 1024;

inline uint16_t readU16LE(const uint8_t* p) { return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8); }

inline uint32_t readU32LE(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

inline uint64_t readU64LE(const uint8_t* p) {
  uint64_t lo = readU32LE(p);
  uint64_t hi = readU32LE(p + 4);
  return lo | (hi << 32);
}

inline void writeU16LE(uint8_t* p, uint16_t v) {
  p[0] = v & 0xFF;
  p[1] = (v >> 8) & 0xFF;
}

inline void writeU32LE(uint8_t* p, uint32_t v) {
  p[0] = v & 0xFF;
  p[1] = (v >> 8) & 0xFF;
  p[2] = (v >> 16) & 0xFF;
  p[3] = (v >> 24) & 0xFF;
}

inline void writeU64LE(uint8_t* p, uint64_t v) {
  writeU32LE(p, v & 0xFFFFFFFFu);
  writeU32LE(p + 4, (v >> 32) & 0xFFFFFFFFu);
}

// Pop a uint16-length-prefixed string from a payload cursor. Returns false on
// truncation. Caller advances the cursor on success.
bool popLString(const uint8_t*& p, size_t& remaining, std::string& out) {
  if (remaining < 2) return false;
  uint16_t len = readU16LE(p);
  p += 2;
  remaining -= 2;
  if (remaining < len) return false;
  out.assign(reinterpret_cast<const char*>(p), len);
  p += len;
  remaining -= len;
  return true;
}

}  // namespace

size_t SerialFileBridge::rxBufferReportedSize = 0;

SerialFileBridge& SerialFileBridge::getInstance() {
  static SerialFileBridge instance;
  return instance;
}

void SerialFileBridge::begin() {
  if (started) return;
  started = true;

  // 6KB stack: SD ops + buffers. SdFat operations can use ~1.5KB stack.
  // RX buffer sizing happens in main.cpp before Serial.begin() because
  // HWCDC.setRxBufferSize() is a no-op once the CDC interface is running.
  xTaskCreate(&SerialFileBridge::taskTrampoline, "SerialBridge", 6144, this, 1, nullptr);
  LOG_DBG("SBR", "SerialFileBridge task started");
}

void SerialFileBridge::taskTrampoline(void* arg) { static_cast<SerialFileBridge*>(arg)->taskLoop(); }

void SerialFileBridge::taskLoop() {
  rxFrame.reserve(2048);
  txScratch.reserve(2048);
  uint32_t lastByteMs = 0;

  while (true) {
    int n = logSerial.available();
    if (n <= 0) {
      // If a frame has been accumulating for too long, the closing END byte
      // probably got dropped by USB-Serial-JTAG. Reset so the next frame's
      // leading END can re-sync without waiting for MAX_FRAME_SIZE.
      if (rxStarted && !rxFrame.empty() && (millis() - lastByteMs) > 200) {
        rxFrame.clear();
        rxEscape = false;
        rxStarted = false;
      }
      vTaskDelay(pdMS_TO_TICKS(2));
      continue;
    }
    int processed = 0;
    while (n-- > 0) {
      int c = logSerial.read();
      if (c < 0) break;
      feedByte(static_cast<uint8_t>(c));
      lastByteMs = millis();
      if (++processed >= 16) {
        processed = 0;
        taskYIELD();
      }
    }
  }
}

void SerialFileBridge::feedByte(uint8_t b) {
  if (b == SLIP_END) {
    if (rxStarted && !rxFrame.empty()) {
      handleFrame();
    }
    rxFrame.clear();
    rxEscape = false;
    rxStarted = true;
    return;
  }

  if (!rxStarted) {
    // Bytes outside any frame are ignored. This is how log lines (text)
    // coexist with framed commands on the same wire.
    return;
  }

  if (rxEscape) {
    if (b == SLIP_ESC_END)
      b = SLIP_END;
    else if (b == SLIP_ESC_ESC)
      b = SLIP_ESC;
    // Any other byte after ESC is malformed; we still accept it as literal
    // rather than dropping the whole frame.
    rxEscape = false;
  } else if (b == SLIP_ESC) {
    rxEscape = true;
    return;
  }

  if (rxFrame.size() >= MAX_FRAME_SIZE) {
    // Overrun: discard frame, wait for next END.
    rxFrame.clear();
    rxStarted = false;
    return;
  }
  rxFrame.push_back(b);
}

void SerialFileBridge::handleFrame() {
  if (rxFrame.size() < 3) return;  // need opcode + reqid

  uint8_t opcode = rxFrame[0];
  uint16_t reqid = readU16LE(rxFrame.data() + 1);
  const uint8_t* payload = rxFrame.data() + 3;
  size_t plen = rxFrame.size() - 3;

  dispatch(opcode, reqid, payload, plen);
}

void SerialFileBridge::dispatch(uint8_t opcode, uint16_t reqid, const uint8_t* payload, size_t len) {
  switch (opcode) {
    case OP_PING:
      handlePing(reqid);
      break;
    case OP_LIST_DIR:
      handleListDir(reqid, payload, len);
      break;
    case OP_STAT:
      handleStat(reqid, payload, len);
      break;
    case OP_READ:
      handleRead(reqid, payload, len);
      break;
    case OP_WRITE_BEGIN:
      handleWriteBegin(reqid, payload, len);
      break;
    case OP_WRITE_CHUNK:
      handleWriteChunk(reqid, payload, len);
      break;
    case OP_WRITE_END:
      handleWriteEnd(reqid, payload, len);
      break;
    case OP_DELETE:
      handleDelete(reqid, payload, len);
      break;
    case OP_MKDIR:
      handleMkdir(reqid, payload, len);
      break;
    case OP_RENAME:
      handleRename(reqid, payload, len);
      break;
    default:
      sendError(reqid, ERR_BAD_REQUEST, "unknown opcode");
      break;
  }
}

void SerialFileBridge::handlePing(uint16_t reqid) {
  // Embed the RX-buffer-resize result in the firmware tag so it is visible
  // in the host's connection badge without changing the wire format. If the
  // resize failed we will see "rx0" and know the default 256B is in effect.
  char tag[64];
  const int n = snprintf(tag, sizeof(tag), "crosspoint-reader rx%u", static_cast<unsigned>(rxBufferReportedSize));
  const size_t tagLen = n > 0 ? static_cast<size_t>(n) : 0;
  txScratch.resize(4 + tagLen + 4);
  writeU32LE(txScratch.data(), tagLen);
  memcpy(txScratch.data() + 4, tag, tagLen);
  writeU32LE(txScratch.data() + 4 + tagLen, ESP.getFreeHeap());
  sendOk(OP_PING, reqid, txScratch.data(), txScratch.size());
}

void SerialFileBridge::handleListDir(uint16_t reqid, const uint8_t* p, size_t n) {
  std::string path;
  if (!popLString(p, n, path)) {
    sendError(reqid, ERR_BAD_REQUEST, "bad list_dir args");
    return;
  }
  if (path.empty()) path = "/";

  HalFile root = Storage.open(path.c_str());
  if (!root) {
    sendError(reqid, ERR_NOT_FOUND, "directory not found");
    return;
  }
  if (!root.isDirectory()) {
    root.close();
    sendError(reqid, ERR_NOT_DIR, "not a directory");
    return;
  }

  // Build entries into txScratch with a placeholder count, then patch.
  txScratch.clear();
  txScratch.resize(2);  // count placeholder
  uint16_t count = 0;

  HalFile child = root.openNextFile();
  char nameBuf[256];
  while (child) {
    size_t nameLen = child.getName(nameBuf, sizeof(nameBuf));
    if (nameLen > 0 && nameLen <= 255) {
      // Skip dot entries and hidden cache directory names if desired? Keep
      // them visible — host UI decides what to show.
      bool isDir = child.isDirectory();
      uint64_t size = isDir ? 0 : child.size();
      // Reserve and append: type(1) + size(8) + name_len(2) + name
      size_t off = txScratch.size();
      txScratch.resize(off + 1 + 8 + 2 + nameLen);
      txScratch[off] = isDir ? 1 : 0;
      writeU64LE(txScratch.data() + off + 1, size);
      writeU16LE(txScratch.data() + off + 9, static_cast<uint16_t>(nameLen));
      memcpy(txScratch.data() + off + 11, nameBuf, nameLen);
      ++count;
    }
    child.close();
    child = root.openNextFile();
  }
  root.close();

  writeU16LE(txScratch.data(), count);
  sendOk(OP_LIST_DIR, reqid, txScratch.data(), txScratch.size());
}

void SerialFileBridge::handleStat(uint16_t reqid, const uint8_t* p, size_t n) {
  std::string path;
  if (!popLString(p, n, path)) {
    sendError(reqid, ERR_BAD_REQUEST, "bad stat args");
    return;
  }
  HalFile f = Storage.open(path.c_str());
  if (!f) {
    sendError(reqid, ERR_NOT_FOUND, "not found");
    return;
  }
  uint8_t type = f.isDirectory() ? 1 : 0;
  uint64_t size = type == 1 ? 0 : f.size();
  f.close();

  uint8_t buf[1 + 8];
  buf[0] = type;
  writeU64LE(buf + 1, size);
  sendOk(OP_STAT, reqid, buf, sizeof(buf));
}

void SerialFileBridge::handleRead(uint16_t reqid, const uint8_t* p, size_t n) {
  std::string path;
  if (!popLString(p, n, path)) {
    sendError(reqid, ERR_BAD_REQUEST, "bad read args");
    return;
  }
  if (n < 12) {
    sendError(reqid, ERR_BAD_REQUEST, "read missing offset/length");
    return;
  }
  uint64_t offset = readU64LE(p);
  uint32_t length = readU32LE(p + 8);
  if (length > MAX_READ_CHUNK) length = MAX_READ_CHUNK;

  HalFile f = Storage.open(path.c_str());
  if (!f) {
    sendError(reqid, ERR_NOT_FOUND, "not found");
    return;
  }
  if (f.isDirectory()) {
    f.close();
    sendError(reqid, ERR_IS_DIR, "is a directory");
    return;
  }
  uint64_t total = f.size();
  if (offset > total) offset = total;
  if (offset + length > total) length = static_cast<uint32_t>(total - offset);

  if (!f.seekSet(static_cast<size_t>(offset))) {
    f.close();
    sendError(reqid, ERR_IO, "seek failed");
    return;
  }

  txScratch.resize(4 + length);
  writeU32LE(txScratch.data(), length);
  if (length > 0) {
    int got = f.read(txScratch.data() + 4, length);
    if (got < 0) {
      f.close();
      sendError(reqid, ERR_IO, "read failed");
      return;
    }
    txScratch.resize(4 + static_cast<size_t>(got));
    writeU32LE(txScratch.data(), static_cast<uint32_t>(got));
  }
  f.close();
  sendOk(OP_READ, reqid, txScratch.data(), txScratch.size());
}

void SerialFileBridge::abortActiveWrite() {
  if (writeSession.active) {
    writeSession.file.close();
    writeSession.active = false;
    writeSession.id = 0;
  }
}

void SerialFileBridge::handleWriteBegin(uint16_t reqid, const uint8_t* p, size_t n) {
  const size_t origN = n;
  std::string path;
  if (!popLString(p, n, path)) {
    char msg[64];
    snprintf(msg, sizeof(msg), "bad write_begin args (got %u bytes)", static_cast<unsigned>(origN));
    sendError(reqid, ERR_BAD_REQUEST, msg);
    return;
  }
  if (n < 8) {
    char msg[96];
    snprintf(msg, sizeof(msg), "missing total_size: payload %u, path '%s'(%u), left %u", static_cast<unsigned>(origN),
             path.c_str(), static_cast<unsigned>(path.size()), static_cast<unsigned>(n));
    sendError(reqid, ERR_BAD_REQUEST, msg);
    return;
  }
  uint64_t totalSize = readU64LE(p);

  if (writeSession.active) {
    // Host should have completed or aborted previous session. Tear it down to
    // avoid resource leak rather than refuse — the previous host may have
    // disconnected.
    abortActiveWrite();
  }

  HalFile f = Storage.open(path.c_str(), O_WRITE | O_CREAT | O_TRUNC);
  if (!f) {
    sendError(reqid, ERR_IO, "cannot open for write");
    return;
  }

  writeSession.active = true;
  writeSession.id = nextSessionId++;
  if (nextSessionId == 0) nextSessionId = 1;
  writeSession.totalSize = totalSize;
  writeSession.writtenSize = 0;
  writeSession.file = std::move(f);

  uint8_t buf[2];
  writeU16LE(buf, writeSession.id);
  sendOk(OP_WRITE_BEGIN, reqid, buf, sizeof(buf));
}

void SerialFileBridge::handleWriteChunk(uint16_t reqid, const uint8_t* p, size_t n) {
  if (n < 6) {
    char msg[64];
    snprintf(msg, sizeof(msg), "bad write_chunk args (got %u bytes)", static_cast<unsigned>(n));
    sendError(reqid, ERR_BAD_REQUEST, msg);
    return;
  }
  uint16_t sid = readU16LE(p);
  uint32_t chunkLen = readU32LE(p + 2);
  if (n < 6u + chunkLen) {
    // Include the last 8 received bytes (hex) so the host can compare with
    // the bytes it sent and tell if loss is at the tail or the middle.
    char msg[160];
    char tail[3 * 8 + 1];
    tail[0] = '\0';
    const size_t tailStart = n >= 8 ? n - 8 : 0;
    for (size_t i = tailStart, w = 0; i < n && w + 3 < sizeof(tail); ++i, w += 3) {
      snprintf(tail + w, 4, "%02X ", p[i]);
    }
    snprintf(msg, sizeof(msg), "truncated: claimed %u payload %u expected %u tail[%u..]: %s",
             static_cast<unsigned>(chunkLen), static_cast<unsigned>(n), static_cast<unsigned>(6u + chunkLen),
             static_cast<unsigned>(tailStart), tail);
    sendError(reqid, ERR_BAD_REQUEST, msg);
    return;
  }
  if (chunkLen > MAX_WRITE_CHUNK) {
    sendError(reqid, ERR_BAD_REQUEST, "chunk too large");
    return;
  }
  if (!writeSession.active || writeSession.id != sid) {
    sendError(reqid, ERR_NO_SESSION, "no matching session");
    return;
  }

  size_t written = writeSession.file.write(p + 6, chunkLen);
  if (written != chunkLen) {
    abortActiveWrite();
    sendError(reqid, ERR_IO, "short write (disk full?)");
    return;
  }
  writeSession.writtenSize += chunkLen;

  uint8_t buf[8];
  writeU64LE(buf, writeSession.writtenSize);
  sendOk(OP_WRITE_CHUNK, reqid, buf, sizeof(buf));
}

void SerialFileBridge::handleWriteEnd(uint16_t reqid, const uint8_t* p, size_t n) {
  if (n < 2) {
    sendError(reqid, ERR_BAD_REQUEST, "bad write_end args");
    return;
  }
  uint16_t sid = readU16LE(p);
  if (!writeSession.active || writeSession.id != sid) {
    sendError(reqid, ERR_NO_SESSION, "no matching session");
    return;
  }
  writeSession.file.flush();
  writeSession.file.close();
  writeSession.active = false;
  writeSession.id = 0;
  sendOk(OP_WRITE_END, reqid, nullptr, 0);
}

void SerialFileBridge::handleDelete(uint16_t reqid, const uint8_t* p, size_t n) {
  std::string path;
  if (!popLString(p, n, path)) {
    sendError(reqid, ERR_BAD_REQUEST, "bad delete args");
    return;
  }
  HalFile probe = Storage.open(path.c_str());
  if (!probe) {
    sendError(reqid, ERR_NOT_FOUND, "not found");
    return;
  }
  bool isDir = probe.isDirectory();
  probe.close();

  bool ok = isDir ? Storage.removeDir(path.c_str()) : Storage.remove(path.c_str());
  if (!ok) {
    sendError(reqid, ERR_IO, "delete failed");
    return;
  }
  sendOk(OP_DELETE, reqid, nullptr, 0);
}

void SerialFileBridge::handleMkdir(uint16_t reqid, const uint8_t* p, size_t n) {
  std::string path;
  if (!popLString(p, n, path)) {
    sendError(reqid, ERR_BAD_REQUEST, "bad mkdir args");
    return;
  }
  if (!Storage.mkdir(path.c_str(), true)) {
    sendError(reqid, ERR_IO, "mkdir failed");
    return;
  }
  sendOk(OP_MKDIR, reqid, nullptr, 0);
}

void SerialFileBridge::handleRename(uint16_t reqid, const uint8_t* p, size_t n) {
  std::string src;
  std::string dst;
  if (!popLString(p, n, src) || !popLString(p, n, dst)) {
    sendError(reqid, ERR_BAD_REQUEST, "bad rename args");
    return;
  }
  if (!Storage.rename(src.c_str(), dst.c_str())) {
    sendError(reqid, ERR_IO, "rename failed");
    return;
  }
  sendOk(OP_RENAME, reqid, nullptr, 0);
}

void SerialFileBridge::sendOk(uint8_t opcode, uint16_t reqid, const uint8_t* payload, size_t len) {
  // [opcode|0x80][reqid LE][payload]
  std::vector<uint8_t> frame;
  frame.reserve(3 + len);
  frame.push_back(opcode | OP_RESPONSE_BIT);
  frame.push_back(reqid & 0xFF);
  frame.push_back((reqid >> 8) & 0xFF);
  if (len > 0 && payload != nullptr) {
    frame.insert(frame.end(), payload, payload + len);
  }
  writeFrame(frame.data(), frame.size());
}

void SerialFileBridge::sendError(uint16_t reqid, uint8_t code, const char* msg) {
  size_t mlen = msg ? strlen(msg) : 0;
  if (mlen > 255) mlen = 255;
  std::vector<uint8_t> frame;
  frame.reserve(3 + 1 + 2 + mlen);
  frame.push_back(OP_ERROR);
  frame.push_back(reqid & 0xFF);
  frame.push_back((reqid >> 8) & 0xFF);
  frame.push_back(code);
  frame.push_back(mlen & 0xFF);
  frame.push_back((mlen >> 8) & 0xFF);
  if (mlen > 0) frame.insert(frame.end(), msg, msg + mlen);
  writeFrame(frame.data(), frame.size());
}

void SerialFileBridge::writeFrame(const uint8_t* data, size_t len) {
  // Bracket with a leading END too. RFC1055 recommends it to recover from
  // line noise that may have left the receiver mid-frame.
  logSerial.write(SLIP_END);
  for (size_t i = 0; i < len; ++i) {
    uint8_t b = data[i];
    if (b == SLIP_END) {
      uint8_t pair[2] = {SLIP_ESC, SLIP_ESC_END};
      logSerial.write(pair, 2);
    } else if (b == SLIP_ESC) {
      uint8_t pair[2] = {SLIP_ESC, SLIP_ESC_ESC};
      logSerial.write(pair, 2);
    } else {
      logSerial.write(b);
    }
  }
  logSerial.write(SLIP_END);
  logSerial.flush();
}
