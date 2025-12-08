#!/bin/bash
# Sort proccount output by instruction count (highest first)
# Usage: proccount-sort.sh [options] <proccount.txt> [num_results] [image_filter]

CALLED_ONLY=0

# Parse options
while [[ "$1" == --* ]]; do
    case "$1" in
        --called-only)
            CALLED_ONLY=1
            shift
            ;;
        --help)
            echo "Usage: $0 [options] <proccount.txt> [num_results] [image_filter]"
            echo ""
            echo "Options:"
            echo "  --called-only  Only show routines with calls > 0"
            echo "  --help         Show this help message"
            echo ""
            echo "Arguments:"
            echo "  proccount.txt: The TSV output file from proccount2tsv.sh"
            echo "  num_results: Number of top results to show (default: 20)"
            echo "  image_filter: Optional filter by image name (e.g., mojo.out)"
            echo ""
            echo "Examples:"
            echo "  $0 mojo.proccount.txt                      # Top 20 by icount"
            echo "  $0 mojo.proccount.txt 10                   # Top 10 by icount"
            echo "  $0 mojo.proccount.txt 10 mojo.out          # Top 10 from mojo.out only"
            echo "  $0 --called-only mojo.proccount.txt 10     # Top 10 with calls > 0"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

if [ $# -lt 1 ]; then
    echo "Usage: $0 [options] <proccount.txt> [num_results] [image_filter]"
    echo "Use --help for more information"
    exit 1
fi

INPUT="$1"
NUM="${2:-20}"
IMAGE_FILTER="${3:-}"

if [ ! -f "$INPUT" ]; then
    echo "Error: File not found: $INPUT"
    exit 1
fi

# Print header
head -1 "$INPUT"
echo "---"

# Build the pipeline
# Column 4 = Calls, Column 5 = Instructions
if [ -n "$IMAGE_FILTER" ]; then
    FILTER_CMD="grep \"$IMAGE_FILTER\""
else
    FILTER_CMD="cat"
fi

if [ "$CALLED_ONLY" -eq 1 ]; then
    # Filter for calls > 0 (column 4)
    tail -n +2 "$INPUT" | eval "$FILTER_CMD" | awk -F'\t' '$4 > 0' | sort -t$'\t' -k5 -n -r | head -n "$NUM"
else
    tail -n +2 "$INPUT" | eval "$FILTER_CMD" | sort -t$'\t' -k5 -n -r | head -n "$NUM"
fi
