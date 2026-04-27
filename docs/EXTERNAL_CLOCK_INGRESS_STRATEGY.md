# External Clock Ingress Strategy

## Guarantees

- Internal master timing uses the realtime timer lane and deadline-based output queue.
- USB slave timing uses timestamped input read from the USB MIDI transport.
- USB slave timing remains bounded by USB receive buffering and `pollInput()` cadence.

## Current USB Path

```text
UsbMidi::pollInput()
  -> IMidi realtime callback
  -> OpenControl event bus
  -> MidiClockSyncService::onClock()
  -> SequencerRuntimeService::update()
```

The timestamp is captured in `UsbMidi::pollInput()` immediately after `usbMIDI.read()` returns a message.

## Measurement

`MidiClockSyncService` records:

- received clock count
- maximum timestamp interval
- maximum host-side gap
- maximum jitter against recent mean interval

Runtime perf logging consumes these counters once per profiling window.

## Hardware Direction

For stronger slave-clock determinism, prefer a DIN/UART MIDI ingress path with interrupt or DMA timestamp capture, then feed the same `MidiClockSyncService` clock API. Keep USB slave support, but do not describe it as independent from loop cadence without hardware measurement.
