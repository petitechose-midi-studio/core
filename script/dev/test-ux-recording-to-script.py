#!/usr/bin/env python3

import unittest

from ux_recording_to_script import ux_lines


class UxRecordingToScriptTest(unittest.TestCase):
    def test_converts_raw_and_manager_rows_at_the_logical_input_boundary(self) -> None:
        lines = [
            '{"type":"session_start","schema":5}',
            '12:00:00 [DBG] UXR {"seq":1,"ms":1000,"kind":"input",'
            '"gesture":"press","button":"NAV","button_id":40}',
            '{"type":"ux_event","seq":2,"ms":1025,"kind":"input",'
            '"gesture":"turn","encoder":"OPT","encoder_id":401,'
            '"value_kind":"absolute","value_milli":378}',
            '{"type":"ux_event","seq":3,"ms":1060,"kind":"input",'
            '"gesture":"release","button":"NAV","button_id":40}',
            '{"type":"ux_event","seq":4,"ms":1080,"kind":"input",'
            '"gesture":"turn","encoder":"NAV","encoder_id":400,'
            '"value_kind":"delta","delta_milli":-125}',
        ]

        self.assertEqual(
            ux_lines(lines),
            [
                "0 button NAV down",
                "25 encoder_value OPT 0.378",
                "60 button NAV up",
                "80 encoder_value NAV -0.125",
            ],
        )

    def test_rejects_a_recording_without_raw_inputs(self) -> None:
        with self.assertRaisesRegex(ValueError, "no replayable input"):
            ux_lines(['{"kind":"button","gesture":"press"}'])


if __name__ == "__main__":
    unittest.main()
