# System Invariants

> **Status**: Design Contract
> **Enforcement**: Code Review + Static Analysis (future)

These rules are **non-negotiable**. Any feature that violates them must be redesigned.

---

## 1. Single Source of Truth

### Rule
- **State** is the truth (`BitwigState`, `MacroState`, etc.)
- **UI** is a pure projection of State
- **Handlers** never write to LVGL directly
- Any visible UI change must be explainable by State alone

### Consequences
- Handlers translate input into **state changes** or **protocol messages**
- Views only observe State and render
- No `lv_*` calls outside of View/Widget code

### Verification
```cpp
// CORRECT: Handler updates state
void HandlerInputTransport::togglePlay() {
    bool newState = !state_.transport.playing.get();
    state_.transport.playing.set(newState);        // State change
    protocol_.send(TransportPlayMessage{newState}); // Protocol message
}

// WRONG: Handler touches UI
void HandlerInputTransport::togglePlay() {
    lv_label_set_text(playBtn_, "STOP");  // VIOLATION
}
```

---

## 2. Input Authority

### Rule
At any time, **exactly one scope** has input authority:

| Priority | Layer | Example |
|----------|-------|---------|
| 1 (highest) | Top overlay | DeviceSelector, TrackSelector |
| 2 | Active view | RemoteControlsView, MixView |
| 3 (lowest) | Global | Transport controls |

### Consequences
- Lower-priority scopes MUST NOT receive input while a higher-priority scope is active
- Bindings must include `scope()` to participate in authority resolution
- Global bindings (scope = 0) only trigger when no scoped binding matches

### Verification
```cpp
// CORRECT: Scoped binding with authority
buttons_.button(ButtonID::NAV)
    .press()
    .scope(overlayElement_)  // Tied to overlay visibility
    .then([this]() { selectItem(); });

// RISKY: Global binding that could conflict
buttons_.button(ButtonID::NAV)
    .press()
    .then([this]() { navigate(); });  // No scope = always active
```

---

## 3. Handler Boundaries

### Rule

| Layer | Can Call | Cannot Call |
|-------|----------|-------------|
| **InputHandler** | `state_.*.set()`, `protocol_.send()` | `lv_*()`, view methods |
| **HostHandler** | `state_.*.set()` | `lv_*()`, `protocol_.send()`, input APIs |
| **View** | `lv_*()`, subscribe to signals | `protocol_.send()`, `inputBinding_` methods |

### Rationale
- Prevents circular dependencies
- Makes data flow unidirectional
- Enables testing in isolation

### File Organization
```
handler/input/   → InputHandler implementations
handler/host/    → HostHandler implementations
ui/views/        → View implementations
ui/widgets/      → Widget implementations (LVGL only)
```

---

## 4. Overlay Lifecycle

### Rule
When an overlay closes, it MUST perform these steps **in order**:

1. **Update state**: `state_.overlay.visible.set(false)` or equivalent
2. **Clear scope**: `inputBinding_.clearScope(scopeId)`
3. **Release latches**: `inputBinding_.clearLatch(button)` for each latched button

### Rationale
- Prevents ghost input bindings
- Releases modal capture
- Restores input to underlying view

### Pattern
```cpp
void DeviceSelector::close() {
    // 1. Update state (triggers UI hide via subscription)
    state_.selector.visible.set(false);

    // 2. Clear all bindings for this overlay
    inputBinding_.clearScope(getScopeId());

    // 3. Release latch if this overlay was latched
    inputBinding_.clearLatch(ButtonID::LEFT_CENTER);
}
```

### Anti-Pattern
```cpp
void DeviceSelector::close() {
    lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);  // WRONG: UI before state
    // Missing: clearScope, clearLatch
}
```

---

## 5. Latch Contract

### Rule
- A latch is owned by **exactly one scope** at any time
- `latch_owner_[button]` tracks ownership (0 = not latched)

### Latch Release Triggers
1. **Button release while latched**: Normal release flow
2. **Scope cleanup**: `clearScope(scopeId)` or `clearLatchesForScope(scopeId)`
3. **Explicit clear**: `clearLatch(button)`

### State Machine
```
        ┌─────────────────────────────────────────┐
        │                                         │
        ▼                                         │
  ┌──────────┐   press (< threshold)   ┌──────────┴──┐
  │ UNLATCHED │ ───────────────────────▶ │   LATCHED   │
  └──────────┘                          └─────────────┘
        ▲                                     │
        │   release / clearLatch / clearScope │
        └─────────────────────────────────────┘
```

---

## 6. Echo Suppression

### Rule
- Controller → Host messages are echoed back by the host
- Echo detection uses **80ms time window** (configurable via `ECHO_TIMEOUT_MS`)
- `fromHost` flag distinguishes echoes from real updates

### Flow
```
Controller sends: DeviceRemoteControlValueChangeMessage(value=0.5, fromHost=false)
    │
    ▼
Host receives, applies to Bitwig API
    │
    ▼
Bitwig observer fires, Host sends: DeviceRemoteControlValueChangeMessage(value=0.5, fromHost=true)
    │
    ▼
Controller receives, checks: (now - lastChangeTime) < 80ms?
    │
    ├── Yes (echo) → Suppress, don't update UI
    └── No (real)  → Update state and UI
```

### Assumptions
- Network latency is typically < 80ms
- If latency exceeds 80ms, echo suppression may fail (acceptable trade-off)

---

## 7. Host Authority

### Rule
- Device emits **intentions**, never final decisions
- Host (Bitwig) is **authoritative** for:
  - Parameter values
  - Device/track selection
  - Transport state
- Host updates are idempotent

### Consequences
- Controller optimistically updates UI (responsiveness)
- Host confirmation replaces optimistic value
- Stale updates are safely ignored (value already matches)

### Pattern
```cpp
// Optimistic update for responsiveness
void HandlerInputRemoteControl::onEncoderTurn(int index, float delta) {
    float newValue = std::clamp(current + delta, 0.0f, 1.0f);

    // Optimistic: update UI immediately
    state_.parameters.slots[index].value.set(newValue);

    // Send to host (host will confirm or correct)
    protocol_.send(DeviceRemoteControlValueChangeMessage{index, newValue});
}

// Host confirmation (may differ from optimistic value)
void HandlerHostRemoteControl::onValueChange(const Message& msg) {
    if (!msg.fromHost) return;  // Skip echoes

    // Host's value is authoritative
    state_.parameters.slots[msg.index].value.set(msg.value);
}
```

---

## 8. UI Update Discipline

### Rule
- Signal subscriptions do not call `lv_*` on every change
- `NotificationQueue` coalesces updates per tick
- LVGL updates occur at most once per `LVGL_HZ` (60 Hz)

### Flow
```
state_.name.set("A")  ─┐
state_.name.set("B")  ─┼─▶ NotificationQueue (deduplicated)
state_.name.set("C")  ─┘           │
                                   ▼
                         app.update() calls flush()
                                   │
                                   ▼
                         Callback fires ONCE with value "C"
                                   │
                                   ▼
                         label_.setText("C")  // Single LVGL call
```

### Anti-Pattern
```cpp
// WRONG: notifyImmediate() bypasses coalescing
state_.value.notifyImmediate();  // Use only for critical real-time feedback
```

---

## 9. No Global Redraws

### Rule
- `lv_obj_invalidate(lv_screen_active())` is forbidden except on view change
- Only invalidate the smallest possible region
- Widgets invalidate themselves, not parents

### Rationale
- Prevents redraw storms
- Preserves 60 FPS on Teensy 4.1
- Reduces SPI bandwidth to display

### Verification
```cpp
// CORRECT: Widget invalidates itself
void KnobWidget::setValue(float value) {
    value_ = value;
    lv_obj_invalidate(arc_);  // Only the arc
}

// WRONG: Invalidate parent or screen
void KnobWidget::setValue(float value) {
    value_ = value;
    lv_obj_invalidate(lv_screen_active());  // VIOLATION
}
```

---

## Enforcement

### Code Review Checklist
- [ ] Handlers don't call `lv_*`
- [ ] Views don't call `protocol_.send()`
- [ ] Overlays call `clearScope()` on close
- [ ] Latches are released on overlay close
- [ ] Scoped bindings use `scope()` correctly
- [ ] No full-screen invalidations

### Future: Static Analysis
Consider adding custom clang-tidy checks:
- `lv_*` calls only in `ui/` directory
- `protocol_.send()` only in `handler/input/` directory
- `clearScope()` called in every overlay destructor

---

## Changelog

| Date | Change |
|------|--------|
| 2024-12 | Initial version from architecture review |
