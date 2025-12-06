#!/bin/bash
# Convert proccount output (skipping the first MAIN# line)
# Outputs tab-separated values for easy Excel import
# Usage: proccount2csv.sh <input.out> [output.txt]

if [ $# -lt 1 ]; then
    echo "Usage: $0 <proccount.out> [output.txt]"
    exit 1
fi

INPUT="$1"
OUTPUT="${2:-${INPUT%.out}.txt}"

# Skip first line, remove trailing # from each field (convert #\t to \t)
tail -n +2 "$INPUT" | sed 's/#\t/\t/g; s/#$//' > "$OUTPUT"

echo "Converted: $INPUT -> $OUTPUT"

