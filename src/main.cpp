#include <Arduino.h>
#include <EInkDisplay.h>
#include <Epub.h>
#include <GfxRenderer.h>
#include <InputManager.h>
#include <SD.h>
#include <SPI.h>
#include <builtinFonts/bookerly_2b.h>
#include <builtinFonts/bookerly_bold_2b.h>
#include <builtinFonts/bookerly_bold_italic_2b.h>
#include <builtinFonts/bookerly_italic_2b.h>
#include <builtinFonts/d2coding_14.h>
#include <builtinFonts/eulyoo_2b.h>
#include <builtinFonts/eulyoo_semibold_2b.h>
#include <builtinFonts/pixelarial14.h>
#include <builtinFonts/pretendard_8.h>
#include <builtinFonts/ubuntu_10.h>
#include <builtinFonts/ubuntu_bold_10.h>

#include "Battery.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "activities/boot_sleep/BootActivity.h"
#include "activities/boot_sleep/SleepActivity.h"
#include "activities/home/HomeActivity.h"
#include "activities/network/CrossPointWebServerActivity.h"
#include "activities/reader/ReaderActivity.h"
#include "activities/settings/SettingsActivity.h"
#include "activities/util/FullScreenMessageActivity.h"
#include "config.h"

#define SPI_FQ 40000000
// Display SPI pins (custom pins for XteinkX4, not hardware SPI defaults)
#define EPD_SCLK 8   // SPI Clock
#define EPD_MOSI 10  // SPI MOSI (Master Out Slave In)
#define EPD_CS 21    // Chip Select
#define EPD_DC 4     // Data/Command
#define EPD_RST 5    // Reset
#define EPD_BUSY 6   // Busy

#define UART0_RXD 20  // Used for USB connection detection

#define SD_SPI_CS 12
#define SD_SPI_MISO 7

EInkDisplay einkDisplay(EPD_SCLK, EPD_MOSI, EPD_CS, EPD_DC, EPD_RST, EPD_BUSY);
InputManager inputManager;
GfxRenderer renderer(einkDisplay);
Activity* currentActivity;

// Fonts
EpdFont bookerlyFont(&bookerly_2b);
EpdFont bookerlyBoldFont(&bookerly_bold_2b);
EpdFont bookerlyItalicFont(&bookerly_italic_2b);
EpdFont bookerlyBoldItalicFont(&bookerly_bold_italic_2b);
EpdFontFamily bookerlyFontFamily(&bookerlyFont, &bookerlyBoldFont, &bookerlyItalicFont, &bookerlyBoldItalicFont);

EpdFont eulyooFont(&eulyoo_2b);
EpdFont eulyooSemiBoldFont(&eulyoo_semibold_2b);
EpdFontFamily eulyooFontFamily(&eulyooFont, &eulyooSemiBoldFont);

EpdFont smallFont(&pixelarial14);
EpdFontFamily smallFontFamily(&smallFont);

EpdFont ubuntu10Font(&ubuntu_10);
EpdFont ubuntuBold10Font(&ubuntu_bold_10);
EpdFontFamily ubuntuFontFamily(&ubuntu10Font, &ubuntuBold10Font);

EpdFont pretendardFont(&pretendard_8);
EpdFontFamily pretendardFontFamily(&pretendardFont);

EpdFont d2codingFont(&d2coding_14);
EpdFontFamily d2codingFontFamily(&d2codingFont);

// Auto-sleep timeout (10 minutes of inactivity)
constexpr unsigned long AUTO_SLEEP_TIMEOUT_MS = 10 * 60 * 1000;
// measurement of power button press duration calibration value
unsigned long t1 = 0;
unsigned long t2 = 0;

// Screenshot capture settings
constexpr bool ENABLE_SCREENSHOT_CAPTURE = true;
constexpr unsigned long SCREENSHOT_INTERVAL_MS = 10 * 1000;  // 10 seconds
static unsigned long lastScreenshotTime = 0;
static int screenshotCounter = 0;

void exitActivity() {
  if (currentActivity) {
    currentActivity->onExit();
    delete currentActivity;
    currentActivity = nullptr;
  }
}

void enterNewActivity(Activity* activity) {
  currentActivity = activity;
  currentActivity->onEnter();
}

// Verify long press on wake-up from deep sleep
void verifyWakeupLongPress() {
  // Give the user up to 1000ms to start holding the power button, and must hold for SETTINGS.getPowerButtonDuration()
  const auto start = millis();
  bool abort = false;
  // It takes us some time to wake up from deep sleep, so we need to subtract that from the duration
  uint16_t calibration = 25;
  uint16_t calibratedPressDuration =
      (calibration < SETTINGS.getPowerButtonDuration()) ? SETTINGS.getPowerButtonDuration() - calibration : 1;

  inputManager.update();
  // Verify the user has actually pressed
  while (!inputManager.isPressed(InputManager::BTN_POWER) && millis() - start < 1000) {
    delay(10);  // only wait 10ms each iteration to not delay too much in case of short configured duration.
    inputManager.update();
  }

  t2 = millis();
  if (inputManager.isPressed(InputManager::BTN_POWER)) {
    do {
      delay(10);
      inputManager.update();
    } while (inputManager.isPressed(InputManager::BTN_POWER) && inputManager.getHeldTime() < calibratedPressDuration);
    abort = inputManager.getHeldTime() < calibratedPressDuration;
  } else {
    abort = true;
  }

  if (abort) {
    // Button released too early. Returning to sleep.
    // IMPORTANT: Re-arm the wakeup trigger before sleeping again
    esp_deep_sleep_enable_gpio_wakeup(1ULL << InputManager::POWER_BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
    esp_deep_sleep_start();
  }
}

void waitForPowerRelease() {
  inputManager.update();
  while (inputManager.isPressed(InputManager::BTN_POWER)) {
    delay(50);
    inputManager.update();
  }
}

// Enter deep sleep mode
void enterDeepSleep() {
  exitActivity();
  enterNewActivity(new SleepActivity(renderer, inputManager));

  einkDisplay.deepSleep();
  Serial.printf("[%lu] [   ] Power button press calibration value: %lu ms\n", millis(), t2 - t1);
  Serial.printf("[%lu] [   ] Entering deep sleep.\n", millis());
  esp_deep_sleep_enable_gpio_wakeup(1ULL << InputManager::POWER_BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
  // Ensure that the power button has been released to avoid immediately turning back on if you're holding it
  waitForPowerRelease();
  // Enter Deep Sleep
  esp_deep_sleep_start();
}

void onGoHome();
void onGoToReader(const std::string& initialEpubPath) {
  exitActivity();
  enterNewActivity(new ReaderActivity(renderer, inputManager, initialEpubPath, onGoHome));
}
void onGoToReaderHome() { onGoToReader(std::string()); }
void onContinueReading() { onGoToReader(APP_STATE.openEpubPath); }

void onGoToFileTransfer() {
  exitActivity();
  enterNewActivity(new CrossPointWebServerActivity(renderer, inputManager, onGoHome));
}

void onGoToSettings() {
  exitActivity();
  enterNewActivity(new SettingsActivity(renderer, inputManager, onGoHome));
}

void onGoHome() {
  exitActivity();
  enterNewActivity(new HomeActivity(renderer, inputManager, onContinueReading, onGoToReaderHome, onGoToSettings,
                                    onGoToFileTransfer));
}

void setupDisplayAndFonts() {
  einkDisplay.begin();
  Serial.printf("[%lu] [   ] Display initialized\n", millis());
  renderer.insertFont(READER_FONT_ID, eulyooFontFamily);
  renderer.insertFont(UI_FONT_ID, pretendardFontFamily);
  renderer.insertFont(SMALL_FONT_ID, pretendardFontFamily);
  Serial.printf("[%lu] [   ] Fonts setup\n", millis());
}

// Save current framebuffer as 1-bit BMP to SD card
void saveScreenshotBMP(const char* filename) {
  File bmpFile = SD.open(filename, FILE_WRITE);
  if (!bmpFile) {
    Serial.printf("[%lu] [SCR] Failed to open %s for writing\n", millis(), filename);
    return;
  }

  // Display dimensions (physical buffer is 800x480)
  constexpr int width = 800;
  constexpr int height = 480;
  constexpr int bytesPerRow = width / 8;  // 100 bytes per row (already 4-byte aligned)
  constexpr int imageSize = bytesPerRow * height;
  constexpr int paletteSize = 8;   // 2 colors * 4 bytes
  constexpr int headerSize = 14;   // BMP file header
  constexpr int dibSize = 40;      // BITMAPINFOHEADER
  constexpr int pixelOffset = headerSize + dibSize + paletteSize;
  constexpr int fileSize = pixelOffset + imageSize;

  // BMP File Header (14 bytes)
  bmpFile.write('B');
  bmpFile.write('M');
  // File size (little-endian)
  bmpFile.write(fileSize & 0xFF);
  bmpFile.write((fileSize >> 8) & 0xFF);
  bmpFile.write((fileSize >> 16) & 0xFF);
  bmpFile.write((fileSize >> 24) & 0xFF);
  // Reserved
  bmpFile.write((uint8_t)0);
  bmpFile.write((uint8_t)0);
  bmpFile.write((uint8_t)0);
  bmpFile.write((uint8_t)0);
  // Pixel data offset
  bmpFile.write(pixelOffset & 0xFF);
  bmpFile.write((pixelOffset >> 8) & 0xFF);
  bmpFile.write((pixelOffset >> 16) & 0xFF);
  bmpFile.write((pixelOffset >> 24) & 0xFF);

  // DIB Header - BITMAPINFOHEADER (40 bytes)
  // Header size
  bmpFile.write((uint8_t)40);
  bmpFile.write((uint8_t)0);
  bmpFile.write((uint8_t)0);
  bmpFile.write((uint8_t)0);
  // Width
  bmpFile.write(width & 0xFF);
  bmpFile.write((width >> 8) & 0xFF);
  bmpFile.write((width >> 16) & 0xFF);
  bmpFile.write((width >> 24) & 0xFF);
  // Height (negative for top-down)
  int32_t negHeight = -height;
  bmpFile.write(negHeight & 0xFF);
  bmpFile.write((negHeight >> 8) & 0xFF);
  bmpFile.write((negHeight >> 16) & 0xFF);
  bmpFile.write((negHeight >> 24) & 0xFF);
  // Planes
  bmpFile.write((uint8_t)1);
  bmpFile.write((uint8_t)0);
  // Bits per pixel
  bmpFile.write((uint8_t)1);
  bmpFile.write((uint8_t)0);
  // Compression (none)
  bmpFile.write((uint8_t)0);
  bmpFile.write((uint8_t)0);
  bmpFile.write((uint8_t)0);
  bmpFile.write((uint8_t)0);
  // Image size
  bmpFile.write(imageSize & 0xFF);
  bmpFile.write((imageSize >> 8) & 0xFF);
  bmpFile.write((imageSize >> 16) & 0xFF);
  bmpFile.write((imageSize >> 24) & 0xFF);
  // X pixels per meter (72 DPI = 2835)
  bmpFile.write((uint8_t)0x13);
  bmpFile.write((uint8_t)0x0B);
  bmpFile.write((uint8_t)0);
  bmpFile.write((uint8_t)0);
  // Y pixels per meter
  bmpFile.write((uint8_t)0x13);
  bmpFile.write((uint8_t)0x0B);
  bmpFile.write((uint8_t)0);
  bmpFile.write((uint8_t)0);
  // Colors used
  bmpFile.write((uint8_t)2);
  bmpFile.write((uint8_t)0);
  bmpFile.write((uint8_t)0);
  bmpFile.write((uint8_t)0);
  // Colors important
  bmpFile.write((uint8_t)2);
  bmpFile.write((uint8_t)0);
  bmpFile.write((uint8_t)0);
  bmpFile.write((uint8_t)0);

  // Color Palette (2 colors * 4 bytes = 8 bytes)
  // In framebuffer: 0 = black (bit cleared), 1 = white (bit set)
  // Color 0: Black (BGRA)
  bmpFile.write((uint8_t)0x00);
  bmpFile.write((uint8_t)0x00);
  bmpFile.write((uint8_t)0x00);
  bmpFile.write((uint8_t)0x00);
  // Color 1: White (BGRA)
  bmpFile.write((uint8_t)0xFF);
  bmpFile.write((uint8_t)0xFF);
  bmpFile.write((uint8_t)0xFF);
  bmpFile.write((uint8_t)0x00);

  // Write pixel data directly from framebuffer
  // Framebuffer format: MSB first, 1 = white, 0 = black (matches BMP format)
  const uint8_t* frameBuffer = renderer.getFrameBuffer();
  bmpFile.write(frameBuffer, imageSize);

  bmpFile.close();
  Serial.printf("[%lu] [SCR] Screenshot saved: %s (%d bytes)\n", millis(), filename, fileSize);
}

void setup() {
  t1 = millis();

  // Only start serial if USB connected
  pinMode(UART0_RXD, INPUT);
  if (digitalRead(UART0_RXD) == HIGH) {
    Serial.begin(115200);
  }

  Serial.printf("[%lu] [   ] Starting CrossPoint version " CROSSPOINT_VERSION "\n", millis());

  inputManager.begin();
  // Initialize pins
  pinMode(BAT_GPIO0, INPUT);

  // Initialize SPI with custom pins
  SPI.begin(EPD_SCLK, SD_SPI_MISO, EPD_MOSI, EPD_CS);

  // SD Card Initialization
  // We need 6 open files concurrently when parsing a new chapter
  if (!SD.begin(SD_SPI_CS, SPI, SPI_FQ, "/sd", 6)) {
    Serial.printf("[%lu] [   ] SD card initialization failed\n", millis());
    setupDisplayAndFonts();
    exitActivity();
    enterNewActivity(new FullScreenMessageActivity(renderer, inputManager, "SD card error", BOLD));
    return;
  }

  SETTINGS.loadFromFile();

  // verify power button press duration after we've read settings.
  verifyWakeupLongPress();

  setupDisplayAndFonts();

  exitActivity();
  enterNewActivity(new BootActivity(renderer, inputManager));

  APP_STATE.loadFromFile();
  if (APP_STATE.openEpubPath.empty()) {
    onGoHome();
  } else {
    // Clear app state to avoid getting into a boot loop if the epub doesn't load
    const auto path = APP_STATE.openEpubPath;
    APP_STATE.openEpubPath = "";
    APP_STATE.saveToFile();
    onGoToReader(path);
  }

  // Ensure we're not still holding the power button before leaving setup
  waitForPowerRelease();
}

void loop() {
  static unsigned long maxLoopDuration = 0;
  const unsigned long loopStartTime = millis();
  static unsigned long lastMemPrint = 0;

  inputManager.update();

  if (Serial && millis() - lastMemPrint >= 10000) {
    Serial.printf("[%lu] [MEM] Free: %d bytes, Total: %d bytes, Min Free: %d bytes\n", millis(), ESP.getFreeHeap(),
                  ESP.getHeapSize(), ESP.getMinFreeHeap());
    lastMemPrint = millis();
  }

  // Screenshot capture every 10 seconds
  if (ENABLE_SCREENSHOT_CAPTURE && millis() - lastScreenshotTime >= SCREENSHOT_INTERVAL_MS) {
    char filename[32];
    snprintf(filename, sizeof(filename), "/screenshots/scr_%04d.bmp", screenshotCounter++);

    // Ensure screenshots directory exists
    if (!SD.exists("/screenshots")) {
      SD.mkdir("/screenshots");
    }

    saveScreenshotBMP(filename);
    lastScreenshotTime = millis();
  }

  // Check for any user activity (button press or release)
  static unsigned long lastActivityTime = millis();
  if (inputManager.wasAnyPressed() || inputManager.wasAnyReleased()) {
    lastActivityTime = millis();  // Reset inactivity timer
  }

  if (millis() - lastActivityTime >= AUTO_SLEEP_TIMEOUT_MS) {
    Serial.printf("[%lu] [SLP] Auto-sleep triggered after %lu ms of inactivity\n", millis(), AUTO_SLEEP_TIMEOUT_MS);
    enterDeepSleep();
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

  if (inputManager.isPressed(InputManager::BTN_POWER) &&
      inputManager.getHeldTime() > SETTINGS.getPowerButtonDuration()) {
    enterDeepSleep();
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

  const unsigned long activityStartTime = millis();
  if (currentActivity) {
    currentActivity->loop();
  }
  const unsigned long activityDuration = millis() - activityStartTime;

  const unsigned long loopDuration = millis() - loopStartTime;
  if (loopDuration > maxLoopDuration) {
    maxLoopDuration = loopDuration;
    if (maxLoopDuration > 50) {
      Serial.printf("[%lu] [LOOP] New max loop duration: %lu ms (activity: %lu ms)\n", millis(), maxLoopDuration,
                    activityDuration);
    }
  }

  // Add delay at the end of the loop to prevent tight spinning
  // When an activity requests skip loop delay (e.g., webserver running), use yield() for faster response
  // Otherwise, use longer delay to save power
  if (currentActivity && currentActivity->skipLoopDelay()) {
    yield();  // Give FreeRTOS a chance to run tasks, but return immediately
  } else {
    delay(10);  // Normal delay when no activity requires fast response
  }
}
