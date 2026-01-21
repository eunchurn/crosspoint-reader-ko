#include "CrossPointSettings.h"

#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <Serialization.h>

#include <cstring>

#include "fontIds.h"

// Initialize the static instance
CrossPointSettings CrossPointSettings::instance;

namespace {
constexpr uint8_t SETTINGS_FILE_VERSION = 1;
// Increment this when adding new persisted settings fields
constexpr uint8_t SETTINGS_COUNT = 22;  // Added customFontBoldPath, customFontItalicPath, customFontBoldItalicPath
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
  serialization::writePod(outputFile, fontFamily);
  serialization::writePod(outputFile, fontSize);
  serialization::writePod(outputFile, lineSpacing);
  serialization::writePod(outputFile, paragraphAlignment);
  serialization::writePod(outputFile, sleepTimeout);
  serialization::writePod(outputFile, refreshFrequency);
  serialization::writePod(outputFile, screenMargin);
  serialization::writePod(outputFile, sleepScreenCoverMode);
  serialization::writeString(outputFile, std::string(opdsServerUrl));
  serialization::writePod(outputFile, textAntiAliasing);
  serialization::writePod(outputFile, hideBatteryPercentage);
  serialization::writePod(outputFile, longPressChapterSkip);
  serialization::writeString(outputFile, std::string(customFontPath));
  serialization::writeString(outputFile, std::string(customFontBoldPath));
  serialization::writeString(outputFile, std::string(customFontItalicPath));
  serialization::writeString(outputFile, std::string(customFontBoldItalicPath));
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
  if (version != SETTINGS_FILE_VERSION) {
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
    serialization::readPod(inputFile, fontFamily);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, fontSize);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, lineSpacing);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, paragraphAlignment);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, sleepTimeout);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, refreshFrequency);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, screenMargin);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, sleepScreenCoverMode);
    if (++settingsRead >= fileSettingsCount) break;
    {
      std::string urlStr;
      serialization::readString(inputFile, urlStr);
      strncpy(opdsServerUrl, urlStr.c_str(), sizeof(opdsServerUrl) - 1);
      opdsServerUrl[sizeof(opdsServerUrl) - 1] = '\0';
    }
    serialization::readPod(inputFile, textAntiAliasing);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, hideBatteryPercentage);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, longPressChapterSkip);
    if (++settingsRead >= fileSettingsCount) break;
    {
      std::string fontPathStr;
      serialization::readString(inputFile, fontPathStr);
      strncpy(customFontPath, fontPathStr.c_str(), sizeof(customFontPath) - 1);
      customFontPath[sizeof(customFontPath) - 1] = '\0';
    }
    if (++settingsRead >= fileSettingsCount) break;
    {
      std::string fontPathStr;
      serialization::readString(inputFile, fontPathStr);
      strncpy(customFontBoldPath, fontPathStr.c_str(), sizeof(customFontBoldPath) - 1);
      customFontBoldPath[sizeof(customFontBoldPath) - 1] = '\0';
    }
    if (++settingsRead >= fileSettingsCount) break;
    {
      std::string fontPathStr;
      serialization::readString(inputFile, fontPathStr);
      strncpy(customFontItalicPath, fontPathStr.c_str(), sizeof(customFontItalicPath) - 1);
      customFontItalicPath[sizeof(customFontItalicPath) - 1] = '\0';
    }
    if (++settingsRead >= fileSettingsCount) break;
    {
      std::string fontPathStr;
      serialization::readString(inputFile, fontPathStr);
      strncpy(customFontBoldItalicPath, fontPathStr.c_str(), sizeof(customFontBoldItalicPath) - 1);
      customFontBoldItalicPath[sizeof(customFontBoldItalicPath) - 1] = '\0';
    }
    if (++settingsRead >= fileSettingsCount) break;
  } while (false);

  inputFile.close();
  Serial.printf("[%lu] [CPS] Settings loaded from file\n", millis());
  return true;
}

float CrossPointSettings::getReaderLineCompression() const {
  switch (fontFamily) {
    case BOOKERLY:
    default:
      switch (lineSpacing) {
        case TIGHT:
          return 0.95f;
        case NORMAL:
        default:
          return 1.0f;
        case WIDE:
          return 1.1f;
      }
    case NOTOSANS:
      switch (lineSpacing) {
        case TIGHT:
          return 0.90f;
        case NORMAL:
        default:
          return 0.95f;
        case WIDE:
          return 1.0f;
      }
    case OPENDYSLEXIC:
      switch (lineSpacing) {
        case TIGHT:
          return 0.90f;
        case NORMAL:
        default:
          return 0.95f;
        case WIDE:
          return 1.0f;
      }
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
  // Return custom font ID if a custom font is configured
  if (hasCustomFont()) {
    // Generate unique negative ID based on font path hash
    // This ensures different custom fonts have different IDs for cache invalidation
    uint32_t hash = 5381;
    for (const char* p = customFontPath; *p; p++) {
      hash = ((hash << 5) + hash) + static_cast<uint8_t>(*p);  // djb2 hash
    }
    // Return negative value to avoid collision with built-in font IDs
    return -static_cast<int>((hash & 0x7FFFFFFF) | 1);
  }

  // Use built-in font based on fontFamily/fontSize
  switch (fontFamily) {
    case BOOKERLY:
    default:
      switch (fontSize) {
        case SMALL:
          return BOOKERLY_12_FONT_ID;
        case MEDIUM:
        default:
          return BOOKERLY_14_FONT_ID;
        case LARGE:
          return BOOKERLY_16_FONT_ID;
        case EXTRA_LARGE:
          return BOOKERLY_18_FONT_ID;
      }
    case NOTOSANS:
      switch (fontSize) {
        case SMALL:
          return NOTOSANS_12_FONT_ID;
        case MEDIUM:
        default:
          return NOTOSANS_14_FONT_ID;
        case LARGE:
          return NOTOSANS_16_FONT_ID;
        case EXTRA_LARGE:
          return NOTOSANS_18_FONT_ID;
      }
    case OPENDYSLEXIC:
      switch (fontSize) {
        case SMALL:
          return OPENDYSLEXIC_8_FONT_ID;
        case MEDIUM:
        default:
          return OPENDYSLEXIC_10_FONT_ID;
        case LARGE:
          return OPENDYSLEXIC_12_FONT_ID;
        case EXTRA_LARGE:
          return OPENDYSLEXIC_14_FONT_ID;
      }
  }
}

const char* CrossPointSettings::getCustomFontName() const {
  if (!hasCustomFont()) {
    return nullptr;
  }
  // Extract family name from path
  // Supports formats:
  //   /fonts/FontFamily-Style-Size.epdfont -> "FontFamily-Size"
  //   /fonts/FontFamily-Size.epdfont -> "FontFamily-Size"
  const char* lastSlash = strrchr(customFontPath, '/');
  const char* filename = lastSlash ? lastSlash + 1 : customFontPath;

  static char nameBuffer[48];
  strncpy(nameBuffer, filename, sizeof(nameBuffer) - 1);
  nameBuffer[sizeof(nameBuffer) - 1] = '\0';

  // Remove .epdfont extension
  char* dot = strrchr(nameBuffer, '.');
  if (dot && strcasecmp(dot, ".epdfont") == 0) {
    *dot = '\0';
  }

  // Parse to extract family-size (remove style if present)
  // Find last hyphen (before size)
  char* lastHyphen = strrchr(nameBuffer, '-');
  if (!lastHyphen) {
    return nameBuffer;  // No hyphen, return as-is
  }

  // Check if part after last hyphen is a number (size)
  bool isSize = true;
  for (const char* p = lastHyphen + 1; *p; p++) {
    if (*p < '0' || *p > '9') {
      isSize = false;
      break;
    }
  }

  if (!isSize) {
    return nameBuffer;  // Last part is not size, return as-is
  }

  // Find second-to-last hyphen
  *lastHyphen = '\0';  // Temporarily terminate to find previous hyphen
  char* styleHyphen = strrchr(nameBuffer, '-');
  *lastHyphen = '-';  // Restore

  if (!styleHyphen) {
    return nameBuffer;  // Only one hyphen (FontFamily-Size), return as-is
  }

  // Check if the part between hyphens is a style name
  size_t styleLen = lastHyphen - styleHyphen - 1;
  char stylePart[16];
  if (styleLen < sizeof(stylePart)) {
    strncpy(stylePart, styleHyphen + 1, styleLen);
    stylePart[styleLen] = '\0';

    // Check for known style names (case-insensitive)
    if (strcasecmp(stylePart, "Regular") == 0 || strcasecmp(stylePart, "Bold") == 0 ||
        strcasecmp(stylePart, "Italic") == 0 || strcasecmp(stylePart, "BoldItalic") == 0) {
      // Remove style part: "FontFamily-Style-Size" -> "FontFamily-Size"
      memmove(styleHyphen, lastHyphen, strlen(lastHyphen) + 1);
    }
  }

  return nameBuffer;
}
