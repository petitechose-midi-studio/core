# MIDI Studio device support

`ms-device-support` is the narrow, versioned Teensy 4.1 hardware boundary
shared by MIDI Studio firmware products. It contains board constants, input
IDs and policy, display/buffer configuration, `lv_conf.h`, and the single LVGL
PSRAM provider.

Consumers include only the contract they use, for example:

```cpp
#include <ms/device_support/v1/Display.hpp>
#include <ms/device_support/v1/Buffers.hpp>
```

There is intentionally no umbrella header. Application state, UI composition,
MIDI routing and `InputAPI` are not part of this package.

The public path and namespace carry API version `v1`. The package manifest
version is independent and follows normal semantic-version release rules.
