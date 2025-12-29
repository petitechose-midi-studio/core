# Extension Readiness Checklist

> Use this checklist before adding major new views (Mixer, Clips, Modulation, etc.)
> Each item must be verified before the feature is considered complete.

---

## Pre-Development

### Architecture Understanding
- [ ] Read `INVARIANTS.md` completely
- [ ] Understand the Signal → NotificationQueue → UI flow
- [ ] Understand scope-based input authority
- [ ] Review existing similar view for patterns (e.g., RemoteControlsView)

### Planning
- [ ] Identify which state signals are needed
- [ ] Identify which input bindings are required
- [ ] Determine if overlays are needed
- [ ] Sketch the UI layout on paper/Figma

---

## Input & Authority

### Scope Registration
- [ ] View has a unique ScopeID (typically `reinterpret_cast<ScopeID>(this)`)
- [ ] All input bindings use `.scope(scopeElement)` or `.scope(scopeId)`
- [ ] No "floating" bindings without scope (except truly global ones)

### Authority Resolution
- [ ] If adding overlays, register with `OverlayController`
- [ ] Verify overlay scopes are returned by `AuthorityResolver`
- [ ] Test that global bindings are blocked when overlay is open

### Latch Handling
- [ ] If using latch buttons, register with `OverlayController::registerOverlay()`
- [ ] Verify latches are released on overlay close
- [ ] Test the hold → release → toggle behavior

### Input Cleanup
- [ ] `clearScope()` called when view deactivates
- [ ] Overlay uses `OverlayController::hide()` (not manual hide)
- [ ] No orphan bindings after view closes

---

## State & UI

### Signal Usage
- [ ] All state lives in `*State` structs (BitwigState, etc.)
- [ ] UI subscribes via `Signal::subscribe()` or `Subscription`
- [ ] Subscriptions stored as class members (auto-unsubscribe)
- [ ] No `notifyImmediate()` unless absolutely necessary

### Handler Boundaries
- [ ] Input handlers only call `state_.*.set()` and `protocol_.send()`
- [ ] Input handlers never call `lv_*()` directly
- [ ] Views never call `protocol_.send()` directly
- [ ] Views never access `inputBinding_` directly

### State Updates
- [ ] `NotificationQueue` handles batching (no manual batching)
- [ ] Callbacks fire after `app.update()` flushes queue
- [ ] Multiple rapid updates don't cause flicker

---

## LVGL / UI

### Widget Hierarchy
- [ ] Container created in `onActivate()` or lazy `create()`
- [ ] Container hidden in `onDeactivate()`
- [ ] No leaking LVGL objects (check `lv_obj_delete` in destructor)

### Invalidation
- [ ] No `lv_obj_invalidate(lv_screen_active())` calls
- [ ] Widgets invalidate only themselves
- [ ] Verify 60 FPS is maintained (no redraw storms)

### Memory
- [ ] Use `VirtualList` for lists > 10 items
- [ ] Large fonts loaded progressively (not all at once)
- [ ] Images use DMA-compatible buffers

---

## Overlays

### Registration
- [ ] Registered with `OverlayController::registerOverlay()`
- [ ] Visibility signal, scopeId, and latchButton all provided
- [ ] Overlay starts hidden (`LV_OBJ_FLAG_HIDDEN`)

### Lifecycle
- [ ] Uses `OverlayController::show()` to open
- [ ] Uses `OverlayController::hide()` to close (automatic cleanup)
- [ ] If self-closing, calls `OverlayController::cleanupFor()` first

### Stacking
- [ ] Tested with another overlay stacked on top
- [ ] Verify correct overlay receives input
- [ ] Verify both overlays visible (z-order correct)

---

## Performance

### Frame Rate
- [ ] Measured actual FPS during operation
- [ ] No visible lag on encoder turns
- [ ] UI responsive during rapid input

### Memory
- [ ] Stack usage within limits (`arm-none-eabi-size`)
- [ ] Heap usage stable (no leaks)
- [ ] No excessive `std::string` allocations in update loop

### Protocol
- [ ] Message frequency reasonable (< 100/sec typical)
- [ ] Batch messages used where appropriate
- [ ] Echo suppression working (no feedback loops)

---

## Testing

### Unit Tests
- [ ] New handler logic has unit tests
- [ ] Scope/authority changes have tests
- [ ] Edge cases covered (rapid input, empty states, etc.)

### Integration Tests
- [ ] Test with real hardware (not just simulator)
- [ ] Test overlay open/close cycles
- [ ] Test latch behavior
- [ ] Test with host connected and disconnected

### Regression
- [ ] Existing features still work
- [ ] No new warnings in build
- [ ] Tests pass on CI

---

## Documentation

### Code Comments
- [ ] Complex logic has explanatory comments
- [ ] `// INVARIANT:` comments on critical sections
- [ ] Public API has doxygen-style comments

### User Documentation
- [ ] Feature described in relevant docs
- [ ] User-facing controls documented
- [ ] Known limitations noted

---

## Review Criteria

A feature is ready for merge when:

1. **All boxes checked** in relevant sections
2. **Code reviewed** by at least one other developer
3. **Tested on hardware** in realistic conditions
4. **No regressions** in existing functionality
5. **Performance verified** (60 FPS, responsive input)

---

## Quick Reference: Common Violations

| Violation | Where to Check | Fix |
|-----------|----------------|-----|
| Handler calls `lv_*()` | Handler cpp files | Move to View subscription |
| View calls `protocol_.send()` | View cpp files | Create InputHandler |
| Missing `clearScope()` | View `onDeactivate()` | Add call |
| Orphan latches | `OverlayController` | Use `registerOverlay()` |
| Full-screen invalidate | Search for `lv_obj_invalidate` | Invalidate specific widget |
| Scope-less binding | Search `.then()` without `.scope()` | Add `.scope()` |
