# RTNPerf - Routine Performance Profiler

A Pin probe-mode tool for measuring function execution time using RDTSC (CPU timestamp counter).

## Features
- Lightweight profiling using Pin's probe mode (not JIT)
- Measures TSC cycles at function entry/exit
- Per-thread call counts and timing statistics
- Filter by image, routine name, or prefix

## Tool Knobs

### Required (one of these)
| Option | Description |
|--------|-------------|
| `-rtnperf:routine <name>` | Profile a specific routine by exact name |
| `-rtnperf:prefix <prefix>` | Profile all routines starting with prefix |
| `-rtnperf:include_file <file>` | File with prefixes to include (one per line) |

### Optional
| Option | Default | Description |
|--------|---------|-------------|
| `-rtnperf:image <name>` | (all) | Filter by image/binary name |
| `-rtnperf:outfile <file>` | `rtnperf.txt` | Output file |
| `-rtnperf:minicount <n>` | `1` | Only profile RTNs with >= n static instructions |
| `-rtnperf:exclude_file <file>` | | File with prefixes to exclude |
| `-rtnperf:demangled <mode>` | `name_only` | `none`, `name_only`, or `full` |
| `-rtnperf:verbose` | `0` | Print verbose information about probes |

## Usage

```bash
export PIN_ROOT=~/Tools/pin-external-4.0-99633-g5ca9893f2-gcc-linux

# Profile specific function
$PIN_ROOT/pin -t obj-intel64/RTNPerf.so \
    -rtnperf:image myapp.out \
    -rtnperf:routine foo \
    -- ./myapp.out

# Profile all functions with prefix
$PIN_ROOT/pin -t obj-intel64/RTNPerf.so \
    -rtnperf:image myapp.out \
    -rtnperf:prefix "my_func" \
    -- ./myapp.out
```

## Output Format

```
RTN_Name:  ( foo )  Static instruction count 625
 tid 0  Call count before 1 Average TSC for RTN 8673 tsc_entry_last ... tsc_exit_last ... last call TSC 8673
```

- **Static instruction count**: Number of instructions in the function
- **Call count before**: Number of times the function was called
- **Average TSC for RTN**: Average CPU cycles per call
- **last call TSC**: Cycles for the most recent call

## Language-Specific Notes

### C/C++
Function names match directly:
```bash
-rtnperf:routine foo           # Exact match
-rtnperf:prefix "my_"          # All functions starting with "my_"
```

### Mojo
Mojo compiles functions with module-prefixed names. Use the module name as prefix:
```bash
# For test-symdebug.mojo containing function foo:
-rtnperf:prefix "test-symdebug"    # Profiles test-symdebug::foo(), test-symdebug::main(), etc.
```

To find Mojo function names:
```bash
nm mojo.out | grep "::foo"
# Output: test-symdebug::foo(::List[::Int]&,::Int,::List[::Int]&,::List[::Int]&)
```

## Limitations

### Cannot probe [vdso] functions
Functions in `[vdso]` (Virtual Dynamic Shared Object) cannot be profiled with RTNPerf. The vDSO is a special kernel-mapped memory region designed for ultra-fast system calls (like `clock_gettime`) and does not support Pin's probe insertion.

If you see a function from `[vdso]` as a top procedure in ProcCount output, you cannot profile it with RTNPerf:
```
clock_gettime    [vdso]    0x7fff...    5312    421736   # Cannot be probed
```

**Workaround:** Profile functions from regular shared libraries or your binary instead.

### Probe insertion failures
Some functions may be too small or have incompatible instruction sequences for probe insertion. RTNPerf will print a warning:
```
WARNING: Cannot insert probe at <function_name>
```

## Building

```bash
export PIN_ROOT=~/Tools/pin-external-4.0-99633-g5ca9893f2-gcc-linux
make
```

