#!/bin/bash
# Convert RTNPerf output to tab-separated values for Excel
# Usage: rtnperf2tsv.sh <rtnperf.txt> [output.tsv]

if [ $# -lt 1 ]; then
    echo "Usage: $0 <rtnperf.txt> [output.tsv]"
    exit 1
fi

INPUT="$1"
OUTPUT="${2:-${INPUT%.txt}.tsv}"

if [ ! -f "$INPUT" ]; then
    echo "Error: File not found: $INPUT"
    exit 1
fi

# Print header
echo -e "RTN_Name\tStatic_Icount\tTID\tCall_Count\tAvg_TSC\tLast_TSC" > "$OUTPUT"

# Parse the rtnperf output format:
# RTN_Name:  ( name )  Static instruction count NNN
#  tid N  Call count before NNN Average TSC for RTN NNN ... last call TSC NNN

awk '
/^RTN_Name:/ {
    # Extract routine name between ( and )
    match($0, /\( .* \)/)
    rtn_name = substr($0, RSTART+2, RLENGTH-4)
    
    # Extract static instruction count
    match($0, /Static instruction count [0-9]+/)
    split(substr($0, RSTART), arr, " ")
    static_icount = arr[4]
}
/^ tid [0-9]+/ {
    # Extract tid
    match($0, /tid [0-9]+/)
    split(substr($0, RSTART), arr, " ")
    tid = arr[2]
    
    # Extract call count
    match($0, /Call count before [0-9]+/)
    split(substr($0, RSTART), arr, " ")
    call_count = arr[4]
    
    # Extract average TSC
    match($0, /Average TSC for RTN [0-9]+/)
    split(substr($0, RSTART), arr, " ")
    avg_tsc = arr[5]
    
    # Extract last call TSC
    match($0, /last call TSC [0-9]+/)
    split(substr($0, RSTART), arr, " ")
    last_tsc = arr[4]
    
    print rtn_name "\t" static_icount "\t" tid "\t" call_count "\t" avg_tsc "\t" last_tsc
}
' "$INPUT" >> "$OUTPUT"

echo "Converted: $INPUT -> $OUTPUT"

