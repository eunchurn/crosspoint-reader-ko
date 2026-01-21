#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "activities/ActivityWithSubactivity.h"

/**
 * Font family entry with paths to different style variants.
 * Follows FontFamily-Style-Size.epdfont naming convention.
 * Style can be: Regular, Bold, Italic, BoldItalic
 */
struct FontFamilyEntry {
  std::string displayName;   // e.g., "Literata-14"
  std::string regularPath;   // Path to Regular variant
  std::string boldPath;      // Path to Bold variant (optional)
  std::string italicPath;    // Path to Italic variant (optional)
  std::string boldItalicPath;  // Path to BoldItalic variant (optional)
};

/**
 * Activity for selecting a custom font from /fonts folder.
 * Lists font families (grouped by FontFamily-Style-Size.epdfont naming convention)
 * and allows the user to select one.
 */
class FontSelectionActivity final : public ActivityWithSubactivity {
 public:
  explicit FontSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                 const std::function<void()>& onBack)
      : ActivityWithSubactivity("FontSelection", renderer, mappedInput), onBack(onBack) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool updateRequired = false;

  int selectedIndex = 0;
  std::vector<FontFamilyEntry> fontFamilies;  // Grouped font families
  const std::function<void()> onBack;

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void render();
  void loadFontList();
  void handleSelection();

  // Parse font filename into family-size key and style
  // e.g., "Literata-Bold-14.epdfont" -> key="Literata-14", style="Bold"
  static bool parseFontFilename(const char* filename, std::string& familySizeKey, std::string& style);

  static constexpr const char* FONTS_DIR = "/fonts";
};
