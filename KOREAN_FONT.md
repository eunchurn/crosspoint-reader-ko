# Korean Font Support Guide

## 개요

한글 지원을 위해 세 가지 폰트를 추가했습니다:

| 용도 | 폰트 | 설명 |
|------|------|------|
| 📖 **리더** | 을유1945 (Eulyoo1945) | EPUB 본문용 명조 계열 |
| 🖥️ **UI** | Pretendard | 시스템 메뉴용 고딕 계열 |
| 🔤 **모노** | D2Coding | 코드/기술 정보용 고정폭 |

**Flash 사용량**: 94.1% (6.16MB / 6.55MB)

---

## 적용된 폰트

### 1. 을유1945 (Eulyoo1945) - 리더 폰트

EPUB 본문 표시용 명조 계열 서체입니다.

| 스타일 | TTF 파일 | 헤더 파일 | 크기 |
|--------|----------|-----------|------|
| Regular | `Eulyoo1945-Regular.ttf` | `eulyoo_2b.h` | 8.0MB |
| SemiBold | `Eulyoo1945-SemiBold.ttf` | `eulyoo_semibold_2b.h` | 8.4MB |

**지원 문자**:
- 한글 음절 (11,172자)
- 한글 호환 자모
- CJK 통합 한자 (20,992자)
- CJK 기호 및 구두점 (『』「」《》〈〉【】 등)
- 기본 라틴, 키릴 문자 등

### 2. Pretendard - UI 폰트

시스템 메뉴 및 UI 표시용 고딕 계열 서체입니다.

| 스타일 | TTF 파일 | 헤더 파일 | 크기 |
|--------|----------|-----------|------|
| SemiBold | `Pretendard-SemiBold.ttf` | `pretendard_10.h` | 6.2MB |

**지원 문자**:
- 한글 음절 (11,172자)
- 한글 호환 자모
- CJK 통합 한자 (20,992자)
- 기본 라틴 문자 등

### 3. D2Coding - 모노 폰트

코드 및 기술 정보 표시용 고정폭 서체입니다.

| 스타일 | TTF 파일 | 헤더 파일 | 크기 |
|--------|----------|-----------|------|
| Regular | `D2CodingLigatureNerdFont-Regular.ttf` | `d2coding_14.h` | 370KB |

**지원 문자**:
- 기본 라틴 문자
- 일반 구두점 및 기호
- ⚠️ **한글 미포함** (Flash 용량 제한)

---

## 변경된 파일

### `lib/EpdFont/builtinFonts/`
```
eulyoo_2b.h          # 리더 폰트 Regular
eulyoo_semibold_2b.h # 리더 폰트 SemiBold
pretendard_10.h      # UI 폰트
d2coding_14.h        # 모노 폰트
```

### `src/main.cpp`
```cpp
// 추가된 include
#include <builtinFonts/eulyoo_2b.h>
#include <builtinFonts/eulyoo_semibold_2b.h>
#include <builtinFonts/pretendard_10.h>
#include <builtinFonts/d2coding_14.h>

// 폰트 정의
EpdFont eulyooFont(&eulyoo_2b);
EpdFont eulyooSemiBoldFont(&eulyoo_semibold_2b);
EpdFontFamily eulyooFontFamily(&eulyooFont, &eulyooSemiBoldFont);

EpdFont pretendardFont(&pretendard_10);
EpdFontFamily pretendardFontFamily(&pretendardFont);

EpdFont d2codingFont(&d2coding_14);
EpdFontFamily d2codingFontFamily(&d2codingFont);

// 폰트 등록
renderer.insertFont(READER_FONT_ID, eulyooFontFamily);      // 리더
renderer.insertFont(UI_FONT_ID, pretendardFontFamily);      // UI
renderer.insertFont(SMALL_FONT_ID, d2codingFontFamily);     // 모노
```

### `src/config.h`
```cpp
#define READER_FONT_ID (-15174892)    // Eulyoo1945
#define UI_FONT_ID (-575875680)       // Pretendard
#define SMALL_FONT_ID 1362425038      // D2Coding
```

---

## 폰트 변환 방법

### 기본 명령어

```bash
python lib/EpdFont/scripts/fontconvert.py <name> <size> <ttf_file> --2bit > output.h
```

### 한글 + 한자 포함 변환

```bash
# Eulyoo1945 Regular (리더)
python lib/EpdFont/scripts/fontconvert.py eulyoo_2b 14 fonts/Eulyoo1945-Regular.ttf --2bit \
  --additional-intervals 0xAC00,0xD7AF \
  --additional-intervals 0x3130,0x318F \
  --additional-intervals 0x4E00,0x9FFF \
  --additional-intervals 0x3000,0x303F \
  2>/dev/null > lib/EpdFont/builtinFonts/eulyoo_2b.h

# Eulyoo1945 SemiBold (리더)
python lib/EpdFont/scripts/fontconvert.py eulyoo_semibold_2b 14 fonts/Eulyoo1945-SemiBold.ttf --2bit \
  --additional-intervals 0xAC00,0xD7AF \
  --additional-intervals 0x3130,0x318F \
  --additional-intervals 0x4E00,0x9FFF \
  --additional-intervals 0x3000,0x303F \
  2>/dev/null > lib/EpdFont/builtinFonts/eulyoo_semibold_2b.h

# Pretendard (UI)
python lib/EpdFont/scripts/fontconvert.py pretendard_10 10 fonts/Pretendard-SemiBold.ttf --2bit \
  --additional-intervals 0xAC00,0xD7AF \
  --additional-intervals 0x3130,0x318F \
  --additional-intervals 0x4E00,0x9FFF \
  2>/dev/null > lib/EpdFont/builtinFonts/pretendard_10.h

# D2Coding (모노) - 기본 문자만 (한글 제외)
python lib/EpdFont/scripts/fontconvert.py d2coding_14 14 fonts/D2CodingLigatureNerdFont-Regular.ttf --2bit \
  2>/dev/null > lib/EpdFont/builtinFonts/d2coding_14.h
```

### 유니코드 범위

| 범위 | 설명 | 문자 수 |
|------|------|---------|
| `0xAC00,0xD7AF` | 한글 음절 (Hangul Syllables) | 11,172자 |
| `0x3130,0x318F` | 한글 호환 자모 (Hangul Compatibility Jamo) | 96자 |
| `0x4E00,0x9FFF` | CJK 통합 한자 (CJK Unified Ideographs) | 20,992자 |
| `0x3000,0x303F` | CJK 기호 및 구두점 (『』「」《》〈〉【】 등) | 64자 |

### 의존성

```bash
pip install freetype-py
```

---

## 폰트 ID 생성

폰트 파일이 변경되면 새 ID를 생성해야 합니다:

```bash
# READER_FONT_ID
ruby -rdigest -e 'puts [
  "./lib/EpdFont/builtinFonts/eulyoo_2b.h",
  "./lib/EpdFont/builtinFonts/eulyoo_semibold_2b.h",
].map{|f| Digest::SHA256.hexdigest(File.read(f)).to_i(16) }.sum % (2 ** 32) - (2 ** 31)'

# UI_FONT_ID
ruby -rdigest -e 'puts [
  "./lib/EpdFont/builtinFonts/pretendard_10.h",
].map{|f| Digest::SHA256.hexdigest(File.read(f)).to_i(16) }.sum % (2 ** 32) - (2 ** 31)'

# SMALL_FONT_ID
ruby -rdigest -e 'puts [
  "./lib/EpdFont/builtinFonts/d2coding_14.h",
].map{|f| Digest::SHA256.hexdigest(File.read(f)).to_i(16) }.sum % (2 ** 32) - (2 ** 31)'
```

---

## 원본 폰트로 되돌리기

### `src/main.cpp`
```cpp
renderer.insertFont(READER_FONT_ID, bookerlyFontFamily);  // Bookerly
renderer.insertFont(UI_FONT_ID, ubuntuFontFamily);        // Ubuntu
renderer.insertFont(SMALL_FONT_ID, smallFontFamily);      // Pixelarial
```

### `src/config.h`
```cpp
#define READER_FONT_ID 1818981670     // Bookerly
#define UI_FONT_ID (-1619831379)      // Ubuntu
#define SMALL_FONT_ID 1482513144      // Pixelarial
```

---

## 라이선스

| 폰트 | 제공 | 라이선스 |
|------|------|----------|
| 을유1945 | [을유문화사](https://www.eulyoo.co.kr/) | 확인 필요 |
| Pretendard | [GitHub](https://github.com/orioncactus/pretendard) | OFL 1.1 |
| D2Coding | [GitHub](https://github.com/naver/d2codingfont) | OFL 1.1 |

사용 전 각 폰트의 라이선스 조건을 확인하세요.
