"""Run the C++ assertions and parse every emitted response with stdlib JSON."""
import json
import subprocess
import sys

result = subprocess.run(sys.argv[1:], capture_output=True, text=True)
print(result.stdout, end="")
print(result.stderr, end="", file=sys.stderr)
if result.returncode:
    raise SystemExit(result.returncode)
payloads = [line.removeprefix("WIRE_JSON ") for line in result.stdout.splitlines()
            if line.startswith("WIRE_JSON ")]
assert payloads, "No response payloads exercised"
decoded = [json.loads(payload) for payload in payloads]
assert all(isinstance(payload, dict) for payload in decoded)
large_metric = next(payload for payload in decoded if payload.get("total_us") == 4_500_000_000)
assert type(large_metric["total_us"]) is int
assert large_metric["name"] == "midi.usb-queue-age"
assert large_metric["count"] == 75 and large_metric["max_us"] == 60_000_000
assert large_metric["max_at_us"] == 80_000_000
assert large_metric["unit_a_max"] == 123_456_789 and large_metric["unit_b_max"] == 987_654_321
assert len(large_metric["bins_log2"]) == 33 and large_metric["bins_log2"][26] == 75
assert sum(large_metric["bins_log2"]) == 75
large_frames = [payload for payload in decoded if payload.get("total_handler_us") == 2**64 - 1]
assert {frame["index"] for frame in large_frames} == {0, 1, 2}
for frame in large_frames:
    assert type(frame["total_handler_us"]) is int
    assert [frame[key] for key in ("frames", "errors", "marker", "sequence", "started_at_us",
                                  "handler_us", "invalidated_pixels", "submitted_pixels")] == [37, 5, 19, 23, 123, 29, 31, 41]
    assert [phase["name"] for phase in frame["phases"]] == ["timer", "refresh", "layout", "style", "draw", "flush"]
    for phase in frame["phases"]:
        assert type(phase["total_us"]) is int and phase["total_us"] == 2**64 - 1
        assert phase["calls"] == 43 and phase["max_us"] == 47
print(f"[PASS] {len(payloads)} wire response payloads parse as JSON objects")
