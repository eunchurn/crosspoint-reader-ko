# Korean Font (을유1945) 적용 가이드

## 개요

한글 지원을 위해 다음 폰트들을 적용했습니다:

- **EPUB 리더**: 을유1945 (Eulyoo1945) - 한글/영문/한자 지원
- **UI/시스템**: Pretendard - 한글/영문 지원

## 적용된 폰트

| 용도 | 폰트 | 스타일 | 사이즈 | 헤더 파일 |
|------|------|--------|--------|-----------|
| EPUB 리더 | Eulyoo1945 | Regular | 14 | `eulyoo_2b.h` |
| EPUB 리더 | Eulyoo1945 | SemiBold | 14 | `eulyoo_semibold_2b.h` |
| UI | Pretendard | Regular | 8 | `pretendard_8.h` |
| Small | Pretendard | Regular | 8 | `pretendard_8.h` |

## 변경된 파일

### 1. `lib/EpdFont/builtinFonts/`
- `eulyoo_2b.h` - Regular 폰트 (2-bit, size 14)
- `eulyoo_semibold_2b.h` - SemiBold 폰트 (2-bit, size 14)
- `pretendard_8.h` - UI/Small 폰트 (2-bit, size 8)

### 2. `src/main.cpp`
```cpp
// 폰트 include
#include <builtinFonts/eulyoo_2b.h>
#include <builtinFonts/eulyoo_semibold_2b.h>
#include <builtinFonts/pretendard_8.h>

// 폰트 정의
EpdFont eulyooFont(&eulyoo_2b);
EpdFont eulyooSemiBoldFont(&eulyoo_semibold_2b);
EpdFontFamily eulyooFontFamily(&eulyooFont, &eulyooSemiBoldFont);

EpdFont pretendardFont(&pretendard_8);
EpdFontFamily pretendardFontFamily(&pretendardFont);

// 폰트 등록
renderer.insertFont(READER_FONT_ID, eulyooFontFamily);    // EPUB 리더
renderer.insertFont(UI_FONT_ID, pretendardFontFamily);    // UI
renderer.insertFont(SMALL_FONT_ID, pretendardFontFamily); // Small
```

### 3. `src/config.h`
```cpp
#define READER_FONT_ID 634176764    // Eulyoo1945
#define UI_FONT_ID (-237359987)     // Pretendard 8
#define SMALL_FONT_ID (-237359987)  // Pretendard 8
```

## 폰트 변환 방법

TTF 폰트를 헤더 파일로 변환하려면:

```bash
python lib/EpdFont/scripts/fontconvert.py <name> <size> <ttf_file> --2bit > output.h
```

### 을유폰트 변환 (한글 + 한자 + 특수문자)

```bash
# Regular
python lib/EpdFont/scripts/fontconvert.py eulyoo_2b 14 fonts/Eulyoo1945-Regular.ttf --2bit \
  --additional-intervals 0xAC00,0xD7AF \
  --additional-intervals 0x3130,0x318F \
  --additional-intervals 0x4E00,0x9FFF \
  --additional-intervals 0x3000,0x303F \
  --additional-intervals 0x2000,0x206F \
  --additional-intervals 0x2100,0x214F \
  --additional-intervals 0x2150,0x218F \
  --additional-intervals 0x2190,0x21FF \
  --additional-intervals 0x2200,0x22FF \
  --additional-intervals 0x2300,0x23FF \
  --additional-intervals 0x2460,0x24FF \
  --additional-intervals 0x25A0,0x25FF \
  --additional-intervals 0x2600,0x26FF \
  --additional-intervals 0x2700,0x27BF \
  --additional-intervals 0x3200,0x32FF \
  2>/dev/null > lib/EpdFont/builtinFonts/eulyoo_2b.h

# SemiBold
python lib/EpdFont/scripts/fontconvert.py eulyoo_semibold_2b 14 fonts/Eulyoo1945-SemiBold.ttf --2bit \
  --additional-intervals 0xAC00,0xD7AF \
  --additional-intervals 0x3130,0x318F \
  --additional-intervals 0x4E00,0x9FFF \
  --additional-intervals 0x3000,0x303F \
  --additional-intervals 0x2000,0x206F \
  --additional-intervals 0x2100,0x214F \
  --additional-intervals 0x2150,0x218F \
  --additional-intervals 0x2190,0x21FF \
  --additional-intervals 0x2200,0x22FF \
  --additional-intervals 0x2300,0x23FF \
  --additional-intervals 0x2460,0x24FF \
  --additional-intervals 0x25A0,0x25FF \
  --additional-intervals 0x2600,0x26FF \
  --additional-intervals 0x2700,0x27BF \
  --additional-intervals 0x3200,0x32FF \
  2>/dev/null > lib/EpdFont/builtinFonts/eulyoo_semibold_2b.h
```

### 유니코드 범위

| 범위 | 설명 |
|------|------|
| `0xAC00-0xD7AF` | 한글 음절 (Hangul Syllables) - 11,172자 |
| `0x3130-0x318F` | 한글 호환 자모 (Hangul Compatibility Jamo) |
| `0x4E00-0x9FFF` | CJK 통합 한자 (CJK Unified Ideographs) - 20,992자 |
| `0x3000-0x303F` | CJK 기호 및 문장부호 (『』「」《》〈〉【】 등) |
| `0x2000-0x206F` | General Punctuation (—–…''""†) |
| `0x2100-0x214F` | Letterlike Symbols (℃℉№™℡) |
| `0x2150-0x218F` | Number Forms (⅓⅔¼½¾) |
| `0x2190-0x21FF` | Arrows (←↑→↓↔↕) |
| `0x2200-0x22FF` | Mathematical Operators (±×÷≠≤≥∞) |
| `0x2300-0x23FF` | Miscellaneous Technical (⌚⌛⏰) |
| `0x2460-0x24FF` | Enclosed Alphanumerics (①②③ⓐⓑⓒ) |
| `0x25A0-0x25FF` | Geometric Shapes (○●◎□■△▲) |
| `0x2600-0x26FF` | Miscellaneous Symbols (☀☁☂★☆♠♣♥♦) |
| `0x2700-0x27BF` | Dingbats (✓✔✕✖✗✘) |
| `0x3200-0x32FF` | Enclosed CJK (㈜㈀㉠㉡) |

### 의존성
- Python 3
- freetype-py (`pip install freetype-py`)

## 폰트 ID 생성

새 폰트 ID를 생성하려면:

```bash
# READER_FONT_ID (Eulyoo)
ruby -rdigest -e 'puts [
  "./lib/EpdFont/builtinFonts/eulyoo_2b.h",
  "./lib/EpdFont/builtinFonts/eulyoo_semibold_2b.h",
].map{|f| Digest::SHA256.hexdigest(File.read(f)).to_i(16) }.sum % (2 ** 32) - (2 ** 31)'

# UI_FONT_ID / SMALL_FONT_ID (Pretendard)
ruby -rdigest -e 'puts [
  "./lib/EpdFont/builtinFonts/pretendard_8.h",
].map{|f| Digest::SHA256.hexdigest(File.read(f)).to_i(16) }.sum % (2 ** 32) - (2 ** 31)'
```

## Bookerly로 되돌리기

기존 Bookerly 폰트로 되돌리려면:

1. `src/main.cpp`에서:
   ```cpp
   renderer.insertFont(READER_FONT_ID, bookerlyFontFamily);
   // renderer.insertFont(READER_FONT_ID, eulyooFontFamily);
   ```

2. `src/config.h`에서:
   ```cpp
   #define READER_FONT_ID 1818981670
   ```

## 라이선스

- **을유1945**: [을유문화사](https://www.eulyoo.co.kr/)에서 제공하는 서체입니다.
- **Pretendard**: [GitHub](https://github.com/orioncactus/pretendard)에서 제공하는 오픈소스 서체입니다 (SIL Open Font License).

## 설치 방법

### 직접 플래시

`CrossPoint-0.9.0-ko.bin` 파일을 다운로드하여 `0x0` 주소에 플래시:

```bash
esptool.py --chip esp32c3 write_flash 0x0 CrossPoint-0.9.0-ko.bin
```

### 웹사이트에서 플래시

웹사이트에서 플래시 할 수 있습니다.

<https://xteink.dve.al/>

`CrossPoint-0.9.0-ko.bin` 파일을 다운로드하여 아래 방법으로 플래시

**OTA fast flash controls** > Select File > Flash firmware from file

> [!TIP]
> **reset** 버튼을 한번 누른 후 전원 버튼을 1초이상 누르면 펌웨어 적용이 됩니다.
