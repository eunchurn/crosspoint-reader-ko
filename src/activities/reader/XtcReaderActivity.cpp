/**
 * XtcReaderActivity.cpp
 *
 * XTC ebook reader activity implementation
 * Displays pre-rendered XTC pages on e-ink display
 */

#include "XtcReaderActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "XtcReaderChapterSelectionActivity.h"
#include "fontIds.h"

namespace {
constexpr unsigned long skipPageMs = 700;
constexpr unsigned long goHomeMs = 1000;
}  // namespace

void XtcReaderActivity::taskTrampoline(void* param) {
  auto* self = static_cast<XtcReaderActivity*>(param);
  self->displayTaskLoop();
}

void XtcReaderActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  if (!xtc) {
    return;
  }

  renderingMutex = xSemaphoreCreateMutex();

  xtc->setupCacheDir();

  // Load saved progress
  loadProgress();

  // Save current XTC as last opened book
  APP_STATE.openEpubPath = xtc->getPath();
  APP_STATE.saveToFile();

  // Trigger first update
  updateRequired = true;

  xTaskCreate(&XtcReaderActivity::taskTrampoline, "XtcReaderActivityTask",
              4096,               // Stack size (smaller than EPUB since no parsing needed)
              this,               // Parameters
              1,                  // Priority
              &displayTaskHandle  // Task handle
  );
}

void XtcReaderActivity::onExit() {
  ActivityWithSubactivity::onExit();

  // Wait until not rendering to delete task
  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
  xtc.reset();
}

void XtcReaderActivity::loop() {
  // Pass input responsibility to sub activity if exists
  if (subActivity) {
    subActivity->loop();
    return;
  }

  // Enter chapter selection activity
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (xtc && xtc->hasChapters() && !xtc->getChapters().empty()) {
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      exitActivity();
      enterNewActivity(new XtcReaderChapterSelectionActivity(
          this->renderer, this->mappedInput, xtc, currentPage,
          [this] {
            exitActivity();
            updateRequired = true;
          },
          [this](const uint32_t newPage) {
            currentPage = newPage;
            exitActivity();
            updateRequired = true;
          }));
      xSemaphoreGive(renderingMutex);
    }
  }

  // Long press BACK (1s+) goes directly to home
  if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= goHomeMs) {
    onGoHome();
    return;
  }

  // Short press BACK goes to file selection
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && mappedInput.getHeldTime() < goHomeMs) {
    onGoBack();
    return;
  }

  const bool prevReleased = mappedInput.wasReleased(MappedInputManager::Button::PageBack) ||
                            mappedInput.wasReleased(MappedInputManager::Button::Left);
  const bool nextReleased = mappedInput.wasReleased(MappedInputManager::Button::PageForward) ||
                            (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::PAGE_TURN &&
                             mappedInput.wasReleased(MappedInputManager::Button::Power)) ||
                            mappedInput.wasReleased(MappedInputManager::Button::Right);

  if (!prevReleased && !nextReleased) {
    return;
  }

  // Handle end of book
  if (currentPage >= xtc->getPageCount()) {
    currentPage = xtc->getPageCount() - 1;
    updateRequired = true;
    return;
  }

  const bool skipPages = mappedInput.getHeldTime() > skipPageMs;
  const int skipAmount = skipPages ? 10 : 1;

  if (prevReleased) {
    if (currentPage >= static_cast<uint32_t>(skipAmount)) {
      currentPage -= skipAmount;
    } else {
      currentPage = 0;
    }
    updateRequired = true;
  } else if (nextReleased) {
    currentPage += skipAmount;
    if (currentPage >= xtc->getPageCount()) {
      currentPage = xtc->getPageCount();  // Allow showing "End of book"
    }
    updateRequired = true;
  }
}

void XtcReaderActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      renderScreen();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void XtcReaderActivity::renderScreen() {
  if (!xtc) {
    return;
  }

  // Bounds check
  if (currentPage >= xtc->getPageCount()) {
    // Show end of book screen
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, "End of book", true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  renderPage();
  saveProgress();
}

void XtcReaderActivity::renderPage() {
  const uint16_t pageWidth = xtc->getPageWidth();
  const uint16_t pageHeight = xtc->getPageHeight();
  const uint8_t bitDepth = xtc->getBitDepth();

  // Calculate buffer size for one page
  // XTG (1-bit): Row-major, ((width+7)/8) * height bytes
  // XTH (2-bit): Two bit planes, column-major, ((width * height + 7) / 8) * 2 bytes
  size_t pageBufferSize;
  if (bitDepth == 2) {
    pageBufferSize = ((static_cast<size_t>(pageWidth) * pageHeight + 7) / 8) * 2;
  } else {
    pageBufferSize = ((pageWidth + 7) / 8) * pageHeight;
  }

  // For XTCH (2-bit), try to allocate single plane buffer (48KB) if full buffer (96KB) fails
  // This allows streaming rendering with less memory
  uint8_t* pageBuffer = static_cast<uint8_t*>(malloc(pageBufferSize));
  bool useStreamingMode = false;

  if (!pageBuffer && bitDepth == 2) {
    // Try half-size buffer for streaming mode
    const size_t planeSize = pageBufferSize / 2;
    pageBuffer = static_cast<uint8_t*>(malloc(planeSize));
    if (pageBuffer) {
      useStreamingMode = true;
      Serial.printf("[%lu] [XTR] Using streaming mode (plane buffer %lu bytes)\n", millis(), planeSize);
    }
  }

  if (!pageBuffer) {
    Serial.printf("[%lu] [XTR] Failed to allocate page buffer (%lu bytes)\n", millis(), pageBufferSize);
    // Don't display anything - just return to avoid showing garbage
    return;
  }

  // Load page data
  size_t bytesRead;
  if (useStreamingMode) {
    // Streaming mode: will load planes separately later
    bytesRead = 1;  // Placeholder, actual loading happens in render loop
  } else {
    bytesRead = xtc->loadPage(currentPage, pageBuffer, pageBufferSize);
  }

  if (bytesRead == 0) {
    Serial.printf("[%lu] [XTR] Failed to load page %lu\n", millis(), currentPage);
    free(pageBuffer);
    return;
  }

  // Clear screen first
  renderer.clearScreen();

  // Copy page bitmap using GfxRenderer's drawPixel
  // XTC/XTCH pages are pre-rendered with status bar included, so render full page
  const uint16_t maxSrcY = pageHeight;

  if (bitDepth == 2) {
    // XTH 2-bit grayscale format (from spec):
    // - Two sequential bit planes: plane1 (bit1), plane2 (bit2)
    // - Column-major: columns right→left, 8 vertical pixels/byte, MSB=top
    // - pixelValue = (bit1 << 1) | bit2: 0=White, 1=DarkGrey, 2=LightGrey, 3=Black
    // - plane1 → cmd 0x24 (BW RAM), plane2 → cmd 0x26 (RED RAM)
    //
    // XTH is pre-rendered for 480x800 portrait display.
    // E-paper RAM is 800x480 with 8 horizontal pixels/byte.
    // Coordinate transform needed: XTH(x,y) → RAM(y, 479-x)

    const size_t planeSize = (static_cast<size_t>(pageWidth) * pageHeight + 7) / 8;
    const size_t colBytes = (pageHeight + 7) / 8;  // 100 bytes per column for 800 height

    uint8_t* frameBuffer = renderer.getFrameBuffer();
    const size_t fbSize = GfxRenderer::getBufferSize();

    // Transform XTH plane (column-major) to framebuffer (row-major with rotation)
    // LUT: 00=no change, 01=light gray, 10=gray, 11=dark gray
    // Higher value = darker, so bit 1 = active (contributes to darker)
    // XTH: col = width-1-x, byte = y/8, bit = 7-(y%8)
    // FB:  row = 479-x, byte = y/8, bit = 7-(y%8)
    auto transformXthPlaneToFb = [&](const uint8_t* xthPlane) {
      memset(frameBuffer, 0x00, fbSize);  // Start with all 0s (no gray effect)

      for (uint16_t xthX = 0; xthX < pageWidth; xthX++) {
        // XTH column index (right to left)
        const size_t xthCol = pageWidth - 1 - xthX;
        const size_t xthColBase = xthCol * colBytes;

        // FB row (after 90° rotation: xthX → fbRow)
        const uint16_t fbRow = EInkDisplay::DISPLAY_HEIGHT - 1 - xthX;
        const size_t fbRowBase = fbRow * EInkDisplay::DISPLAY_WIDTH_BYTES;

        for (uint16_t xthY = 0; xthY < pageHeight; xthY++) {
          // Read XTH bit
          const size_t xthByteIdx = xthColBase + (xthY / 8);
          const uint8_t xthBitPos = 7 - (xthY % 8);
          const uint8_t xthBit = (xthPlane[xthByteIdx] >> xthBitPos) & 1;

          // FB column (after 90° rotation: xthY → fbCol)
          const uint16_t fbCol = xthY;
          const size_t fbByteIdx = fbRowBase + (fbCol / 8);
          const uint8_t fbBitPos = 7 - (fbCol % 8);

          // XTH bit 1 = this plane contributes to darker pixel
          // LUT bit 1 = active (darker), so copy directly
          if (xthBit == 1) {
            frameBuffer[fbByteIdx] |= (1 << fbBitPos);  // Set to 1 (active)
          }
          // else: leave as 0 (no effect)
        }
      }
    };

    if (!useStreamingMode) {
      // Normal mode: 96KB buffer has both planes
      const uint8_t* plane1 = pageBuffer;              // bit1 → BW RAM (0x24)
      const uint8_t* plane2 = pageBuffer + planeSize;  // bit2 → RED RAM (0x26)

      Serial.printf("[%lu] [XTR] Grayscale render (clear → BW → grayscale)\n", millis());

      // Step 1: Clear screen to white with HALF_REFRESH to remove ghosting
      renderer.clearScreen(0xFF);
      renderer.displayBuffer(EInkDisplay::HALF_REFRESH);

      // Step 2: Build and display BW image (black where any non-white pixel)
      memset(frameBuffer, 0xFF, fbSize);
      for (uint16_t xthX = 0; xthX < pageWidth; xthX++) {
        const size_t xthCol = pageWidth - 1 - xthX;
        const size_t xthColBase = xthCol * colBytes;
        const uint16_t fbRow = EInkDisplay::DISPLAY_HEIGHT - 1 - xthX;
        const size_t fbRowBase = fbRow * EInkDisplay::DISPLAY_WIDTH_BYTES;

        for (uint16_t xthY = 0; xthY < pageHeight; xthY++) {
          const size_t xthByteIdx = xthColBase + (xthY / 8);
          const uint8_t xthBitPos = 7 - (xthY % 8);
          const uint8_t bit1 = (plane1[xthByteIdx] >> xthBitPos) & 1;
          const uint8_t bit2 = (plane2[xthByteIdx] >> xthBitPos) & 1;

          // Any non-white pixel (bit1 or bit2 is 1) → black in BW
          if (bit1 || bit2) {
            const uint16_t fbCol = xthY;
            const size_t fbByteIdx = fbRowBase + (fbCol / 8);
            const uint8_t fbBitPos = 7 - (fbCol % 8);
            frameBuffer[fbByteIdx] &= ~(1 << fbBitPos);
          }
        }
      }
      renderer.displayBuffer();  // Fast refresh for BW

      // Step 3: Store BW buffer for restoration
      renderer.storeBwBuffer();

      // Step 4: Render grayscale planes
      // XTH: pixelValue = (bit1 << 1) | bit2
      //   0=White, 1=DarkGrey, 2=LightGrey, 3=Black
      // LUT: (MSB << 1) | LSB
      //   00=white, 01=light gray, 10=gray, 11=dark gray
      transformXthPlaneToFb(plane2);
      renderer.copyGrayscaleLsbBuffers();  // plane2 (bit2) → LSB → BW RAM (0x24)

      transformXthPlaneToFb(plane1);
      renderer.copyGrayscaleMsbBuffers();  // plane1 (bit1) → MSB → RED RAM (0x26)

      // Step 5: Display grayscale
      renderer.displayGrayBuffer();

      // Step 6: Restore BW buffer
      renderer.restoreBwBuffer();

    } else {
      // Streaming mode: 48KB buffer, load each plane separately for 2-bit grayscale
      Serial.printf("[%lu] [XTR] Streaming mode: 2-bit grayscale (clear → BW → grayscale)\n", millis());

      // Lambda to load a specific plane via streaming
      auto loadPlane = [&](size_t planeOffset) -> bool {
        size_t loaded = 0;
        xtc->loadPageStreaming(
            currentPage,
            [&](const uint8_t* data, size_t size, size_t offset) {
              // Only copy data from the target plane
              if (offset >= planeOffset && offset < planeOffset + planeSize) {
                const size_t planeLocalOffset = offset - planeOffset;
                const size_t copySize = std::min(size, planeSize - planeLocalOffset);
                memcpy(pageBuffer + planeLocalOffset, data, copySize);
                loaded = planeLocalOffset + copySize;
              } else if (offset < planeOffset && offset + size > planeOffset) {
                // Data spans into our plane
                const size_t skipBytes = planeOffset - offset;
                const size_t copySize = std::min(size - skipBytes, planeSize);
                memcpy(pageBuffer, data + skipBytes, copySize);
                loaded = copySize;
              }
            },
            4096);
        return loaded >= planeSize;
      };

      // Step 1: Clear screen to white with HALF_REFRESH
      renderer.clearScreen(0xFF);
      renderer.displayBuffer(EInkDisplay::HALF_REFRESH);

      // Load plane1
      if (!loadPlane(0)) {
        Serial.printf("[%lu] [XTR] Streaming plane1 incomplete\n", millis());
        free(pageBuffer);
        return;
      }

      // Step 2: Build BW image from plane1 (approximate)
      memset(frameBuffer, 0xFF, fbSize);
      for (uint16_t xthX = 0; xthX < pageWidth; xthX++) {
        const size_t xthCol = pageWidth - 1 - xthX;
        const size_t xthColBase = xthCol * colBytes;
        const uint16_t fbRow = EInkDisplay::DISPLAY_HEIGHT - 1 - xthX;
        const size_t fbRowBase = fbRow * EInkDisplay::DISPLAY_WIDTH_BYTES;

        for (uint16_t xthY = 0; xthY < pageHeight; xthY++) {
          const size_t xthByteIdx = xthColBase + (xthY / 8);
          const uint8_t xthBitPos = 7 - (xthY % 8);
          const uint8_t bit1 = (pageBuffer[xthByteIdx] >> xthBitPos) & 1;

          if (bit1) {
            const uint16_t fbCol = xthY;
            const size_t fbByteIdx = fbRowBase + (fbCol / 8);
            const uint8_t fbBitPos = 7 - (fbCol % 8);
            frameBuffer[fbByteIdx] &= ~(1 << fbBitPos);
          }
        }
      }
      renderer.displayBuffer();  // Fast refresh for BW

      // Store BW buffer
      renderer.storeBwBuffer();

      // Step 3: Transform plane1 for MSB
      transformXthPlaneToFb(pageBuffer);
      renderer.copyGrayscaleMsbBuffers();  // plane1 (bit1) → MSB → RED RAM (0x26)

      // Load plane2
      if (!loadPlane(planeSize)) {
        Serial.printf("[%lu] [XTR] Streaming plane2 incomplete\n", millis());
        // Continue with just plane1
      } else {
        // Transform plane2 for LSB
        transformXthPlaneToFb(pageBuffer);
      }
      renderer.copyGrayscaleLsbBuffers();  // plane2 (bit2) → LSB → BW RAM (0x24)

      // Display grayscale
      renderer.displayGrayBuffer();

      // Restore BW buffer
      renderer.restoreBwBuffer();
    }

    free(pageBuffer);

    Serial.printf("[%lu] [XTR] Rendered page %lu/%lu (2-bit grayscale)\n", millis(), currentPage + 1,
                  xtc->getPageCount());
    return;
  } else {
    // 1-bit mode: 8 pixels per byte, MSB first
    const size_t srcRowBytes = (pageWidth + 7) / 8;  // 60 bytes for 480 width

    for (uint16_t srcY = 0; srcY < maxSrcY; srcY++) {
      const size_t srcRowStart = srcY * srcRowBytes;

      for (uint16_t srcX = 0; srcX < pageWidth; srcX++) {
        // Read source pixel (MSB first, bit 7 = leftmost pixel)
        const size_t srcByte = srcRowStart + srcX / 8;
        const size_t srcBit = 7 - (srcX % 8);
        const bool isBlack = !((pageBuffer[srcByte] >> srcBit) & 1);  // XTC: 0 = black, 1 = white

        if (isBlack) {
          renderer.drawPixel(srcX, srcY, true);
        }
      }
    }
  }
  // White pixels are already cleared by clearScreen()

  free(pageBuffer);

  // XTC pages already have status bar pre-rendered, no need to add our own

  // Display with appropriate refresh
  if (pagesUntilFullRefresh <= 1) {
    renderer.displayBuffer(EInkDisplay::HALF_REFRESH);
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  } else {
    renderer.displayBuffer();
    pagesUntilFullRefresh--;
  }

  Serial.printf("[%lu] [XTR] Rendered page %lu/%lu (%u-bit)\n", millis(), currentPage + 1, xtc->getPageCount(),
                bitDepth);
}

void XtcReaderActivity::saveProgress() const {
  FsFile f;
  if (SdMan.openFileForWrite("XTR", xtc->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    data[0] = currentPage & 0xFF;
    data[1] = (currentPage >> 8) & 0xFF;
    data[2] = (currentPage >> 16) & 0xFF;
    data[3] = (currentPage >> 24) & 0xFF;
    f.write(data, 4);
    f.close();
  }
}

void XtcReaderActivity::loadProgress() {
  FsFile f;
  if (SdMan.openFileForRead("XTR", xtc->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    if (f.read(data, 4) == 4) {
      currentPage = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
      Serial.printf("[%lu] [XTR] Loaded progress: page %lu\n", millis(), currentPage);

      // Validate page number
      if (currentPage >= xtc->getPageCount()) {
        currentPage = 0;
      }
    }
    f.close();
  }
}
