#include "SerialFileBridge.h"

#include <Arduino.h>
#include <HardwareSerial.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>

namespace {
constexpr uint8_t SLIP_END = 0xC0;
constexpr uint8_t SLIP_ESC = 0xDB;
constexpr uint8_t SLIP_ESC_END = 0xDC;
constexpr uint8_t SLIP_ESC_ESC = 0xDD;
}  // namespace

size_t SerialFileBridge::rxBufferReportedSize = 0;

SerialFileBridge& SerialFileBridge::getInstance() {
  static SerialFileBridge instance;
  return instance;
}

void SerialFileBridge::begin() {
  if (started) return;
  started = true;

  // Embed RX-buffer-resize result in the PING tag so the host can verify
  // the resize actually took effect ("rx16384" vs the 256-byte default).
  snprintf(pingSuffix, sizeof(pingSuffix), "rx%u", static_cast<unsigned>(rxBufferReportedSize));
  cmdCtx.pingTagSuffix = pingSuffix;
  cmdCtx.sink = {.ctx = this, .send = &SerialFileBridge::sinkSend};

  // 6KB stack: SD ops + buffers. SdFat operations can use ~1.5KB stack.
  // RX buffer sizing happens in main.cpp before Serial.begin() because
  // HWCDC.setRxBufferSize() is a no-op once the CDC interface is running.
  xTaskCreate(&SerialFileBridge::taskTrampoline, "SerialBridge", 6144, this, 1, nullptr);
  LOG_DBG("SBR", "SerialFileBridge task started");
}

void SerialFileBridge::taskTrampoline(void* arg) { static_cast<SerialFileBridge*>(arg)->taskLoop(); }

void SerialFileBridge::taskLoop() {
  rxFrame.reserve(2048);
  cmdCtx.txScratch.reserve(2048);
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
      FileCommands::dispatch(cmdCtx, rxFrame.data(), rxFrame.size());
    }
    rxFrame.clear();
    rxEscape = false;
    rxStarted = true;
    return;
  }

  if (!rxStarted) return;  // log text outside frames

  if (rxEscape) {
    if (b == SLIP_ESC_END)
      b = SLIP_END;
    else if (b == SLIP_ESC_ESC)
      b = SLIP_ESC;
    rxEscape = false;
  } else if (b == SLIP_ESC) {
    rxEscape = true;
    return;
  }

  if (rxFrame.size() >= MAX_FRAME_SIZE) {
    rxFrame.clear();
    rxStarted = false;
    return;
  }
  rxFrame.push_back(b);
}

void SerialFileBridge::sinkSend(void* ctx, const uint8_t* data, size_t len) {
  static_cast<SerialFileBridge*>(ctx)->writeFrame(data, len);
}

void SerialFileBridge::writeFrame(const uint8_t* data, size_t len) {
  // RFC1055-style: bracket with leading END too so receivers recover from
  // any line noise that left them mid-frame.
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
