# Guarded macOS host trace

`run_trace.py` builds private generated PPC sources into a native macOS
executable, loads one explicitly named decoded guest image, binds the portable
guest-import registry, and invokes `_xstart` inside `GuestExecutionGuard`. It is
a bounded investigation tool, not a game launcher or an Android runtime.

The same C++ runner also supports a separate [native Android diagnostic](ANDROID.md)
with an explicit ADB device and on-device process supervision.

Stopped physical snapshots retain the default 4 KiB aggregate limit. Explicit
`--capture-stopped-byte-budget` can select an aligned limit up to 2 MiB, only with
one to eight explicit disjoint `--capture-stopped-range ADDRESS BYTES` ranges.
Ownership checks and all-or-nothing capture remain. Android supervision derives
a bounded output allowance from that budget; no opt-in leaves its 64 KiB cap
unchanged. Captured payloads are private ignored artifacts, never report text.

## Optional serialized thread execution

With title, initial-thread, and created-thread-pool staging enabled, add
`--thread-type-base 0x62002000 --cooperative-threads --thread-quantum 32`.
The type binding requires the parser-verified `ExThreadObjectType` data import.
It is a reversible opaque identity, not a complete kernel object descriptor.

`NtResumeThread` enqueues a real translated entry on its own host call stack,
with a separate PPC context, guest stack, PCR, and TLS. A single execution gate
permits only one guest task to run at a time. Function-entry checkpoints rotate
tasks and rebind the registry's current-thread token. The XAPI trampoline receives
the start routine and context; direct starts receive the context alone.

The shared entry budget and first guard stop cancel all tasks; every worker is
joined before callbacks, registry state, or guest memory are released. Reports
include workers created, workers entered, and context switches. Default limits
are one initial task plus 64 workers; the quantum must be 1–1,000,000 entries.
The per-run function-entry budget accepts1–10,000,000 entries (default10,000).
Larger startup runs still require an explicit budget; CPU, wall-clock, mapping,
worker and retained-label limits are independent and unchanged.
Without this opt-in mode, resume is rejected rather than reported as successful.

Singleton virtual affinity updates guest CPU identity (six virtual CPUs, default
CPU 0 for created threads); it does not pin physical host cores. Only a validated
debugger thread-name notification is consumed by `RtlRaiseException`; all other
exceptions stop. Names are bounded to 255 bytes and never logged or serialized.

Infinite non-alertable waits can park a task while others run. Semaphore and
auto-reset event signals are consumed once; manual-reset events and completed
threads remain signaled. Negative relative timeouts up to 60 seconds use host
monotonic deadlines and return timeout only on expiry. Absolute and alertable
waits still reject. A closed waited-on
handle rejects instead of being silently retargeted. No runnable producer or
wait-budget exhaustion returns a guard stop, never invented success. If the
last runnable task finishes with others parked, the report says `no-runnable-task`.
`wait_operations` counts attempts, including cancelled waits, not completed wakes;
its cap equals the function-entry budget. No readiness callback survives unwind.

Normal thread-priority increments -2..2 weight per-task checkpoint slices by
1/4, 1/2, 1, 2, or 4, with minimum one checkpoint. All runnable tasks rotate;
lower-priority tasks do not starve. Suspended priority is carried into enqueue,
and live changes require the explicit scheduler callback. Failed updates do not
change metadata; repeated writes cannot reset a consumed slice. Reports name
this policy; it is not strict Xbox priority ordering or a host OS priority change.
Priority rejection diagnostics retain only signed increments and Boolean state,
never guest addresses, names, or raw memory.

This is diagnostic scheduling, not a full Xbox scheduler. APCs,
priority saturation/dynamic boosts and preemption within a translated function are not provided.
Host thread stack sizes are platform defaults; guest stacks have explicit bounds
but no guard-page enforcement. The outer process watchdog is still required.
Returned-thread storage is retained until reset, and thread-exit imports and APC
rundown remain separate work. This mode is not connected to asset-free APK startup.

The generated PPC directory, guarded bridge directory, and output directory
must all resolve below this repository's ignored `local-analysis/` directory.
The decoded image must be an explicitly named absolute path to a regular,
non-symlink file with exactly the size declared by the private `ppc_config.h`.
No private generated source, bridge source, object, executable, decoded image,
or report is committed.

Builds copy validated PPC inputs into a stable private build subdirectory using
content comparison. Moving to a newly generated corpus therefore recompiles only
changed chunks (or dependencies affected by header/compiler-option changes).
All original validation still runs, no prebuilt object is trusted, and input
corpora remain untouched. Symlink/directory staging targets are rejected before
copying; unrelated files are preserved and never globbed into the target. The
first build after enabling staging rebuilds once to establish stable paths.

On macOS, run:

```sh
python tools/host_guest_trace/run_trace.py \
  local-analysis/generated/ppc \
  local-analysis/android-runtime-bridge/api26 \
  /absolute/path/to/decoded.image.bin \
  --expected-chunk-count 374 \
  --heap-base 0x10000000 \
  --heap-size 0x04000000 \
  --stack-base 0x70000000 \
  --stack-size 0x00010000 \
  --physical-base 0xA0000000 \
  --physical-size 0x20000000 \
  --physical-address-offset 0 \
  --function-entry-budget 10000 \
  --cpu-time-seconds 10 \
  --max-function-labels 64 \
  --output-dir local-analysis/host-guest-trace-run
```

All heap and stack values are mandatory. The three physical-memory arguments
are optional but all-or-none. They bind a separate page-aligned guest arena and
the physical address corresponding to its base; both the guest and translated
physical ranges must fit in 32 bits. The driver rejects overlap among the image,
heap, stack, physical arena, and any staged title structures. Image base and
size are read from the generated configuration rather than embedded in the
tracked tool. Physical-memory support remains opt-in so older diagnostic runs
can still stop at the first physical import rather than silently inventing an
arena.

Before building, the driver also validates the generated `PPCFuncMappings`
source: it must contain 1–1,000,000 strictly increasing, unique, four-byte
aligned addresses inside the configured PPC code range followed by exactly one
canonical null sentinel. The native runner repeats these checks against the
compiled table. Indirect branches use an exact binary search over that host
table; the generated pointer-table location in guest memory is never written.
An absent or non-exact target becomes the structured, address-redacted
`invalid-indirect-target` guard stop.

Use `--validate-only` to validate the private inputs and write a redacted report
without building or executing. A normal run requires CMake and Ninja and emits
`trace-report.json` in the ignored output directory. The report contains
counts, public Xbox import names, bounded priority-state diagnostics, explicitly bounded generated function labels,
and coarse build/stop status. When enabled, the function-label window retains
the most recent labels so a stop can be localized without recording an
unbounded trace. Guest debug text and private paths are not kept. Set
`--max-function-labels 0` (the default) to omit function labels.

Newly generated guarded bridges also capture r1 and r3-r10 immediately before
an unsupported/rejected import stops. All 64 bits are preserved without following
pointers; floating-point arguments, stack slots, LR and full CPU state are not
captured. The terminal worker's snapshot survives cooperative cancellation and
is cleared between runs. These values are **private diagnostics**, retained only
in the ignored mode-0600 report, not safe-to-publish aggregate metadata. Do not
upload reports automatically. Older bridges can legitimately omit the group.

Scalar load/store macros in the native trace now stop before accessing the
64-KiB GPU register window at 0x7FC80000 or crossing the 4-GiB address boundary.
They preserve a private address/width/store-value/direction/region snapshot.
Ordinary scalar RAM transfers use explicit big-endian conversion and unaligned-
safe copies. This is not a complete address-space model: vector/atomic/direct
pointer accesses, other hardware windows, allocation ownership and protection
are not covered. No register access is completed or GPU interrupt invented.

## Optional title environment

Title-environment staging is opt-in. Add all four title arguments to a normal
or `--validate-only` invocation:

```sh
  --title-xex /absolute/path/to/default.xex \
  --title-header-base 0x60000000 \
  --title-export-address 0x62000000 \
  --title-module-base 0x62001000
```

The driver rejects a relative path, symlink, non-regular raw XEX, or raw file
larger than 512 MiB. It parses the raw XEX and decoded image together with the
tracked `xex_info` schema-2 parser, requires exactly one `xboxkrnl.exe` data
import with ordinal `0x193`, and checks the parser's image base against the PPC
configuration. A decoded image may be smaller than the XEX security image due
to its zero/BSS tail; scratch overlap checks use the full security image size.

The native runner reopens the same regular file without following a symlink,
revalidates its size, and copies only the parser-declared XEX header span into
the explicit header scratch range. It then initializes the validated import
slot to point through the explicit exported-variable address to a zeroed
`0x64` title-module record containing the header pointer. Image, heap, stack,
header, exported-variable, and module ranges must be inside the 32-bit guest
space and may not overlap. The report records only sizes, scratch ranges, and
the public import name/ordinal; it does not retain the XEX path or bytes.
Keeping the indirect resolver host-side also prevents its pointer storage from
colliding with a security-image zero/BSS tail.

Before `_xstart`, the runner binds one explicit nonzero guest thread token
derived from inside the already validated stack range. This permits only the
portable registry's cooperative same-owner critical-section recursion and
release; it does not create a guest thread, block on contention, or provide
multi-thread scheduling.

The ignored output directory is restricted to mode `0700` and reports to
`0600` where supported by the host filesystem.

## Optional guest thread environment

With title staging enabled, these three arguments initialize the initial
thread's PCR, minimal thread record, and TLS storage:

```sh
  --thread-pcr-base 0x63000000 \
  --thread-record-base 0x63001000 \
  --thread-tls-base 0x63002000
```

The XEX metadata parser supplies the static template address/size, static TLS
size, and dynamic slot count. The runner checks them again against the staged
XEX header, copies only the declared template from the loaded image, zeros the
remaining TLS storage, and sets `r13` to the PCR. The PCR points to static TLS;
dynamic `KeTls*` slots start immediately after that static region. All scratch
ranges must be disjoint from the full title image, heap, physical arena, stack,
header, and module structures. The thread record provides only known identity,
stack, and TLS fields; it does not create a scheduler or support blocking waits.

For the supplied title, the static template is 344 bytes and there are 64
dynamic slots. With physical/thread staging, an explicit created-thread arena,
the 68-import registry, 341 recovered switch tables, guarded trap instructions,
priority-weighted cooperative execution, explicit read-only installed packages
and `--virtual-video-mode 720p60`, a 1,000,000-entry budget reaches 798,406 function
entries and stops at `VdInitializeEngines` on macOS and Android. A
100,000-entry budget now stops on its configured limit. Guest labels retain
only the requested trailing window. See `docs/fable/CONTEXT.md` for the exact
current private-input paths and full reproduction command.

Use a 512 MiB physical arena for the observed title allocation sequence. The
older 256 MiB example could not fit the second 368 MiB allocation and the title
constructed a heap from the null return. Previous high entry counts under that
configuration do not establish valid startup. The current capacity regression
tests both cases. No complete hardware memory map is claimed.

`MmQueryStatistics` uses an explicit **independent native arenas** contract:
physical capacity/free/allocated pages come from the bound physical allocator;
virtual capacity/reservations/commits come from the portable virtual allocator.
Page statistics are in 4-KiB units, including large-page allocations. Host kernel,
image, worker stack, page-table and file-cache storage is outside these arenas;
those guest categories are zero, not guessed Xbox usage or measured host RSS.
The physical and virtual budgets are independent, not a shared 512-MiB console
pool. The complete 104-byte big-endian result is initialized on success.
`execution.native_memory_statistics` records the last successful guest query
and labels these limitations. An absent field means no successful query was
observed, not zero usage. Commit tracking preserves data on overlapping/repeated
commits and clears on release/reset. Protection is enforced for statistics
outputs within the managed arenas; generic non-arena guest memory still uses
the existing bounded address-space/thread-storage checks, not a complete MMU.

`--xex-image-protections` additionally snapshots the supplied title XEX security
page descriptors for `MmQueryAddressProtect`. It requires a complete matching
64-KiB-page image in80000000..90000000, validates every descriptor before binding,
and preserves unknown-region guards. Code/read-only descriptors report read-only;
data descriptors report read/write, with no invented cache modifier. This is
not PE-section inference, signature verification, or host memory enforcement.
Image metadata is immutable until registry reset. Statistics outputs in a bound
image must cover only writable pages. `execution.xex_image_protections` labels
the source and limitations; without the flag, image queries remain guarded.

`--virtual-video-mode 720p60` explicitly configures software video-query state.
Without it, `XGetVideoMode` guards. It returns the public 48-byte mode structure
with reserved words zero; it neither queries the physical display nor creates
a graphics device. The report records that distinction.

## Read-only metadata and explicit file content

`file_metadata_catalog.py --root PRIVATE_EXTRACTED_DIRECTORY --mount game:
--output PRIVATE_CATALOG` snapshots one explicitly selected tree. Both parsers
validate bounded canonical ASCII paths, directory parents, record counts, sizes
and attributes. Symlinks, special files, case-fold collisions and inspection
errors fail the snapshot. Add `--file-metadata-catalog ABSOLUTE_PRIVATE_CATALOG`
to the trace. This metadata uses observed host times and explicit 2,048-byte
logical allocation rounding, not original Xbox volume metadata. Unknown mounts
guard; only absence inside the complete selected snapshot reports not-found.

The native runner also accepts `--native-cache-directory ABSOLUTE_DIRECTORY`
after its `--read-only-file` arguments and before `--native-xam-module`.
This explicitly mounts a live, read-only native directory as `cache:`. It opens
real files and reports real absence, rejects symlinks/traversal and ambiguous
case aliases, and does not create files or implement cache writes. Other drives
remain bound through their existing catalogs; no game filename is special-cased.

`--native-update-directory ABSOLUTE_DIRECTORY` similarly binds `update:` to a
separate read-only native directory. Place it after the optional cache argument
and before `--native-xam-module`. The directory must already exist; the runner
does not download, create, patch, or assume any title-update files. An absent
file returns not-found from the actual directory; an unbound/unsafe namespace
still guards. The disc's system-update payload is not mounted as a title update.
`guest_native_read_only_directory_test` checks absent/present update metadata,
preserved failure outputs, real reads, namespace isolation and symlink guards.

`--native-game-cache-directory ABSOLUTE_DIRECTORY` binds a separate `gamecache:`
with the same read-only contract, after the optional update-directory argument.
The Xbox `cache:` and game's `gamecache:` are distinct roots, not aliases.
Neither mode implements cache writes or invents precomputed game-cache contents.

Add `--read-only-file RECORD_INDEX ABSOLUTE_PRIVATE_FILE` for each desired
content source, up to sixteen files, 8 GiB each. Record indices are one-based in
the catalog's sorted canonical paths and must be rechecked if the tree/catalog
changes. Native setup validates size/type and preopens only those exact files.
Guest paths can select among the explicit bindings but cannot open arbitrary
host paths. Every logical file handle has its own cursor. Reads are capped at
1 MiB, use host scratch before publication, and expose real byte counts/EOF.
Failed source reads preserve guest outputs and cursor. File close/reset releases
logical ownership; preopened input sources live only for the diagnostic process.

`--installed-content-catalog PRIVATE_CATALOG` supplies bounded XCONTENT_DATA
records. Add `--installed-package CONTENT_INDEX PRIVATE_FILE_CATALOG` and an
`--installed-file CONTENT_INDEX FILE_INDEX PRIVATE_FILE` for every regular file
in each package. Read-only mounts are available only for fully bound packages.
The installed-content and STFS inventory tools derive metadata and validated
payloads from explicitly supplied local packages. No license grant, download,
writable content, host-path fallback or unverified partial mount is provided.

File queries support position, byte alignment and snapshot metadata. Xbox
physical-sector queries explicitly fail. Set-information only changes a read
cursor; no writes, delete, truncate, unbuffered, APC, event/port completion or
directory enumeration is provided. Nonzero ApcContext is unused only when no
routine or completion-port association exists. Reports keep open/read/byte/seek
counts and bounded create-request flags/record indices, not paths or data.

Android normally stages and removes exact per-run input files. For repeated
large inputs, create an owned private `/data/local/tmp/recomp-content-cache.XXXXXX`
directory with `mktemp -d`, then supply `--device-content-cache EXACT_DIRECTORY`
to the Android driver. Cache files are source-SHA-256 named, mode 0400 under
0700, checked for type/owner/hash before reuse and hard-linked into the fresh
run directory. Existing entries are never overwritten or auto-deleted. Only
the helper's uniquely named partial upload can be rolled back. Reports disclose
that cache content remains even after per-run cleanup. Keep the exact owned
cache path for deliberate later cleanup; never recursively delete broad device
directories. Cache state affects transfer time only, not guest semantics.

## Trap preservation and asset-free adapter check

Xenon patch 0012 implements TW/TWI/TD/TDI conditions from raw instruction fields,
including signed immediates and register zero. Trace builds override
`PPC_RUNTIME_TRAP` with the structured `guest-trap` stop. A true condition cancels
all workers; this does not implement guest SEH or ignore assertions. Before
building, the driver requires a trap hook after every emitted trap annotation
and the condition helper in the copied runtime header. Older trap-skipping
corpora are rejected. The current corpus validates 3,072 annotations; these are
not necessarily unique guest instruction addresses because functions can overlap.

The asset-free adapter test can be built against a patched public runtime header
or its identical private generated copy. For Android API 26:

```sh
RECOMP_CXX=/opt/homebrew/share/android-commandlinetools/ndk/27.2.12479018/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android26-clang++
"$RECOMP_CXX" -std=c++20 -O1 -pthread -DSIMDE_ENABLE_NATIVE_ALIASES \
  -static-libstdc++ -Iandroid/app/src/main/cpp \
  -Ilocal-analysis/repro-guarded-traps/ppc -Itools/XenonRecomp/thirdparty/simde \
  android/app/src/main/cpp/guest_execution_guard.cpp \
  android/tests/guest_trap_adapter_test.cpp \
  -o local-analysis/android-thread-storage-tests/api26/guest-trap-adapter-test
```

Run only that synthetic binary under a bounded device timeout in a fresh private
ADB temporary directory, then remove the exact staged binary and directory.
The test uses no game image or assets and covers TO masks, signed/unsigned
widths, register zero and the runtime guard hook. It is not part of APK startup.

## Partial native XAM and clock/network bindings

With cooperative title/thread staging, `--xam-module-base 0x62003000` binds a
zero-validated resident record. The parser-validated title XAM minimum selects
the compatibility target; this does not run Xbox firmware. A bounded allowlist
of optional online exports returns entry-point-not-found; unknown exports stay
guarded. Legacy XNet configuration and Winsock 2.x negotiation/cleanup have real
bounded state, but no sockets, DNS or traffic. Notifications begin empty and
only explicit host ingress posts events; no sign-in/controller events are invented.

Generated TB/TBU reads use `PPC_READ_TIMEBASE()` and one process-wide monotonic
50 MHz epoch, shared across native threads. `KeQueryPerformanceFrequency` returns
the same 50,000,000 rate; setup checks the actual registry dispatch. Exact integer
nanosecond division avoids host frequency differences and floating-point drift.
System time separately uses a host UTC anchor plus monotonic elapsed FILETIME
ticks. This is fixed-rate compatibility, not CPU-cycle accuracy, Xbox boot epoch,
save-state/pause semantics or validation of the unknown GPU clock register.
Source validation rejects stale raw `__rdtsc` chunks or missing hook support;
regenerate with Xenon patch 0013 before tracing. Reports record validated source
sites and observed CPU timebase reads, without retaining timer values.

## Optional native XMA audio decoder

Android trace builds may opt into Xenia's real FFmpeg `xmaframes` decoder.
`build_xma_decoder.sh` takes an already-fetched checkout at the pinned revision,
a new build directory, and the Android NDK root. FFmpeg requires paths without
spaces; use a task-owned temporary source alias if needed. Configure the trace
target with `NATIVE_XMA_FFMPEG_DIR` pointing to that generated build tree and
`NATIVE_XMA_ARCHIVE_DIR` containing its `libavcodec.a` and `libavutil.a`.

With a physical arena, the runner allocates and pins the real 320-context pool
before entering guest code. XMA register commands synchronously decode under the
serialized execution gate into the game's big-endian PCM ring. This is separate
from the native output stream: original guest audio code still consumes/mixes
that PCM. Unknown register modes, partial-subframe loops, and hardware interrupt
modes remain guarded. The private trace records decoded-frame/PCM-byte counters
and the context words at a rejected decode boundary.

Changing the scalar MMIO aperture requires rebuilding the translated corpus,
not merely relinking the native decoder. Authored-frame device tests are not
evidence that GTA's audio, title screen, or gameplay works. See the local handoff
for the most recent real game replay. Xenia's BSD attribution is in
`licenses/Xenia-BSD-3-Clause.txt`; the pinned FFmpeg dependency is LGPL.

## Safety boundary

### Optional created-thread arena

With title and initial-thread staging, add both `--thread-pool-base` and
`--thread-pool-size` to enable the bounded `ExCreateThread` implementation.
The arena must be page-aligned, disjoint from every existing range, large enough
for one minimum stack plus metadata, and at most 256 MiB. The worker obtains
the default stack size from the validated XEX optional header `0x20200`.
The startup diagnostic uses `--thread-pool-base 0x64000000 --thread-pool-size 0x02000000`.
Android accepts the same arguments. Without the opt-in binding, thread creation
stops as `rejected-import`; no arena or stack default is silently invented.

Only flags 1 (suspended) and exact mapped entry addresses are supported. No new
thread runs during creation, and handle close does not terminate it. The separate
cooperative opt-in mode provides resume and infinite non-alertable waits.

### Worker containment

The prologue hook cooperatively limits generated function entries, and guarded
missing-import definitions turn unsupported Xbox calls into structured stops.
Partially implemented imports reject unsupported requests with `rejected-import`
and a bounded public import name; this is distinct from `unimplemented-import`.
Neither path invents success or changes the guest return register on rejection.
Guest execution occurs once in a child placed in a new session/process group.
It receives a fixed allowlisted environment and working directory, null stdin,
discarded stderr, and a stdout protocol capped at 64 KiB. The launcher disables
core and regular-file output, limits open files to 32, and applies the explicit
CPU-time limit. The parent enforces the wall limit by sending TERM and then KILL
to the whole process group, including descendants.

Every execution attempt produces a schema-2 parent report with a redacted
outcome: clean completion, guard stop, Unix signal, CPU limit, wall timeout,
protocol limit, launch failure, or invalid protocol. A worker that crashes or
fails its protocol cannot contribute a function count or import name; those
fields remain null. No RSS limit is claimed because Darwin does not provide a
reliable hard per-child resident-memory limit suitable for this 4 GiB guest
reservation.

The process boundary contains a native memory fault to the trace worker, but it
does not make guest execution correct, recover the failed process, or provide a
security sandbox. The resulting executable is a local trace artifact, is not
wired to JNI, and must not be represented as runnable guest support.

Synthetic tests require no game files:

The loading validation workflow is documented in `docs/TESTING_WORKFLOW.md`.
`run_loading_test_batch.py` accepts reviewed schema-2 manifests under ignored
`local-analysis/`. `--check` validates all steps/inputs without running a test or
ADB command. Final batches require focused, capture and regression evidence;
all non-build steps declare `expect` regexes. Empty CTest/unittest selections
fail even if the process returns zero. CTest commands use `--no-tests=error`.
Build steps name exact CMake targets; host checks come before device checks.

Each device step declares `kind: device` and `device_inputs` entries mapping
local private artifacts to exact `/data/local/tmp/` paths. The top-level
`device` records the canonical ADB path, serial, package and previous request.
Before the probe, the runner checks terminal state, device build/ABI/boot ID
and every declared remote SHA. A remote timeout ends inside the host deadline.
There is no install, game launch, input injection or automatic retry here.

Reports are checkpointed atomically before/after each step and retain failures.
`--resume OLD-REPORT --output NEW-REPORT` may reuse only explicitly declared
read-only host focused/regression steps (`reusable: true`) with unchanged
manifest, sources, library, tools, environment and output hashes. Capture,
device and build steps rerun. This is interruption recovery, not a dependency
cache for changed code. Declare invoked toolchain executables in `tools` and
non-public inputs in `additional_inputs`; missing dependencies cannot be inferred.
The installer rejects legacy schema-1 reports for future installs. Preserve
those historical reports; generate new evidence rather than rewriting them.

Future installation also requires `--for-install --check` and a passing
`--for-install` batch. Its `coverage` contract names the actual cases, exact
input identities, a connected check and per-case output markers included in
the step's `expect` list. `case_evidence` declares captured versus synthetic
inputs. Synthetic input cannot satisfy a captured case; missing variants need
an explicit `deferred_reason`. `purpose: integration` requires all cases;
`diagnostic` retains the unknowns and a rationale; `component` never authorizes
an installation. This is declared test coverage, not a correctness oracle.

Each private probe needs a `probe_builds` mapping of `output` to `receipt`.
`build_loading_probe.py` builds from a reviewed private JSON manifest containing
schema 1, explicit compiler `command`/`output`, `additional_inputs`, and declared
`tools`/`toolchain_inputs` for implicit link inputs. It freshly preprocesses
each translation unit for compiler-discovered dependencies, including system
and optional headers. `--check` performs read-only preparation only. A new
`--receipt` preserves evidence; `--reuse OLD-RECEIPT` skips compilation only for
an identical build and output. Changed/missing receipts require a new output
path; an existing unproven binary is never blessed after the fact. Runtime
capture files belong to the test manifest, not a compiler receipt unless they
are actually embedded. No hardware test result is reused by this build cache.

Android audio is separately opt-in. `--experimental-native-audio` opens an
output-only AAudio stream and uses the experimental reference speaker word;
it alone does not start playback. Add `--experimental-native-audio-renderer`
with cooperative threads to enable one backed, wrapped-context callback worker
and owned PCM snapshots. The native policy converts six planar BE float channels
to normalized stereo without LFE, bounds its queue, and paces callbacks from real
service-read counters. It inserts no silence. Additional clients, unregister,
volume controls and console scheduling/mixer equivalence remain unsupported.
The report separates registration, callback entries/completions, accepted PCM,
native writes and service reads; none alone proves physical audibility or gameplay.

`--native-event-wake-boost` separately opts into an increment-one event wake
policy on the serialized scheduler. Eligible waiters receive one extra bounded
slice tier until their next scheduling boundary/blocking wait; boost-disabled
threads are excluded. Signal and boost publication is transactional. Larger
increments, signal-then-wait IRQL, host OS priority changes and Xbox dispatch
equivalence are not implemented. Its report includes request/boost/suppression
counts even when all are zero.

`--native-category1-unity-gain` requires the native renderer and declares an
immutable application policy for numeric category1: gain1.0, no category-level
attenuation. The original title consumes the returned BEfloat; the host does
not multiply mixed PCM a second time. This is not Android system volume or
verified Xbox dashboard state. Other categories, category names, ducking and
volume-change masks remain unsupported. Reports include the policy andquery count.

```sh
PYTHONDONTWRITEBYTECODE=1 \
  python -m unittest discover -s tools/host_guest_trace/tests -v
```
