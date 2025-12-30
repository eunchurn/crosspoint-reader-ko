#!/usr/bin/env python3
"""
Convert .h font files to .epdfont binary format.

Usage:
    python convert_to_epdfont.py input.h output.epdfont
"""

import sys
import re
import struct
import argparse

EPDFONT_MAGIC = 0x46445045  # "EPDF"
EPDFONT_VERSION = 1

def parse_font_header(content):
    """Parse the C header file and extract font data."""

    # Extract font name from the data structure name
    name_match = re.search(r'static const EpdFontData (\w+)\s*=', content)
    if not name_match:
        raise ValueError("Could not find EpdFontData declaration")
    font_name = name_match.group(1)

    # Extract bitmap array
    bitmap_match = re.search(
        rf'static const uint8_t {font_name}Bitmaps\[(\d+)\]\s*=\s*\{{([^}}]+)\}}',
        content, re.DOTALL
    )
    if not bitmap_match:
        raise ValueError(f"Could not find {font_name}Bitmaps array")

    bitmap_size = int(bitmap_match.group(1))
    bitmap_hex = bitmap_match.group(2)
    bitmap_values = [int(x.strip(), 16) for x in re.findall(r'0x[0-9A-Fa-f]+', bitmap_hex)]

    if len(bitmap_values) != bitmap_size:
        print(f"Warning: Expected {bitmap_size} bitmap bytes, found {len(bitmap_values)}")

    # Extract glyph array
    glyph_match = re.search(
        rf'static const EpdGlyph {font_name}Glyphs\[\]\s*=\s*\{{(.+?)\}};',
        content, re.DOTALL
    )
    if not glyph_match:
        raise ValueError(f"Could not find {font_name}Glyphs array")

    glyph_text = glyph_match.group(1)
    # Parse each glyph: { width, height, advanceX, left, top, dataLength, dataOffset },
    glyph_pattern = r'\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}'
    glyphs = []
    for match in re.finditer(glyph_pattern, glyph_text):
        glyphs.append({
            'width': int(match.group(1)),
            'height': int(match.group(2)),
            'advanceX': int(match.group(3)),
            'left': int(match.group(4)),
            'top': int(match.group(5)),
            'dataLength': int(match.group(6)),
            'dataOffset': int(match.group(7)),
        })

    # Extract intervals array
    interval_match = re.search(
        rf'static const EpdUnicodeInterval {font_name}Intervals\[\]\s*=\s*\{{(.+?)\}};',
        content, re.DOTALL
    )
    if not interval_match:
        raise ValueError(f"Could not find {font_name}Intervals array")

    interval_text = interval_match.group(1)
    # Parse each interval: { 0xFirst, 0xLast, 0xOffset },
    interval_pattern = r'\{\s*0x([0-9A-Fa-f]+)\s*,\s*0x([0-9A-Fa-f]+)\s*,\s*0x([0-9A-Fa-f]+)\s*\}'
    intervals = []
    for match in re.finditer(interval_pattern, interval_text):
        intervals.append({
            'first': int(match.group(1), 16),
            'last': int(match.group(2), 16),
            'offset': int(match.group(3), 16),
        })

    # Extract font metadata from EpdFontData struct
    # More flexible pattern that handles various whitespace and formatting
    font_data_pattern = (
        rf'static\s+const\s+EpdFontData\s+{font_name}\s*=\s*\{{\s*'
        rf'{font_name}Bitmaps\s*,\s*'
        rf'{font_name}Glyphs\s*,\s*'
        rf'{font_name}Intervals\s*,\s*'
        rf'(\d+)\s*,\s*'  # intervalCount
        rf'(\d+)\s*,\s*'  # advanceY
        rf'(-?\d+)\s*,\s*'  # ascender
        rf'(-?\d+)\s*,\s*'  # descender
        rf'(true|false)\s*,?\s*'  # is2Bit (optional trailing comma)
        rf'\}}'
    )
    font_data_match = re.search(font_data_pattern, content, re.DOTALL)
    if not font_data_match:
        raise ValueError(f"Could not find {font_name} EpdFontData struct")

    metadata = {
        'intervalCount': int(font_data_match.group(1)),
        'advanceY': int(font_data_match.group(2)),
        'ascender': int(font_data_match.group(3)),
        'descender': int(font_data_match.group(4)),
        'is2Bit': font_data_match.group(5) == 'true',
    }

    return {
        'name': font_name,
        'bitmap': bytes(bitmap_values),
        'glyphs': glyphs,
        'intervals': intervals,
        'metadata': metadata,
    }

def write_epdfont(font_data, output_path):
    """Write font data to .epdfont binary file."""

    metadata = font_data['metadata']
    intervals = font_data['intervals']
    glyphs = font_data['glyphs']
    bitmap = font_data['bitmap']

    # Calculate offsets
    header_size = 32
    intervals_size = len(intervals) * 12
    glyphs_size = len(glyphs) * 16

    intervals_offset = header_size
    glyphs_offset = intervals_offset + intervals_size
    bitmap_offset = glyphs_offset + glyphs_size

    with open(output_path, 'wb') as f:
        # Write header (32 bytes)
        header = struct.pack(
            '<IHBBBBBB5I',  # Little-endian (5 uint32s at end, not 4)
            EPDFONT_MAGIC,           # uint32 magic
            EPDFONT_VERSION,         # uint16 version
            1 if metadata['is2Bit'] else 0,  # uint8 is2Bit
            0,                       # uint8 reserved1
            metadata['advanceY'],    # uint8 advanceY
            metadata['ascender'] & 0xFF,  # int8 ascender
            metadata['descender'] & 0xFF,  # int8 descender (signed)
            0,                       # uint8 reserved2
            len(intervals),          # uint32 intervalCount
            len(glyphs),             # uint32 glyphCount
            intervals_offset,        # uint32 intervalsOffset
            glyphs_offset,           # uint32 glyphsOffset
            bitmap_offset,           # uint32 bitmapOffset
        )
        f.write(header)

        # Write intervals
        for interval in intervals:
            f.write(struct.pack('<3I', interval['first'], interval['last'], interval['offset']))

        # Write glyphs
        for glyph in glyphs:
            f.write(struct.pack(
                '<4B2h2I',
                glyph['width'],
                glyph['height'],
                glyph['advanceX'],
                0,  # reserved
                glyph['left'],
                glyph['top'],
                glyph['dataLength'],
                glyph['dataOffset'],
            ))

        # Write bitmap data
        f.write(bitmap)

    print(f"Created: {output_path}")
    print(f"  Intervals: {len(intervals)}")
    print(f"  Glyphs: {len(glyphs)}")
    print(f"  Bitmap size: {len(bitmap)} bytes")
    print(f"  Total file size: {bitmap_offset + len(bitmap)} bytes")

def main():
    parser = argparse.ArgumentParser(description='Convert .h font to .epdfont binary')
    parser.add_argument('input', help='Input .h font file')
    parser.add_argument('output', help='Output .epdfont file')
    args = parser.parse_args()

    with open(args.input, 'r') as f:
        content = f.read()

    font_data = parse_font_header(content)
    write_epdfont(font_data, args.output)

if __name__ == '__main__':
    main()
