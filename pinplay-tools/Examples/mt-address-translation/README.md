# Multithreaded PinPlay address translation

This standalone example tests the semantics of
`PINPLAY_ENGINE::ReplayerTranslateAddress()` during SDE PinPlay replay. A
deterministic two-worker application publishes one stable stack address per
worker, then both workers call a synchronized point. The Pintool calls the
translation API from an executing analysis callback on each replay worker for
the same code, global, and cross-worker stack addresses, plus the caller's RSP.

The API must be called from an executing replay analysis callback. Calling it
from Pintool `main()` before `PIN_StartProgram()` is not a valid substitute:
there is no active replay Pin thread at that point.

This example uses SDE's existing PinPlay engine obtained from
`sde_tracing_get_pinplay_engine()`. It does not instantiate another
`PINPLAY_ENGINE`, use isimpoint functionality, or require an isimpoint.

## Build

Set `SDE_BUILD_KIT` to an SDE kit containing `sde64`, `pinkit`, and
`intel64/nullapp`:

```sh
make SDE_BUILD_KIT=/path/to/sde-external-10.13.1-... tools apps
```

The target is `obj-intel64/mt_address_translation_target.exe`; the Pintool is
`obj-intel64/mt_address_translation.so`.

## Record and replay

Record the complete multithreaded execution. Do not use
`-log:focus-thread`:

```sh
KIT=/path/to/sde-external-10.13.1-...
"$KIT/sde64" -log -log:mt -log:basename "$PWD/rec" -- \
  "$PWD/obj-intel64/mt_address_translation_target.exe"
```

Baseline replay:

```sh
MT_PROBE_OUT="$PWD/baseline.probe" "$KIT/sde64" -t64 \
  "$PWD/obj-intel64/mt_address_translation.so" \
  -replay -replay:basename "$PWD/rec" -replay:addr_trans -- \
  "$KIT/intel64/nullapp"
```

Forced non-identity replay with PinPlay translation-region diagnostics:

```sh
MT_PROBE_OUT="$PWD/forced.probe" "$KIT/sde64" -t64 \
  "$PWD/obj-intel64/mt_address_translation.so" \
  -replay -replay:basename "$PWD/rec" -replay:addr_trans \
  -xyzzy -replay:force_addr_trans -pinplay:msgon addrtx_info -- \
  "$KIT/intel64/nullapp"
```

The probe output columns are worker ID, Pin `THREADID`, category, logical
address, translated address, logical current RSP, and translated RSP.

## Expected interpretation

Physical addresses are ASLR- and run-dependent, so do not compare them to
fixed constants. The invariant is:

| Logical query | Worker 0 caller | Worker 1 caller |
|---|---|---|
| Code address | same translated address | same translated address |
| Shared/global address | same translated address | same translated address |
| Worker-0 stack address | same translated address | same translated address |
| Worker-1 stack address | same translated address | same translated address |

Baseline replay should report identity translations. Forced replay should
report non-identity translations where PinPlay has relocated a region. Both
workers must return the same translation for the same logical code, global,
worker-0-stack, and worker-1-stack address; each worker must also translate the
other worker's stack address correctly. Caller RSP values naturally differ
because the workers have different logical stacks, but the same logical RSP
must map identically regardless of which worker performs the query.
