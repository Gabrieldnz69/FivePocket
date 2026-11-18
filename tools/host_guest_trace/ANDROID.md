# Native Android diagnostic

`run_android_trace.py` cross-builds the same portable C++ trace runner and all
private generated chunks for `arm64-v8a`. It executes the diagnostic once using
an explicitly selected ADB device. The function mapping is compiled with the
same exact host resolver as the macOS runner.

Pass the normal `run_trace.py` input/layout arguments plus `--ndk`, `--adb`,
`--serial`, and optionally `--api` (default 26):

```sh
python3 tools/host_guest_trace/run_android_trace.py \
  --ndk /absolute/path/to/android-ndk \
  --adb /absolute/path/to/adb \
  --serial EXPLICIT_DEVICE_SERIAL \
  --api 26 \
  local-analysis/generated/ppc \
  local-analysis/android-runtime-bridge/api26 \
  /absolute/path/to/decoded.image.bin \
  --expected-chunk-count 374 \
  --heap-base 0x10000000 --heap-size 0x04000000 \
  --stack-base 0x70000000 --stack-size 0x00010000 \
  --physical-base 0xA0000000 --physical-size 0x10000000 \
  --physical-address-offset 0 \
  --function-entry-budget 100000 --max-function-labels 64 \
  --cpu-time-seconds 10 --trace-timeout-seconds 30 \
  --output-dir local-analysis/android-host-guest-trace
```

The optional title, initial thread, and serialized cooperative-execution arguments documented in the macOS driver
are also accepted. All host input/range/mapping validation runs before building
or touching the device. `--validate-only` writes an ignored redacted report
without compiling, copying files, or executing on the device. An NDK, CMake,
Ninja, an authorized ADB connection, and an ARM64 Android device at or above the
target API are required for an execution run.

The driver creates one random, mode-0700 directory under `/data/local/tmp/`.
Only that diagnostic executable, its supervisor, the explicitly provided
decoded image, and optional raw XEX are copied there. Files have mode 0600,
executables mode 0700, and SHA-256 plus permissions are checked after transfer.
The C++ standard library is linked statically. Private files are never packaged
in an APK or copied to shared storage. The driver removes the four exact known
filenames and then removes the directory after execution. Failed cleanup is
recorded with the exact retry directory in the ignored report.

`android_trace_supervisor.c` runs on the device in a detached session and places
the worker in its own session/process group. It verifies core/file limits of
zero, an open-file limit of 32, and the requested CPU limit before `execve`.
Stdin is null, stderr is discarded, and the environment is allowlisted. A
64-KiB stdout cap and a monotonic wall deadline cause TERM then KILL of the
worker group. Group cleanup also runs after normal worker exit to remove
descendants. The supervisor ignores hangup signals so that losing the host ADB
connection does not remove the on-device deadline. This process boundary is a
diagnostic fault boundary, not an application security sandbox.

The ignored mode-0600 `trace-report.json` retains counts, public stop imports,
bounded function labels, checked-limit status, and temporary-file cleanup
status. Failed or invalid worker protocols have no entry count or stop import.
This tool runs generated guest startup code on Android, but it supplies no
Android application integration, game renderer, complete Xbox scheduler, or playable game.
Opt-in cooperative execution provides serialized worker entries, not unrestricted
parallel access to translated guest memory. Its worker/switch counters are
validated alongside the existing bounded trace protocol.

Asset-free synthetic tests cover native signal/CPU/wall stops, stdout flooding,
descendant cleanup, hangup persistence, protocol consistency, and exact cleanup
targets:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover \
  -s tools/host_guest_trace/tests -p test_android_trace.py -v
```
