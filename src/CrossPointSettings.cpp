#include "CrossPointSettings.h"

#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <Serialization.h>

#include "fontIds.h"

// Initialize the static instance
CrossPointSettings CrossPointSettings::instance;

namespace {
constexpr uint8_t SETTINGS_FILE_VERSION = 3;  // Incremented for Korean-only version
// Increment this when adding new persisted settings fields
constexpr uint8_t SETTINGS_COUNT = 11;
constexpr char SETTINGS_FILE[] = "/.crosspoint/settings.bin";
}  // namespace

bool CrossPointSettings::saveToFile() const {
  // Make sure the directory exists
  SdMan.mkdir("/.crosspoint");

  FsFile outputFile;
  if (!SdMan.openFileForWrite("CPS", SETTINGS_FILE, outputFile)) {
    return false;
  }

  serialization::writePod(outputFile, SETTINGS_FILE_VERSION);
  serialization::writePod(outputFile, SETTINGS_COUNT);
  serialization::writePod(outputFile, sleepScreen);
  serialization::writePod(outputFile, extraParagraphSpacing);
  serialization::writePod(outputFile, shortPwrBtn);
  serialization::writePod(outputFile, statusBar);
  serialization::writePod(outputFile, orientation);
  serialization::writePod(outputFile, frontButtonLayout);
  serialization::writePod(outputFile, sideButtonLayout);
  serialization::writePod(outputFile, lineSpacing);
  serialization::writePod(outputFile, paragraphAlignment);
  serialization::writePod(outputFile, sleepTimeout);
  serialization::writePod(outputFile, refreshFrequency);
  outputFile.close();

  Serial.printf("[%lu] [CPS] Settings saved to file\n", millis());
  return true;
}

bool CrossPointSettings::loadFromFile() {
  FsFile inputFile;
  if (!SdMan.openFileForRead("CPS", SETTINGS_FILE, inputFile)) {
    return false;
  }

  uint8_t version;
  serialization::readPod(inputFile, version);

  // Handle different versions - accept version 1, 2, 3
  if (version < 1 || version > 3) {
    Serial.printf("[%lu] [CPS] Deserialization failed: Unknown version %u\n", millis(), version);
    inputFile.close();
    return false;
  }

  uint8_t fileSettingsCount = 0;
  serialization::readPod(inputFile, fileSettingsCount);

  // load settings that exist (support older files with fewer fields)
  uint8_t settingsRead = 0;
  do {
    serialization::readPod(inputFile, sleepScreen);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, extraParagraphSpacing);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, shortPwrBtn);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, statusBar);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, orientation);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, frontButtonLayout);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, sideButtonLayout);
    if (++settingsRead >= fileSettingsCount) break;

    // Skip fontFamily and fontSize from older versions
    if (version == 1) {
      uint8_t dummy;
      serialization::readPod(inputFile, dummy);  // fontFamily (ignored)
      if (++settingsRead >= fileSettingsCount) break;
      serialization::readPod(inputFile, dummy);  // fontSize (ignored)
      if (++settingsRead >= fileSettingsCount) break;
    }

    serialization::readPod(inputFile, lineSpacing);
    if (++settingsRead >= fileSettingsCount) break;

    // Skip systemFontFamily from version 2
    if (version == 2) {
      uint8_t dummy;
      serialization::readPod(inputFile, dummy);  // systemFontFamily (ignored)
      if (++settingsRead >= fileSettingsCount) break;
    }

    serialization::readPod(inputFile, paragraphAlignment);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, sleepTimeout);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, refreshFrequency);
    if (++settingsRead >= fileSettingsCount) break;
  } while (false);

  inputFile.close();
  Serial.printf("[%lu] [CPS] Settings loaded from file\n", millis());
  return true;
}

float CrossPointSettings::getReaderLineCompression() const {
  // Fixed to Eulyoo 14pt font
  switch (lineSpacing) {
    case TIGHT:
      return 1.00f;
    case NORMAL:
    default:
      return 1.20f;
    case WIDE:
      return 1.40f;
  }
}

unsigned long CrossPointSettings::getSleepTimeoutMs() const {
  switch (sleepTimeout) {
    case SLEEP_1_MIN:
      return 1UL * 60 * 1000;
    case SLEEP_5_MIN:
      return 5UL * 60 * 1000;
    case SLEEP_10_MIN:
    default:
      return 10UL * 60 * 1000;
    case SLEEP_15_MIN:
      return 15UL * 60 * 1000;
    case SLEEP_30_MIN:
      return 30UL * 60 * 1000;
  }
}

int CrossPointSettings::getRefreshFrequency() const {
  switch (refreshFrequency) {
    case REFRESH_1:
      return 1;
    case REFRESH_5:
      return 5;
    case REFRESH_10:
      return 10;
    case REFRESH_15:
    default:
      return 15;
    case REFRESH_30:
      return 30;
  }
}

int CrossPointSettings::getReaderFontId() const {
  // Fixed to Eulyoo 14pt for Korean reader
  return EULYOO_14_FONT_ID;
}

int CrossPointSettings::getUiFontId() const {
  // Fixed to Pretendard 10pt
  return UI_FONT_ID;
}
