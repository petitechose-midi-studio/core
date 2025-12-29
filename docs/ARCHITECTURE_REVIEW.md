# Architecture Review – Open Control Stack (Amended)

> **Date**: December 2024
> **Scope**: Complete system-level review of the Open Control stack
> **Status**: Living document — update as invariants are formalized

This document extends and amends the original architecture review with concrete findings from codebase analysis.

---

## 1. Executive Summary

### Current State Assessment

| Aspect | Status | Notes |
|--------|--------|-------|
| **Overall Architecture** | Excellent | Clean separation, instrument-grade design |
| **Single Source of Truth** | Mostly Respected | Minor violations identified |
| **Input Authority** | Implicit | Works but not guaranteed |
| **Overlay Management** | Partially Formalized | `OverlayManager` exists, needs integration |
| **UI Commit Phase** | Not Implemented | Uses deferred `NotificationQueue` instead |
| **Latch System** | Implemented | Centralized in `InputBinding` |
| **Echo Suppression** | Implemented | Time-window approach (80ms) |

### Priority Actions

1. **HIGH**: Formalize input authority with `AuthorityResolver`
2. **HIGH**: Integrate `OverlayManager` with `InputBinding` scopes
3. **MEDIUM**: Document and enforce invariants
4. **LOW**: Consider UI commit phase (measure first)

---

## 2. What Already Works Well

### 2.1 Signal System (`oc::state::Signal<T>`)

The reactive signal system is solid:
- Change detection via `operator==` or `memcmp`
- Subscription-based with RAII auto-unsubscribe
- `NotificationQueue` coalesces updates per tick
- Deferred notifications prevent update storms

```cpp
// Current pattern - already correct
signal.set(newValue);  // Enqueues notification
// Later: NotificationQueue::flush() executes callbacks once
```

**Verdict**: No changes needed. This achieves the review's "UI Commit Phase" goals via a different mechanism.

### 2.2 Handler Separation (BitwigContext)

The handler architecture is exemplary:

```
BitwigContext
├── HostHandlers (protocol → state)
│   └── Never touch input bindings
├── InputHandlers (input → state + protocol)
│   └── Never observe Bitwig API directly
└── Views (state → UI)
    └── Pure projections via Signal subscriptions
```

**Verdict**: Excellent separation. Document as invariant.

### 2.3 Latch System (InputBinding)

The latch/hold/release pattern is correctly implemented:

```cpp
// InputBinding.hpp:149-150
std::array<ScopeID, MAX_BUTTONS> latch_owner_{};       // Scope that owns latch
std::array<ScopeID, MAX_BUTTONS> button_press_owner_{}; // Scope that handled press
```

Methods `clearLatch()`, `isLatched()`, and `clearScope()` provide centralized management.

**Verdict**: Solid. Document the contract explicitly.

### 2.4 Scope-Based Input Dispatch

The scope system correctly prioritizes scoped over global bindings:

```cpp
// Dispatch order (InputBinding.cpp)
1. triggerScopedButtonBindings()  // Stops on first match
2. triggerGlobalButtonBindings()  // All matching execute
```

**Verdict**: Works, but authority is implicit (relies on scope registration order).

---

## 3. Identified Gaps & Violations

### 3.1 GAP: No Explicit Authority Resolver

**Current State**: Input authority is determined by:
- Scope registration order
- `isActive` predicates on bindings
- `OverlayManager.current()` (not integrated with InputBinding)

**Risk**: Multiple scopes can be "active" simultaneously. Nothing guarantees the correct layer receives input.

**Example Scenario**:
```
1. DeviceSelector opens (scoped bindings registered)
2. TrackSelector opens on top (stacked)
3. Both have scoped bindings for NAV encoder
4. Which one receives input? → Depends on registration order
```

**Recommendation**: Create `AuthorityResolver` that returns the single authoritative scope.

### 3.2 GAP: OverlayManager Not Integrated with InputBinding

**Current State**:
- `OverlayManager` tracks `current_` and `previous_` overlays
- `InputBinding` tracks `latch_owner_` per button
- These systems don't communicate

**Problem**: When `OverlayManager.hide()` is called, latches aren't automatically cleared.

**File**: `midi-studio/plugin-bitwig/src/state/OverlayManager.hpp`

```cpp
void hide() {
    setVisible(current_, false);
    current_ = previous_;
    previous_ = OverlayType::NONE;
    // BUG: No clearLatch() or clearScope() called
}
```

### 3.3 VIOLATION: Some Handlers Touch UI Directly

**Finding**: Most handlers correctly update state only, but some views subscribe with immediate LVGL updates.

**Example Pattern (Correct)**:
```cpp
// Handler updates state
state_.device.name.set(newName);

// View subscribes and updates LVGL
nameSub_ = state_.device.name.subscribe([this](const std::string& name) {
    label_.setText(name);  // LVGL call in callback
});
```

This is acceptable because `NotificationQueue` batches these calls. However, there's no enforcement.

**Risk**: Future code might call `lv_*` directly in handlers.

### 3.4 GAP: No Formalized Invariants Document

The architecture is correct by convention, not by contract. Rules like:
- "Handlers never call `lv_*`"
- "Views never call `protocol_.send()`"
- "Overlays must clear their scope on close"

...exist implicitly but aren't documented.

### 3.5 MINOR: Echo Suppression Uses Local Time

**File**: `midi-studio/plugin-bitwig/host/src/handler/controller/DeviceController.java`

```java
private long[] lastControllerChangeTime = new long[BitwigConfig.MAX_PARAMETERS];

public boolean consumeEcho(int paramIndex) {
    long timeSinceChange = System.currentTimeMillis() - lastControllerChangeTime[paramIndex];
    return timeSinceChange < BitwigConfig.ECHO_TIMEOUT_MS;  // 80ms
}
```

**Risk**: If message latency exceeds 80ms, echo suppression fails. This is an acceptable trade-off documented in DrivenByMoss, but should be noted.

---

## 4. Recommended Architecture Amendments

### 4.1 Create AuthorityResolver

```cpp
// open-control/framework/src/oc/core/input/AuthorityResolver.hpp

namespace oc::core::input {

/**
 * @brief Determines which scope has input authority
 *
 * Authority hierarchy:
 * 1. Top overlay (if any)
 * 2. Active view
 * 3. Global (scope = 0)
 */
class AuthorityResolver {
public:
    using OverlayStackFn = std::function<ScopeID()>;  // Returns top overlay scope
    using ActiveViewFn = std::function<ScopeID()>;     // Returns active view scope

    void setOverlayStack(OverlayStackFn fn) { overlayStackFn_ = fn; }
    void setActiveView(ActiveViewFn fn) { activeViewFn_ = fn; }

    /**
     * @brief Get the scope that currently has input authority
     * @return ScopeID of authoritative scope (0 = global)
     */
    ScopeID getAuthority() const {
        if (overlayStackFn_) {
            ScopeID overlay = overlayStackFn_();
            if (overlay != 0) return overlay;
        }
        if (activeViewFn_) {
            return activeViewFn_();
        }
        return 0;  // Global
    }

    /**
     * @brief Check if a scope currently has authority
     */
    bool hasAuthority(ScopeID scope) const {
        return scope == 0 || scope == getAuthority();
    }

private:
    OverlayStackFn overlayStackFn_;
    ActiveViewFn activeViewFn_;
};

}  // namespace oc::core::input
```

### 4.2 Integrate OverlayManager with Cleanup

```cpp
// Amended OverlayManager with cleanup hooks

class OverlayManager {
public:
    using CleanupCallback = std::function<void(OverlayType)>;

    void setCleanupCallback(CleanupCallback cb) { cleanup_ = cb; }

    void hide() {
        if (current_ == OverlayType::NONE) return;

        // NEW: Notify cleanup before hiding
        if (cleanup_) cleanup_(current_);

        setVisible(current_, false);
        current_ = previous_;
        previous_ = OverlayType::NONE;
    }

private:
    CleanupCallback cleanup_;
};

// Usage in BitwigContext:
overlayManager_.setCleanupCallback([this](OverlayType type) {
    ScopeID scope = getScopeForOverlay(type);
    inputBinding_.clearScope(scope);
    inputBinding_.clearLatchesForScope(scope);  // New method needed
});
```

### 4.3 Add Scope-Based Latch Clearing

```cpp
// Addition to InputBinding

/**
 * @brief Clear all latches owned by a specific scope
 */
void clearLatchesForScope(ScopeID scope) {
    for (size_t i = 0; i < MAX_BUTTONS; ++i) {
        if (latch_owner_[i] == scope) {
            latch_owner_[i] = 0;
        }
    }
}
```

### 4.4 Amend Input Dispatch with Authority Check

```cpp
// In InputBinding::triggerScopedButtonBindings

bool InputBinding::triggerScopedButtonBindings(hal::ButtonID buttonId, ButtonBindingType type) {
    ScopeID authority = authority_resolver_.getAuthority();  // NEW

    for (auto& binding : button_bindings_) {
        if (binding.scopeId == 0) continue;  // Skip global
        if (binding.buttonId != buttonId) continue;
        if (binding.type != type) continue;
        if (!binding.enabled) continue;

        // NEW: Only trigger if binding's scope has authority
        if (binding.scopeId != authority) continue;

        if (binding.isActive && !binding.isActive()) continue;

        binding.action();
        return true;  // Stop propagation
    }
    return false;
}
```

---

## 5. Formalized Invariants

Create `/docs/INVARIANTS.md`:

```markdown
# System Invariants

These rules are **non-negotiable**. Any feature that violates them must be redesigned.

## 1. Single Source of Truth

- **State** is the truth (BitwigState, MacroState, etc.)
- **UI** is a pure projection of State (via Signal subscriptions)
- **Handlers** never write to LVGL directly
- Any visible UI change must be explainable by State alone

## 2. Input Authority

At any time, **exactly one scope** has input authority:

1. Top overlay (highest priority)
2. Active view
3. Global (scope = 0)

Lower-priority scopes MUST NOT receive input while a higher-priority scope is active.

## 3. Handler Boundaries

| Layer | Can Call | Cannot Call |
|-------|----------|-------------|
| InputHandler | state_.*.set(), protocol_.send() | lv_*(), views |
| HostHandler | state_.*.set() | lv_*(), protocol_.send(), input APIs |
| View | lv_*() | protocol_.send(), inputBinding_ |

## 4. Overlay Lifecycle

When an overlay closes, it MUST:
1. Call `overlayManager_.hide()` or equivalent
2. Clear its scope: `inputBinding_.clearScope(scopeId)`
3. Release any latches: `inputBinding_.clearLatchesForScope(scopeId)`

## 5. Latch Contract

- A latch is owned by exactly one scope
- Latches are released on:
  - Button release while latched
  - Scope cleanup (overlay close)
  - Explicit `clearLatch(button)`

## 6. Echo Suppression

- Controller → Host messages are echoed back
- Echo detection uses 80ms time window
- `fromHost` flag distinguishes echoes from real updates
- Echoes are suppressed to prevent feedback loops

## 7. Host Authority

- Device emits **intentions**, never final decisions
- Host (Bitwig) is authoritative for parameter values
- Host updates are idempotent
- Old/stale updates are safely ignored
```

---

## 6. Extension Readiness Checklist (Amended)

Before adding major new views (Mixer, Clips, Modulation):

### Input & Authority
- [ ] `AuthorityResolver` implemented and integrated
- [ ] Overlay stack explicitly managed via `OverlayManager`
- [ ] Latches always released via centralized cleanup
- [ ] All bindings use `scope()` correctly

### State & UI
- [ ] No handler touches LVGL directly (enforced by code review)
- [ ] Views are pure projections (subscribe to signals only)
- [ ] Transient UI state clearly separated from persistent state

### Performance
- [ ] `NotificationQueue` coalesces updates (already done)
- [ ] No full-screen invalidations except on view change
- [ ] Virtual lists used for collections > 10 items

### Testing
- [ ] Unit tests for `AuthorityResolver`
- [ ] Integration tests for overlay stacking scenarios
- [ ] Latch lifecycle tests

---

## 7. Recommended File Structure

```
midi-studio/core/docs/
├── ARCHITECTURE.md          # Current (good)
├── ARCHITECTURE_REVIEW.md   # This document
├── INVARIANTS.md            # Extract from section 5
├── CODE_STYLE.md            # Current (good)
└── EXTENSION_CHECKLIST.md   # Extract from section 6

open-control/framework/src/oc/core/input/
├── InputBinding.hpp         # Current
├── InputBinding.cpp         # Current
├── AuthorityResolver.hpp    # NEW
└── InputConfig.hpp          # Current
```

---

## 8. Implementation Priority

### Phase 1: Formalization (No Code Changes)
1. Create `INVARIANTS.md` from section 5
2. Document current overlay lifecycle expectations
3. Add comments to critical code paths

### Phase 2: Authority Integration
1. Implement `AuthorityResolver`
2. Integrate with `InputBinding`
3. Connect `OverlayManager` cleanup hooks
4. Add `clearLatchesForScope()` method

### Phase 3: Testing & Validation
1. Unit tests for authority resolution
2. Integration tests for overlay stacking
3. Regression tests for existing functionality

### Phase 4: Optional Optimizations
1. Measure FULL vs PARTIAL render mode
2. Consider shared animation manager
3. Profile and optimize if needed

---

## 9. Comparison with Original Review

| Original Recommendation | Current Status | Action |
|------------------------|----------------|--------|
| Single Source of Truth | Respected | Document as invariant |
| Input Authority explicit | Implicit | Implement AuthorityResolver |
| Overlay stack managed | Partial (OverlayManager exists) | Integrate with InputBinding |
| UI Commit Phase | Not needed (NotificationQueue) | No action |
| Centralized overlay close | Partial | Add cleanup hooks |
| Strong focus signal | UI decision | Defer to design phase |
| Value toast | UI decision | Defer to design phase |
| Visible hold progress | Some overlays have it | Standardize |

---

## 10. Conclusion

The Open Control architecture is **production-ready** and follows instrument-grade design principles. The main work remaining is **formalization** and **explicit authority management**.

Key insight: The system works correctly today because of careful implementation. The risk is that future changes could violate implicit rules. By making these rules explicit and adding the `AuthorityResolver`, the system becomes:

- **Easier to reason about**: Clear authority hierarchy
- **Safer to extend**: Invariants prevent accidental breakage
- **Self-documenting**: Rules are in code, not just convention

The `NotificationQueue` already provides batched UI updates, making a separate "UI Commit Phase" unnecessary. Focus efforts on input authority and lifecycle management instead.
