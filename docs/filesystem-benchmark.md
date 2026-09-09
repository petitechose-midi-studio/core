# Filesystem and UX hardware qualification

`dev_filesystem_benchmark` extends the autonomous UX benchmark with the real SD
filesystem endpoint. It is an explicit qualification profile, excluded from
normal product builds. Settings and the versioned musical fixture remain in RAM;
session restore/autosave and serial log formatting remain disabled.

All filesystem paths, including atomic journals and recovery, are prefixed with
`/ms-rpc-bench` by `BenchFileSystem`. The host must use an owned run directory and
verify it is absent before preparing a run. The namespace is retained across
resets; flashing does not clean it. Traversal, backslashes, drive prefixes and
trailing dot/space aliases are refused. The backend's 192-byte physical path bound
includes this prefix, so qualification paths have less room than product paths.

The benchmark owns the physical frame callback and exposes an ITransport view to
the filesystem endpoint. Only FC requests go to that endpoint; UX control traffic
during a run still contaminates the result. `ram_only` is false when filesystem
routing is installed, and `filesystem_requests` counts frames during measurement.
The existing RAM-only host runner deliberately rejects this profile.

Filesystem operations remain stopped-only. A combined scenario must negotiate
before playback, verify BusyPlaying refusals during playback, issue STOP, and
then perform accepted transfers while UX actions continue. Do not bypass this
guard to produce concurrent-transfer performance claims. `main.filesystem-rpc`
records the foreground endpoint/catalog advancement scope; `main.loop`, sequencer
and MIDI metrics remain available. Memory results are snapshots after cleanup,
not measured peak allocation.

Compare baseline/current/baseline using the same dependency snapshots, platform
packages, build flags, SD namespace, fixture, UX script and optimized host clients.
Bind the device serial, firmware content identity, HEX hash, host executable hashes
and operation content checks to each result. Keep cold startup and host USB
reconnection timing separate from steady-state firmware CPU measurements. The
post-link gate requires the scoped SD backend and still forbids settings SD,
session restore/autosave and log reporters in this profile.
