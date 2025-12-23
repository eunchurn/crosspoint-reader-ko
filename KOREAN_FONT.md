# Korean Font (을유1945) 적용 가이드

## 개요

기본 리더 폰트를 Bookerly에서 **을유1945 (Eulyoo1945)**로 변경했습니다.

을유1945는 한글과 영문을 모두 지원하는 서체로, e-ink 디스플레이에서 한글 가독성을 향상시킵니다.

## 적용된 폰트

| 스타일 | 파일명 | 헤더 파일 |
|--------|--------|-----------|
| Regular | `Eulyoo1945-Regular.ttf` | `eulyoo_2b.h` |
| SemiBold | `Eulyoo1945-SemiBold.ttf` | `eulyoo_semibold_2b.h` |

## 변경된 파일

### 1. `lib/EpdFont/builtinFonts/`
- `eulyoo_2b.h` - Regular 폰트 (2-bit, size 14)
- `eulyoo_semibold_2b.h` - SemiBold 폰트 (2-bit, size 14)

### 2. `src/main.cpp`
```cpp
// 추가된 include
#include <builtinFonts/eulyoo_2b.h>
#include <builtinFonts/eulyoo_semibold_2b.h>

// 추가된 폰트 정의
EpdFont eulyooFont(&eulyoo_2b);
EpdFont eulyooSemiBoldFont(&eulyoo_semibold_2b);
EpdFontFamily eulyooFontFamily(&eulyooFont, &eulyooSemiBoldFont);

// 기본 리더 폰트로 설정
// renderer.insertFont(READER_FONT_ID, bookerlyFontFamily);  // 기존 (주석 처리)
renderer.insertFont(READER_FONT_ID, eulyooFontFamily);       // 변경됨
```

### 3. `src/config.h`
```cpp
// 새로운 폰트 ID (한글 포함)
#define READER_FONT_ID 1038169715

// 기존 Bookerly 폰트 ID (주석으로 보존)
// #define READER_FONT_ID 1818981670
```

## 폰트 변환 방법

TTF 폰트를 헤더 파일로 변환하려면:

```bash
python lib/EpdFont/scripts/fontconvert.py <name> <size> <ttf_file> --2bit > output.h
```

### 한글 + 한자 포함 변환 (중요!)

기본 fontconvert.py는 한글/한자 유니코드 범위가 주석 처리되어 있습니다.
한글과 한자를 포함하려면 `--additional-intervals` 옵션을 사용해야 합니다:

```bash
# Regular
python lib/EpdFont/scripts/fontconvert.py eulyoo_2b 14 fonts/Eulyoo1945-Regular.ttf --2bit \
  --additional-intervals 0xAC00,0xD7AF \
  --additional-intervals 0x3130,0x318F \
  --additional-intervals 0x4E00,0x9FFF \
  2>/dev/null > lib/EpdFont/builtinFonts/eulyoo_2b.h

# SemiBold
python lib/EpdFont/scripts/fontconvert.py eulyoo_semibold_2b 14 fonts/Eulyoo1945-SemiBold.ttf --2bit \
  --additional-intervals 0xAC00,0xD7AF \
  --additional-intervals 0x3130,0x318F \
  --additional-intervals 0x4E00,0x9FFF \
  2>/dev/null > lib/EpdFont/builtinFonts/eulyoo_semibold_2b.h
```

### 유니코드 범위

| 범위             | 설명                                      |
|------------------|-------------------------------------------|
| `0xAC00,0xD7AF`  | 한글 음절 (Hangul Syllables) - 11,172자   |
| `0x3130,0x318F`  | 한글 호환 자모 (Hangul Compatibility Jamo)|
| `0x4E00,0x9FFF`  | CJK 통합 한자 (CJK Unified Ideographs) - 20,992자 |

### 의존성
- Python 3
- freetype-py (`pip install freetype-py`)

## 폰트 ID 생성

새 폰트 ID를 생성하려면:

```bash
ruby -rdigest -e 'puts [
  "./lib/EpdFont/builtinFonts/eulyoo_2b.h",
  "./lib/EpdFont/builtinFonts/eulyoo_semibold_2b.h",
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

을유1945 폰트는 [을유문화사](https://www.eulyoo.co.kr/)에서 제공하는 서체입니다.
사용 전 라이선스 조건을 확인하세요.
