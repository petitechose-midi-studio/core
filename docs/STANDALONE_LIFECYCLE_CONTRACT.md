# Standalone Lifecycle Contract

> Date: 2026-04-16
> Scope: `midi-studio/core`
> Purpose: make the live standalone runtime owner and macro activation owner obvious to the next contributor

---

## 1. Runtime ownership

The authoritative standalone sequencer runtime lives in `main.cpp`.

Rules:

- the runtime is allocated once in PSRAM as `standaloneSequencerRuntime`
- the app pre-context hook is the only live standalone runtime update entry point
- `StandaloneContext::update()` does not tick the runtime
- leaving standalone stops the runtime once, from the same hook path

Code seam:

- `main.cpp`
- `src/context/standalone/StandaloneSequencerRuntimeGate.hpp`

Regression coverage:

- `test/test_StandaloneSequencerRuntimeGate`

The runtime gate contract is:

- standalone active => `UPDATE`
- standalone inactive after being active => `STOP`
- standalone inactive while already inactive => `NONE`

---

## 2. Macro activation ownership

Macro-view activation side effects do not belong in `ui/view/*`.

Rules:

- `MacroView` stays presentation-only on activation
- macro runtime/state resync happens before the view is activated
- `statusBar.pageName` is refreshed from the active macro page in the same lifecycle seam

Code seam:

- `src/context/StandaloneContext.cpp`
- `src/context/standalone/MacroViewActivationContract.*`
- `src/context/standalone/ActiveViewLifecyclePlan.hpp`

Regression coverage:

- `test/test_MacroViewActivationContract`
- `test/test_ActiveViewLifecycle`

The activation order for macro view is:

1. deactivate current views
2. prepare macro activation contract
3. activate macro view
4. sync macro encoders

---

## 3. Review checklist

When touching standalone lifecycle code, confirm all of the following:

- runtime ownership is still singular and visible from `main.cpp`
- no context or feature module reintroduces a second runtime tick path
- macro activation side effects still live outside `ui/view/*`
- tests covering the runtime gate and macro activation contract still pass

If one of these stops being true, the standalone lifecycle contract is broken.
