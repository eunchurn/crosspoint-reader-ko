#include "FontSelectionActivity.h"

#include <GfxRenderer.h>
#include <HardwareSerial.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cstring>
#include <map>

#include "CrossPointSettings.h"
#include "FontManager.h"
#include "MappedInputManager.h"
#include "fontIds.h"

namespace {
constexpr const char* DEFAULT_FONT_NAME = "Default";
constexpr const char* CACHE_DIR = "/.crosspoint/cache";

// Recursively delete a directory and its contents
void deleteDirectory(const char* path) {
  FsFile dir = SdMan.open(path);
  if (!dir || !dir.isDir()) {
    if (dir) dir.close();
    return;
  }

  FsFile entry;
  while (entry.openNext(&dir, O_RDONLY)) {
    char entryName[64];
    entry.getName(entryName, sizeof(entryName));
    entry.close();

    std::string fullPath = std::string(path) + "/" + entryName;
    FsFile check = SdMan.open(fullPath.c_str());
    if (check) {
      bool isDir = check.isDir();
      check.close();
      if (isDir) {
        deleteDirectory(fullPath.c_str());
      } else {
        SdMan.remove(fullPath.c_str());
      }
    }
  }
  dir.close();
  SdMan.rmdir(path);
}

// Invalidate rendering caches for EPUB and TXT readers
// Keeps progress.bin (reading position) but removes layout caches
void invalidateReaderCaches() {
  Serial.printf("[%lu] [FNT] Invalidating reader rendering caches...\n", millis());

  FsFile cacheDir = SdMan.open(CACHE_DIR);
  if (!cacheDir || !cacheDir.isDir()) {
    if (cacheDir) cacheDir.close();
    Serial.printf("[%lu] [FNT] No cache directory found\n", millis());
    return;
  }

  int deletedCount = 0;
  FsFile bookCache;
  while (bookCache.openNext(&cacheDir, O_RDONLY)) {
    char bookCacheName[64];
    bookCache.getName(bookCacheName, sizeof(bookCacheName));
    bookCache.close();

    std::string bookCachePath = std::string(CACHE_DIR) + "/" + bookCacheName;

    // For EPUB: delete sections/ folder (keeps progress.bin)
    std::string sectionsPath = bookCachePath + "/sections";
    FsFile sectionsDir = SdMan.open(sectionsPath.c_str());
    if (sectionsDir && sectionsDir.isDir()) {
      sectionsDir.close();
      deleteDirectory(sectionsPath.c_str());
      Serial.printf("[%lu] [FNT] Deleted EPUB sections cache: %s\n", millis(), sectionsPath.c_str());
      deletedCount++;
    } else {
      if (sectionsDir) sectionsDir.close();
    }

    // For TXT: delete index.bin (keeps progress.bin)
    std::string indexPath = bookCachePath + "/index.bin";
    if (SdMan.exists(indexPath.c_str())) {
      SdMan.remove(indexPath.c_str());
      Serial.printf("[%lu] [FNT] Deleted TXT index cache: %s\n", millis(), indexPath.c_str());
      deletedCount++;
    }
  }
  cacheDir.close();

  Serial.printf("[%lu] [FNT] Invalidated %d cache entries\n", millis(), deletedCount);
}

// Case-insensitive string comparison helper
bool caseInsensitiveEquals(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); i++) {
    if (tolower(a[i]) != tolower(b[i])) return false;
  }
  return true;
}
}  // namespace

// Parse font filename into family-size key and style
// Supports formats:
//   FontFamily-Style-Size.epdfont (e.g., "Literata-Bold-14.epdfont")
//   FontFamily-Size.epdfont (e.g., "Literata-14.epdfont" - assumed Regular)
// Returns false if parsing fails
bool FontSelectionActivity::parseFontFilename(const char* filename, std::string& familySizeKey, std::string& style) {
  std::string name(filename);

  // Remove .epdfont extension
  const size_t extPos = name.rfind(".epdfont");
  if (extPos == std::string::npos) {
    return false;
  }
  name = name.substr(0, extPos);

  // Find the last hyphen (before size)
  const size_t lastHyphen = name.rfind('-');
  if (lastHyphen == std::string::npos || lastHyphen == 0) {
    return false;  // Invalid format
  }

  // Check if the part after last hyphen is a number (size)
  std::string sizePart = name.substr(lastHyphen + 1);
  bool isNumber = !sizePart.empty() && std::all_of(sizePart.begin(), sizePart.end(), ::isdigit);

  if (!isNumber) {
    return false;  // Last part is not a size
  }

  // Now find the second-to-last hyphen to check for style
  std::string beforeSize = name.substr(0, lastHyphen);
  const size_t styleHyphen = beforeSize.rfind('-');

  if (styleHyphen != std::string::npos && styleHyphen > 0) {
    std::string potentialStyle = beforeSize.substr(styleHyphen + 1);

    // Check if this is a known style
    if (caseInsensitiveEquals(potentialStyle, "Regular") || caseInsensitiveEquals(potentialStyle, "Bold") ||
        caseInsensitiveEquals(potentialStyle, "Italic") || caseInsensitiveEquals(potentialStyle, "BoldItalic")) {
      // Format: FontFamily-Style-Size
      std::string familyName = beforeSize.substr(0, styleHyphen);
      familySizeKey = familyName + "-" + sizePart;
      style = potentialStyle;
      return true;
    }
  }

  // Format: FontFamily-Size (no explicit style, assume Regular)
  familySizeKey = name;  // e.g., "Literata-14"
  style = "Regular";
  return true;
}

void FontSelectionActivity::taskTrampoline(void* param) {
  auto* self = static_cast<FontSelectionActivity*>(param);
  self->displayTaskLoop();
}

void FontSelectionActivity::loadFontList() {
  fontFamilies.clear();

  // First entry is always the default font
  FontFamilyEntry defaultEntry;
  defaultEntry.displayName = DEFAULT_FONT_NAME;
  fontFamilies.push_back(defaultEntry);

  // Ensure fonts directory exists
  SdMan.mkdir(FONTS_DIR);

  // Try to open the fonts folder
  FsFile dir = SdMan.open(FONTS_DIR);
  if (!dir) {
    Serial.printf("[%lu] [FNT] Font folder %s not found\n", millis(), FONTS_DIR);
    return;
  }

  if (!dir.isDir()) {
    Serial.printf("[%lu] [FNT] %s is not a directory\n", millis(), FONTS_DIR);
    dir.close();
    return;
  }

  // Temporary map to group fonts by family-size
  std::map<std::string, FontFamilyEntry> familyMap;

  // Scan all .epdfont files
  FsFile file;
  while (file.openNext(&dir, O_RDONLY)) {
    if (!file.isDir()) {
      char filename[64];
      file.getName(filename, sizeof(filename));

      // Check if file has .epdfont extension and skip macOS hidden files (._*)
      const size_t len = strlen(filename);
      if (len > 8 && strcasecmp(filename + len - 8, ".epdfont") == 0 && strncmp(filename, "._", 2) != 0) {
        std::string familySizeKey, style;
        if (parseFontFilename(filename, familySizeKey, style)) {
          std::string fullPath = std::string(FONTS_DIR) + "/" + filename;

          // Get or create family entry
          auto& entry = familyMap[familySizeKey];
          if (entry.displayName.empty()) {
            entry.displayName = familySizeKey;
          }

          // Assign path to appropriate style
          if (caseInsensitiveEquals(style, "Regular")) {
            entry.regularPath = fullPath;
          } else if (caseInsensitiveEquals(style, "Bold")) {
            entry.boldPath = fullPath;
          } else if (caseInsensitiveEquals(style, "Italic")) {
            entry.italicPath = fullPath;
          } else if (caseInsensitiveEquals(style, "BoldItalic")) {
            entry.boldItalicPath = fullPath;
          }

          Serial.printf("[%lu] [FNT] Found font: %s (family: %s, style: %s)\n", millis(), fullPath.c_str(),
                        familySizeKey.c_str(), style.c_str());
        } else {
          // Fallback: treat as single-file font (no style variants)
          std::string fullPath = std::string(FONTS_DIR) + "/" + filename;
          std::string displayName(filename, len - 8);

          FontFamilyEntry entry;
          entry.displayName = displayName;
          entry.regularPath = fullPath;
          familyMap[displayName] = entry;

          Serial.printf("[%lu] [FNT] Found font (no style): %s\n", millis(), fullPath.c_str());
        }
      }
    }
    file.close();
  }
  dir.close();

  // Convert map to vector, filtering out families without Regular variant
  for (auto& pair : familyMap) {
    auto& entry = pair.second;
    if (!entry.regularPath.empty()) {
      fontFamilies.push_back(entry);

      // Log style availability
      Serial.printf("[%lu] [FNT] Font family: %s [R:%s B:%s I:%s BI:%s]\n", millis(), entry.displayName.c_str(),
                    entry.regularPath.empty() ? "N" : "Y", entry.boldPath.empty() ? "N" : "Y",
                    entry.italicPath.empty() ? "N" : "Y", entry.boldItalicPath.empty() ? "N" : "Y");
    } else {
      Serial.printf("[%lu] [FNT] Skipping font family without Regular: %s\n", millis(), entry.displayName.c_str());
    }
  }

  Serial.printf("[%lu] [FNT] Total font families found: %zu (including default)\n", millis(), fontFamilies.size());

  // Find currently selected font family index
  selectedIndex = 0;  // Default
  if (SETTINGS.hasCustomFont()) {
    for (size_t i = 1; i < fontFamilies.size(); i++) {
      // Match by regular path (primary identifier)
      if (fontFamilies[i].regularPath == SETTINGS.customFontPath) {
        selectedIndex = static_cast<int>(i);
        break;
      }
    }
  }
}

void FontSelectionActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();

  // Load font list from SD card
  loadFontList();

  updateRequired = true;

  xTaskCreate(&FontSelectionActivity::taskTrampoline, "FontSelectionTask",
              4096,               // Stack size
              this,               // Parameters
              1,                  // Priority
              &displayTaskHandle  // Task handle
  );
}

void FontSelectionActivity::onExit() {
  ActivityWithSubactivity::onExit();

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
}

void FontSelectionActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onBack();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }

  const int itemCount = static_cast<int>(fontFamilies.size());
  if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
      mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    selectedIndex = (selectedIndex + itemCount - 1) % itemCount;
    updateRequired = true;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
             mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    selectedIndex = (selectedIndex + 1) % itemCount;
    updateRequired = true;
  }
}

void FontSelectionActivity::handleSelection() {
  xSemaphoreTake(renderingMutex, portMAX_DELAY);

  // Show loading screen
  renderer.clearScreen();
  renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2 - 10, "Applying font...");
  renderer.displayBuffer();

  const auto& selected = fontFamilies[selectedIndex];

  // Update custom font paths in settings
  if (selectedIndex == 0) {
    // Default font selected - clear custom font paths
    SETTINGS.customFontPath[0] = '\0';
    SETTINGS.customFontBoldPath[0] = '\0';
    SETTINGS.customFontItalicPath[0] = '\0';
    SETTINGS.customFontBoldItalicPath[0] = '\0';
  } else {
    // Custom font selected - save all available style paths
    strncpy(SETTINGS.customFontPath, selected.regularPath.c_str(), sizeof(SETTINGS.customFontPath) - 1);
    SETTINGS.customFontPath[sizeof(SETTINGS.customFontPath) - 1] = '\0';

    if (!selected.boldPath.empty()) {
      strncpy(SETTINGS.customFontBoldPath, selected.boldPath.c_str(), sizeof(SETTINGS.customFontBoldPath) - 1);
      SETTINGS.customFontBoldPath[sizeof(SETTINGS.customFontBoldPath) - 1] = '\0';
    } else {
      SETTINGS.customFontBoldPath[0] = '\0';
    }

    if (!selected.italicPath.empty()) {
      strncpy(SETTINGS.customFontItalicPath, selected.italicPath.c_str(), sizeof(SETTINGS.customFontItalicPath) - 1);
      SETTINGS.customFontItalicPath[sizeof(SETTINGS.customFontItalicPath) - 1] = '\0';
    } else {
      SETTINGS.customFontItalicPath[0] = '\0';
    }

    if (!selected.boldItalicPath.empty()) {
      strncpy(SETTINGS.customFontBoldItalicPath, selected.boldItalicPath.c_str(),
              sizeof(SETTINGS.customFontBoldItalicPath) - 1);
      SETTINGS.customFontBoldItalicPath[sizeof(SETTINGS.customFontBoldItalicPath) - 1] = '\0';
    } else {
      SETTINGS.customFontBoldItalicPath[0] = '\0';
    }
  }

  SETTINGS.saveToFile();
  Serial.printf("[%lu] [FNT] Font family selected: %s\n", millis(),
                selectedIndex == 0 ? "default" : selected.displayName.c_str());

  // Reload custom font dynamically (no reboot needed)
  reloadCustomReaderFont();

  // Invalidate EPUB/TXT caches since font changed
  invalidateReaderCaches();

  xSemaphoreGive(renderingMutex);

  // Return to settings
  onBack();
}

void FontSelectionActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired && !subActivity) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      render();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void FontSelectionActivity::render() {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // Draw header
  renderer.drawCenteredText(UI_12_FONT_ID, 15, "Custom Font", true, EpdFontFamily::BOLD);

  // Calculate visible items (with scrolling if needed)
  constexpr int lineHeight = 30;
  constexpr int startY = 60;
  const int maxVisibleItems = (pageHeight - startY - 50) / lineHeight;
  const int itemCount = static_cast<int>(fontFamilies.size());

  // Calculate scroll offset to keep selected item visible
  int scrollOffset = 0;
  if (itemCount > maxVisibleItems) {
    if (selectedIndex >= maxVisibleItems) {
      scrollOffset = selectedIndex - maxVisibleItems + 1;
    }
  }

  // Determine current selection (for checkmark comparison)
  int currentSelectedIndex = 0;  // Default
  if (SETTINGS.hasCustomFont()) {
    for (size_t i = 1; i < fontFamilies.size(); i++) {
      if (fontFamilies[i].regularPath == SETTINGS.customFontPath) {
        currentSelectedIndex = static_cast<int>(i);
        break;
      }
    }
  }

  // Draw font list
  for (int i = 0; i < maxVisibleItems && (i + scrollOffset) < itemCount; i++) {
    const int itemIndex = i + scrollOffset;
    const int itemY = startY + i * lineHeight;
    const bool isHighlighted = (itemIndex == selectedIndex);
    const bool isCurrentFont = (itemIndex == currentSelectedIndex);

    const auto& entry = fontFamilies[itemIndex];

    // Draw selection highlight
    if (isHighlighted) {
      renderer.fillRect(0, itemY - 2, pageWidth - 1, lineHeight);
    }

    // Draw checkmark for currently active font (using asterisk - available in Pretendard)
    if (isCurrentFont) {
      renderer.drawText(UI_10_FONT_ID, 10, itemY, "*", !isHighlighted);
    }

    // Build display name with style indicators
    std::string displayText = entry.displayName;
    if (itemIndex > 0) {
      // Show available styles for custom fonts
      std::string styles;
      if (!entry.boldPath.empty()) styles += "B";
      if (!entry.italicPath.empty()) styles += "I";
      if (!entry.boldItalicPath.empty()) styles += "+";
      if (!styles.empty()) {
        displayText += " [" + styles + "]";
      }
    }

    // Draw font name
    renderer.drawText(UI_10_FONT_ID, 35, itemY, displayText.c_str(), !isHighlighted);
  }

  // Draw scroll indicators if needed
  if (scrollOffset > 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, startY - 15, "...", true);
  }
  if (scrollOffset + maxVisibleItems < itemCount) {
    renderer.drawCenteredText(UI_10_FONT_ID, startY + maxVisibleItems * lineHeight, "...", true);
  }

  // Draw help text
  const auto labels = mappedInput.mapLabels("« Back", "Select", "", "");
  renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
