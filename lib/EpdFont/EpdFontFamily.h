#pragma once
#include "EpdFont.h"

// Global font style enum - alias for use outside EpdFontFamily class
enum EpdFontStyle : uint8_t { REGULAR = 0, BOLD = 1, ITALIC = 2, BOLD_ITALIC = 3 };

class EpdFontFamily {
 public:
  // Legacy alias for backwards compatibility
  using Style = EpdFontStyle;

  explicit EpdFontFamily(const EpdFont* regular, const EpdFont* bold = nullptr, const EpdFont* italic = nullptr,
                         const EpdFont* boldItalic = nullptr)
      : regular(regular), bold(bold), italic(italic), boldItalic(boldItalic) {}
  ~EpdFontFamily() = default;
  void getTextDimensions(const char* string, int* w, int* h, EpdFontStyle style = REGULAR) const;
  bool hasPrintableChars(const char* string, EpdFontStyle style = REGULAR) const;
  const EpdFontData* getData(EpdFontStyle style = REGULAR) const;
  const EpdGlyph* getGlyph(uint32_t cp, EpdFontStyle style = REGULAR) const;

  // Check if bold variant is available (for synthetic bold decision)
  bool hasBold() const { return bold != nullptr; }

 private:
  const EpdFont* regular;
  const EpdFont* bold;
  const EpdFont* italic;
  const EpdFont* boldItalic;

  const EpdFont* getFont(EpdFontStyle style) const;
};
