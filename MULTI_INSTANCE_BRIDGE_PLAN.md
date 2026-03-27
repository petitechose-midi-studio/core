# Multi-Instance Bridge Plan

## Goal

Make the controller stack reliable with multiple identical controllers connected at the same time.

Target properties:

- each physical controller is identified by a stable hardware identifier
- each logical bridge instance is bound to exactly one physical controller
- COM port changes do not break bindings
- multiple `oc-bridge` processes can run simultaneously
- each bridge instance has isolated state, control port, log port, host port, and lock file
- `ms-manager` keeps autodetection UX, but persists the selected controller binding
- Bitwig can run multiple hardware-facing instances in parallel without all of them targeting port `9000`

The stable hardware identifier must be the USB serial number exposed by the controller.

## Current Problems

### `oc-bridge`

- serial autodetection only matches on `VID/PID`
- a matching device is selected by broad compatibility, not by identity
- daemon lock is global: `oc-bridge.lock`
- config is global and single-instance
- control plane info does not expose enough identity to let a supervisor reason about multiple instances

Relevant files:

- `C:\Users\miu-lab\ms-dev-env\open-control\bridge\src\cli.rs`
- `C:\Users\miu-lab\ms-dev-env\open-control\bridge\src\config.rs`
- `C:\Users\miu-lab\ms-dev-env\open-control\bridge\src\transport\serial.rs`
- `C:\Users\miu-lab\ms-dev-env\open-control\bridge\src\instance_lock.rs`
- `C:\Users\miu-lab\ms-dev-env\open-control\bridge\src\control.rs`
- `C:\Users\miu-lab\ms-dev-env\open-control\bridge\src\bridge\runner.rs`
- `C:\Users\miu-lab\ms-dev-env\open-control\bridge\src\main.rs`

### `ms-manager`

- supervises one global bridge daemon
- persists firmware-flash info only, not controller bindings
- bridge status API is singleton-oriented
- already knows `serial_number`, but does not use it to bind bridge instances

Relevant files:

- `C:\Users\miu-lab\ms-dev-env\ms-manager\src-tauri\src\services\bridge.rs`
- `C:\Users\miu-lab\ms-dev-env\ms-manager\src-tauri\src\services\bridge_status.rs`
- `C:\Users\miu-lab\ms-dev-env\ms-manager\src-tauri\src\services\device.rs`
- `C:\Users\miu-lab\ms-dev-env\ms-manager\src-tauri\src\models.rs`
- `C:\Users\miu-lab\ms-dev-env\ms-manager\src-tauri\src\state.rs`
- `C:\Users\miu-lab\ms-dev-env\ms-manager\src-tauri\src\layout.rs`
- `C:\Users\miu-lab\ms-dev-env\ms-manager\crates\ms-manager-core\src\controller_state.rs`

### Bitwig host

- bridge connection is modeled as fixed presets `9000`, `9001`, `9002`
- hardware mode is hard-coded to `9000`
- MIDI port autodetection uses a fixed hardware name, which is ambiguous if two controllers expose the same USB MIDI name

Relevant files:

- `C:\Users\miu-lab\ms-dev-env\midi-studio\plugin-bitwig\host\src\midistudio\MidiStudioExtension.java`
- `C:\Users\miu-lab\ms-dev-env\midi-studio\plugin-bitwig\host\src\midistudio\MidiStudioExtensionDefinition.java`
- `C:\Users\miu-lab\ms-dev-env\midi-studio\plugin-bitwig\host\src\protocol\Protocol.java`
- `C:\Users\miu-lab\ms-dev-env\midi-studio\plugin-bitwig\src\name.c`

## Target Model

### Identity model

- `controller_serial`: stable identity of a physical controller
- `instance_id`: stable identity of a logical bridge instance

Rules:

- one bridge instance binds to exactly one `controller_serial`
- one `controller_serial` may be associated with one or more app-level instances only if that is explicitly desired later
- no bridge instance may fall back to a different controller when its target serial is absent

### Persistence model

Introduce a new persistent file in `ms-manager`:

- `state/bridge_instances.json`

Suggested schema:

```json
{
  "schema": 1,
  "instances": [
    {
      "instance_id": "bitwig-hw-17081760",
      "app": "bitwig",
      "mode": "hardware",
      "controller_serial": "17081760",
      "controller_vid": 5824,
      "controller_pid": 1161,
      "host_udp_port": 9000,
      "control_port": 7999,
      "log_broadcast_port": 9999,
      "enabled": true
    }
  ]
}
```

Notes:

- `controller_serial` is the key used for hardware binding
- `controller_vid` and `controller_pid` are validation metadata, not the primary identity
- ports are explicit and per-instance
- this state is separate from `controller.json`, which currently stores flash history only

## Implementation Plan

## Phase 1: `oc-bridge` multi-instance foundation

### 1. Extend CLI and config

Files:

- `open-control/bridge/src/cli.rs`
- `open-control/bridge/src/config.rs`
- `open-control/bridge/src/main.rs`
- `open-control/bridge/config/default.toml`

Add CLI options:

```rust
#[arg(long, value_name = "ID")]
pub instance_id: Option<String>;

#[arg(long, value_name = "SERIAL")]
pub serial_number: Option<String>;
```

Extend `BridgeConfig`:

```rust
pub struct BridgeConfig {
    pub instance_id: Option<String>,
    pub serial_number: Option<String>,
    pub controller_transport: ControllerTransport,
    pub serial_port: String,
    pub device_preset: Option<String>,
    pub controller_udp_port: u16,
    pub controller_websocket_port: u16,
    pub host_transport: HostTransport,
    pub host_udp_port: u16,
    pub host_websocket_port: u16,
    pub log_broadcast_port: u16,
    pub control_port: u16,
}
```

Default behavior:

- `instance_id = None`
- `serial_number = None`

`main.rs` must apply CLI overrides in this order:

1. load config
2. apply `--instance-id`
3. apply `--serial-number`
4. apply `--port`
5. apply `--udp-port`
6. apply control/log overrides

Decision:

- `serial_number` is the preferred persistent selector
- `serial_port` remains supported as a manual override for debugging and one-off workflows

### 2. Replace broad autodetection with identity-aware matching

Files:

- `open-control/bridge/src/transport/serial.rs`
- `open-control/bridge/src/bridge/runner.rs`
- `open-control/bridge/src/config.rs`

Introduce a match request type:

```rust
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct SerialMatchRequest {
    pub serial_number: Option<String>,
}
```

Introduce a richer serial candidate type:

```rust
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SerialDeviceCandidate {
    pub port_name: String,
    pub serial_number: Option<String>,
    pub manufacturer: Option<String>,
    pub product: Option<String>,
    pub vid: u16,
    pub pid: u16,
}
```

Refactor detection into smaller pure functions:

```rust
fn candidate_from_port(port: &SerialPortInfo) -> Option<SerialDeviceCandidate>;

fn matches_device_config(candidate: &SerialDeviceCandidate, config: &DeviceConfig) -> bool;

fn matches_request(candidate: &SerialDeviceCandidate, request: &SerialMatchRequest) -> bool;

fn select_candidate<'a>(
    candidates: &'a [SerialDeviceCandidate],
    config: &DeviceConfig,
    request: &SerialMatchRequest,
) -> Result<&'a SerialDeviceCandidate>;
```

Then expose:

```rust
pub fn detect(config: &DeviceConfig) -> Result<String>;

pub fn detect_with_request(
    config: &DeviceConfig,
    request: &SerialMatchRequest,
) -> Result<String>;
```

Selection semantics:

- first filter by `VID/PID` compatible with the preset
- if `request.serial_number` is present, require exact serial match
- if exactly one remains, select it
- if zero remain, return `NoDeviceFound`
- if multiple remain, return `MultipleDevicesFound`

`runner.rs` must use:

- explicit `serial_port` if configured
- otherwise `detect_with_request(... serial_number ...)`

Critical rule:

- if `serial_number` is configured and not found, the bridge must wait and retry
- it must never attach to another matching device

### 3. Make daemon resources instance-scoped

Files:

- `open-control/bridge/src/instance_lock.rs`
- `open-control/bridge/src/main.rs`
- `open-control/bridge/src/control.rs`

Introduce normalized instance naming:

```rust
pub fn effective_instance_id(cfg: &BridgeConfig) -> &str;
```

Fallback:

- if no explicit `instance_id`, use `"default"`

Change lock naming:

- old: `oc-bridge.lock`
- new: `oc-bridge.<instance_id>.lock`

Suggested API change:

```rust
impl InstanceLock {
    pub fn acquire_daemon(instance_id: &str) -> Result<Self>;
}
```

Change file log naming:

- old: `bridge.log`
- new: `bridge.<instance_id>.log`

This matters because a multi-instance product needs logs that stay attributable to one instance.

### 4. Expose instance identity on the control plane

Files:

- `open-control/bridge/src/control.rs`
- `open-control/bridge/src/bridge/runner.rs`

Extend `ControlInfo`:

```rust
pub struct ControlInfo {
    pub pid: u32,
    pub version: String,
    pub config_path: String,
    pub instance_id: String,
    pub controller_serial: Option<String>,
    pub resolved_serial_port: Option<String>,
    pub host_udp_port: u16,
    pub log_broadcast_port: u16,
    pub control_port: u16,
    pub serial_supported: bool,
}
```

Extend `Response` with optional fields:

```rust
pub instance_id: Option<String>,
pub controller_serial: Option<String>,
pub resolved_serial_port: Option<String>,
```

`status` and `info` commands should return these fields.

This lets `ms-manager` confirm:

- which bridge instance responded
- which serial it is targeting
- which COM port it currently resolved to

### 5. Add or rewrite tests in `oc-bridge`

Files:

- `open-control/bridge/src/transport/serial.rs`
- `open-control/bridge/src/instance_lock.rs`
- `open-control/bridge/src/control.rs`
- `open-control/bridge/tests/integration_test.rs`

Required tests:

- `select_candidate_without_serial_returns_multiple_when_two_match`
- `select_candidate_with_matching_serial_returns_correct_port`
- `select_candidate_with_missing_serial_returns_no_device_found`
- `matches_request_rejects_wrong_serial`
- `instance_lock_allows_different_instance_ids`
- `instance_lock_rejects_duplicate_same_instance_id`
- `control_info_status_includes_instance_identity`

Cleanup:

- rewrite or remove `test_config_toml_roundtrip` in `tests/integration_test.rs`

Reason:

- it references stale fields like `udp_port` and `transport_mode`
- it is already inconsistent with current config semantics

## Phase 2: `ms-manager` persistence and multi-instance supervision

### 1. Add a dedicated bridge-instances state model

Files:

- `ms-manager/crates/ms-manager-core/src/lib.rs`
- new file: `ms-manager/crates/ms-manager-core/src/bridge_instances.rs`

Add schema constant:

```rust
pub const BRIDGE_INSTANCES_SCHEMA: u32 = 1;
```

Add types:

```rust
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum BridgeApp {
    Bitwig,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum BridgeMode {
    Hardware,
    NativeSim,
    WasmSim,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct BridgeInstanceBinding {
    pub instance_id: String,
    pub app: BridgeApp,
    pub mode: BridgeMode,
    pub controller_serial: String,
    pub controller_vid: u32,
    pub controller_pid: u32,
    pub host_udp_port: u16,
    pub control_port: u16,
    pub log_broadcast_port: u16,
    pub enabled: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct BridgeInstancesState {
    pub schema: u32,
    #[serde(default)]
    pub instances: Vec<BridgeInstanceBinding>,
}
```

Add helper validation methods:

- no duplicate `instance_id`
- no duplicate `controller_serial` for same `app/mode` unless explicitly allowed later
- no duplicate `host_udp_port`
- no duplicate `control_port`
- no duplicate `log_broadcast_port`

### 2. Persist the new state in `ms-manager`

Files:

- `ms-manager/src-tauri/src/layout.rs`
- `ms-manager/src-tauri/src/state.rs`

Add:

```rust
pub fn bridge_instances_file(&self) -> PathBuf {
    self.state_dir().join("bridge_instances.json")
}
```

Add state field:

```rust
bridge_instances: Mutex<BridgeInstancesState>,
```

Add load/save helpers mirroring existing patterns:

- absent file => default state
- parse error => quarantine to `.corrupt.json` and reset
- schema mismatch => reset until migrations are added

Add methods:

```rust
pub fn bridge_instances_get(&self) -> BridgeInstancesState;
pub fn bridge_instances_set(&self, next: BridgeInstancesState) -> ApiResult<BridgeInstancesState>;
pub fn bridge_instance_upsert(&self, next: BridgeInstanceBinding) -> ApiResult<BridgeInstancesState>;
pub fn bridge_instance_remove(&self, instance_id: &str) -> ApiResult<BridgeInstancesState>;
```

Do not overload `controller.json`.

Reason:

- `controller.json` currently stores flash history only
- bridge instance bindings are a separate concern and need their own schema and lifecycle

### 3. Replace singleton bridge supervision

Files:

- `ms-manager/src-tauri/src/services/bridge.rs`

Current problem:

- one global loop
- one daemon spawn
- one fixed control port

Target:

- a supervisor reconciles desired bindings to running bridge daemons

Suggested internal model:

```rust
struct BridgeInstanceDesired {
    instance_id: String,
    controller_serial: String,
    host_udp_port: u16,
    control_port: u16,
    log_broadcast_port: u16,
}
```

Supervisor algorithm on each tick:

1. load `bridge_instances`
2. keep only `enabled` instances
3. for each desired instance:
   - ping `control_port`
   - if healthy and `instance_id/controller_serial` match, do nothing
   - if unhealthy, spawn exactly that instance
4. optionally detect orphaned `oc-bridge` instances and leave them alone at first

Spawn command shape:

```text
oc-bridge --daemon \
  --instance-id <INSTANCE_ID> \
  --serial-number <SERIAL> \
  --udp-port <HOST_UDP_PORT> \
  --daemon-control-port <CONTROL_PORT> \
  --daemon-log-broadcast-port <LOG_BROADCAST_PORT>
```

Important:

- never spawn a bridge instance without `instance_id`
- never spawn a hardware bridge instance without `controller_serial`

### 4. Replace singleton bridge status APIs

Files:

- `ms-manager/src-tauri/src/models.rs`
- `ms-manager/src-tauri/src/services/bridge_status.rs`
- `ms-manager/src-tauri/src/commands/bridge.rs`
- `ms-manager/src-tauri/src/commands/status.rs`
- `ms-manager/src/lib/api/types.ts`

Add Tauri/backend models:

```rust
#[derive(Debug, Clone, Serialize)]
pub struct BridgeInstanceStatus {
    pub instance_id: String,
    pub configured_serial: String,
    pub running: bool,
    pub paused: bool,
    pub serial_open: bool,
    pub version: Option<String>,
    pub resolved_serial_port: Option<String>,
    pub connected_serial: Option<String>,
    pub message: Option<String>,
    pub host_udp_port: u16,
    pub control_port: u16,
    pub log_broadcast_port: u16,
}
```

Then expose a list-oriented wrapper:

```rust
#[derive(Debug, Clone, Serialize)]
pub struct BridgeStatus {
    pub installed: bool,
    pub instances: Vec<BridgeInstanceStatus>,
}
```

`bridge_status.rs` should query each instance’s `control_port`.

Status validation logic:

- `running = true` only if the daemon responds
- if the daemon responds with a mismatched `instance_id`, report unhealthy
- if the daemon responds with a mismatched `controller_serial`, report unhealthy

### 5. Add commands for binding lifecycle

Files:

- new file likely under `ms-manager/src-tauri/src/commands/bridge_instances.rs`
- `ms-manager/src/lib/api/client.ts`
- `ms-manager/src/lib/api/types.ts`

Add commands:

- `bridge_instances_get`
- `bridge_instance_bind`
- `bridge_instance_remove`
- `bridge_instance_enable_set`

Suggested bind command payload:

```rust
pub struct BridgeInstanceBindRequest {
    pub app: BridgeApp,
    pub mode: BridgeMode,
    pub controller_serial: String,
    pub controller_vid: u32,
    pub controller_pid: u32,
}
```

Binding behavior:

- derive `instance_id`, for example `bitwig-hardware-<serial>`
- allocate free ports from a deterministic range
- persist the binding

Suggested fixed ranges:

- host UDP: start at `9000`
- control: start at `7999`
- log broadcast: start at `9999`

Port allocation helper must skip already-used ports.

### 6. Tests for `ms-manager`

Files:

- `ms-manager/crates/ms-manager-core/src/bridge_instances.rs`
- `ms-manager/src-tauri/src/state.rs`
- `ms-manager/src-tauri/src/services/bridge.rs`
- `ms-manager/src-tauri/src/services/bridge_status.rs`

Required tests:

- serialize/deserialize `BridgeInstancesState`
- reject duplicate `instance_id`
- reject duplicate port assignments
- state loader returns defaults when file is absent
- state loader quarantines corrupted JSON
- supervisor builds one spawn command per enabled binding
- supervisor does not conflate two bindings with different serials
- status query preserves isolation when one instance is down and the other is healthy

## Phase 3: Bitwig host support for multiple hardware instances

### 1. Remove hard-coded bridge mode presets

Files:

- `midi-studio/plugin-bitwig/host/src/midistudio/MidiStudioExtension.java`
- `midi-studio/plugin-bitwig/host/src/protocol/Protocol.java`

Current issue:

- `"Hardware (9000)"`, `"Native Sim (9001)"`, `"WASM Sim (9002)"` hard-code topology assumptions into the extension

Target:

- allow selecting an explicit bridge port per extension instance

Recommended first implementation:

- keep a simple enum for transport type if needed
- replace fixed bridge-mode port mapping with a numeric `Bridge Port` preference

Example:

```java
final SettableIntegerValue bridgePortSetting = host.getPreferences()
   .getNumberSetting("Bridge Port", "Connection", 8000, 9999, 1, "", 9000);
bridgePortSetting.markInterested();
final int bridgePort = bridgePortSetting.getRaw();
```

If Bitwig API typing forces doubles/ints in a different form, adapt to its actual number-setting API, but the intent stays the same:

- the extension instance must own an explicit port choice

### 2. Clarify MIDI-port ambiguity

Files:

- `midi-studio/plugin-bitwig/host/src/midistudio/MidiStudioExtensionDefinition.java`
- `midi-studio/plugin-bitwig/src/name.c`

Important limitation:

- if two hardware devices expose the same USB MIDI name `MIDI Studio [hw]`, Bitwig-side MIDI auto-detection may still be ambiguous even after bridge isolation is fixed

Decision:

- this should be tracked as a separate problem
- do not block bridge-instance work on it
- but do document it clearly as a residual risk

Possible future directions:

- manual port assignment per Bitwig instance
- distinct MIDI product names per controller profile
- if the host API supports it, richer port discrimination than just display name

### 3. Tests for Bitwig host

Files:

- `midi-studio/plugin-bitwig/host/src/...`

Required tests or verifications:

- the selected bridge port is read correctly from preferences
- two extension instances can use two different configured bridge ports
- popup/debug info should report the chosen port

If there is no existing Java test harness in this codebase, add at least a minimal extraction of bridge-port selection logic into a testable helper instead of leaving it embedded in `init()`.

## Cleanup Plan

Remove or refactor obsolete singleton assumptions as part of implementation, not afterward.

### `oc-bridge`

- remove the idea that there is one daemon lock for all bridges
- stop writing all daemons to a single `bridge.log`

### `ms-manager`

- remove singleton-only status assumptions
- replace bridge APIs returning one global status with instance-aware APIs
- stop assuming one control port for the product

### Bitwig

- remove fixed-mode strings that imply exactly one hardware bridge on `9000`

## Acceptance Criteria

This work is done when all of the following are true.

### Functional

- two identical controllers with distinct USB serials can be connected simultaneously
- two `oc-bridge` daemons can run simultaneously
- each daemon targets its configured `controller_serial` only
- unplugging one controller does not make its bridge attach to the other
- reconnecting a controller on a different COM port is transparent
- `ms-manager` restarts and restores persisted bindings correctly
- two Bitwig instances can target two different bridge ports and operate independently

### Observability

- each bridge instance exposes `instance_id`, configured `controller_serial`, and resolved serial port via control-plane status
- each bridge instance has its own log file
- each bridge instance has its own lock file

### Testability

- unit tests exist for serial selection by USB serial
- unit tests exist for per-instance locking
- unit tests exist for persisted bridge-instance state
- supervisor tests exist for multi-instance spawn behavior
- stale tests referencing removed config semantics are deleted or rewritten

## Recommended Execution Order

1. `oc-bridge`: CLI/config/model changes
2. `oc-bridge`: serial selection by USB serial
3. `oc-bridge`: instance lock, per-instance logs, control-plane identity
4. `oc-bridge`: tests
5. `ms-manager-core`: bridge instance state model
6. `ms-manager`: state persistence and port allocation
7. `ms-manager`: supervisor refactor
8. `ms-manager`: status and commands refactor
9. `ms-manager`: tests
10. Bitwig host: configurable bridge port
11. Bitwig host: tests or extracted helper coverage
12. cleanup of dead singleton code paths

## Non-Goals For This Refactor

- solving every possible Bitwig MIDI auto-detection ambiguity immediately
- changing firmware USB serial generation
- changing firmware USB VID/PID strategy
- introducing one single `oc-bridge` process that multiplexes many controllers

## Short Risk Notes

- The highest compatibility risk is the `ms-manager` API shape change from singleton bridge status to instance list.
- The highest behavioral risk is stale code paths that still assume `DEFAULT_CONTROL_PORT`.
- The highest product risk after this refactor is likely the separate Bitwig MIDI-name ambiguity for identical hardware devices.

Those risks are manageable if the singleton code is removed decisively and the tests above are added before wiring UI changes.
