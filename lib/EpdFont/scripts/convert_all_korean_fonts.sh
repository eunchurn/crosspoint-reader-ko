#!/bin/bash
# Convert Korean font header files to .epdfont binary format
# Run this script from the crosspoint root directory

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
FONT_DIR="$SCRIPT_DIR/../builtinFonts"
OUTPUT_DIR="${1:-./sd_fonts}"

mkdir -p "$OUTPUT_DIR"

echo "Converting Korean fonts to .epdfont format..."
echo "Output directory: $OUTPUT_DIR"
echo ""

# Pretendard fonts
for size in 10 12; do
  for style in regular bold; do
    input="$FONT_DIR/pretendard_${size}_${style}.h"
    output="$OUTPUT_DIR/pretendard_${size}_${style}.epdfont"
    if [ -f "$input" ]; then
      echo "Converting: pretendard_${size}_${style}.h"
      python3 "$SCRIPT_DIR/convert_to_epdfont.py" "$input" "$output"
    else
      echo "Warning: $input not found"
    fi
  done
done

echo ""
echo "Conversion complete!"
echo ""
echo "Copy the contents of $OUTPUT_DIR to your SD card's /.crosspoint/fonts/ directory"
