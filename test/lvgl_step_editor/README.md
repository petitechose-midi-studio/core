# Step Editor: real LVGL opening regression

This standalone target complements the Core tests that use lightweight LVGL
stubs. It renders RGB565 at 320×240 using the application's LVGL checkout and
a built-in font; no SDL window or product assets are needed.

```sh
cmake -S test/lvgl_step_editor -B build/step-editor \
  -DLVGL_DIR=/absolute/path/to/lvgl -DCMAKE_BUILD_TYPE=Release
cmake --build build/step-editor
ctest --test-dir build/step-editor --output-on-failure
```

The workspace layout supplies sibling `ui`, `device-support` and `open-control`
repositories. `OC_ROOT` can override the latter location.

Checks: registry's early reveal, hidden opening layout, unchanged reopen,
resized parent, chord layout, first frame versus full redraw. `--reference`
only skips the hidden-layout assertion to print the old renderer's framebuffer
hashes for comparison; it is not the acceptance-test mode. Real font/icon and
navigation coverage remains in `ms ux run core` workflows.

The optional `project_modulator_workspace` regression links the built Core,
Note and Framework native libraries. Set `-DMS_CORE_NATIVE_BUILD=/absolute/path/to/core-native-build`
to enable it. It runs the real curve widget and its LVGL marker timer across
Control owner exchanges, poisons the retired source before freeing it, then
checks marker identity after resize and hide/reopen. Only the widget call is
observed; the production workspace implementation and marker callback run unchanged.
