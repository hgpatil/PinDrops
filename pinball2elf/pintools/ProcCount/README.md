# Tool for RTN profiling across forks/execs

## Tool knobs
### -msgfile  [default msg.out]
        tool messages
### -outdir  [default .]
        Output directory

## Relevant Pin knob
Also use the pin knob `-follow_execv`
### -follow_execv  [default 0]
  Execute with Pin all processes spawned by execv class system calls

## Output format
The tool outputs a file `proccount.<pid>.out` with fields separated by `#\t` (hash + tab):
```
Procedure#	Image#	Address#	Calls#	Instructions
```

## Helper scripts

### proccount2tsv.sh
Converts the raw output to a clean tab-separated file for Excel import.

**Usage:**
```bash
./proccount2tsv.sh <input.out> [output.txt]
```

**Example:**
```bash
./proccount2tsv.sh proccount.12345.out myoutput.txt
```

This:
- Skips the first `MAIN#` line
- Removes trailing `#` from each field
- Outputs clean tab-separated values

**Importing into Excel:**
1. Open the `.txt` file in Excel
2. Use **Tab** as the delimiter
