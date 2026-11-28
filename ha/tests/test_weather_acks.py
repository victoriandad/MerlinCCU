"""Tests for the weather acknowledgement payloads (#210).

Run with: python -m unittest discover -s ha/tests
`weather_acks.py` is loaded by path because the package's __init__ needs Home
Assistant installed.
"""

import importlib.util
import json
import pathlib
import sys
import unittest

HERE = pathlib.Path(__file__).parent
MODULE_PATH = HERE.parent / "custom_components" / "merlinccu" / "weather_acks.py"
_spec = importlib.util.spec_from_file_location("merlinccu_weather_acks_payloads", MODULE_PATH)
wa = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = wa
_spec.loader.exec_module(wa)


class ParseAckTest(unittest.TestCase):
    def test_reads_what_the_firmware_sends(self):
        # As weather_acks::format_ack() writes it.
        payload = b'{"alert":"wx_temp","id":"1a2b3c4d","unit":"merlinccu2"}'
        self.assertEqual(wa.parse_ack(payload), ("wx_temp", "1a2b3c4d", "merlinccu2"))

    def test_refuses_unknown_kinds_and_bad_ids(self):
        self.assertIsNone(wa.parse_ack('{"alert":"heap","id":"1a2b3c4d"}'))
        self.assertIsNone(wa.parse_ack('{"alert":"official","id":"1A2B3C4D"}'))
        self.assertIsNone(wa.parse_ack('{"alert":"official","id":"123"}'))
        self.assertIsNone(wa.parse_ack("not json"))
        self.assertIsNone(wa.parse_ack("[1,2]"))

    def test_an_odd_unit_name_is_kept_out_of_the_log(self):
        ack = wa.parse_ack('{"alert":"official","id":"00c0ffee","unit":"<script>"}')
        self.assertEqual(ack, ("official", "00c0ffee", "unknown"))


class PayloadTest(unittest.TestCase):
    def test_lists_known_kinds_in_order(self):
        payload = wa.acks_payload({"wx_wind": "0000beef", "official": "1a2b3c4d"})
        self.assertEqual(payload, '{"v":1,"official":"1a2b3c4d","wx_wind":"0000beef"}')
        self.assertEqual(json.loads(wa.acks_payload({})), {"v": 1})

    def test_clean_drops_damaged_storage(self):
        self.assertEqual(
            wa.clean({"official": "1a2b3c4d", "wx_temp": "zz", "other": "1a2b3c4d"}),
            {"official": "1a2b3c4d"},
        )
        self.assertEqual(wa.clean(None), {})

    def test_topics_stay_out_of_the_panels_filters(self):
        # Panels subscribe to <base>/house/weather/+ and <base>/+/+; the set
        # topic matches neither, the acks topic the first.
        self.assertEqual(wa.set_topic("merlinccu"), "merlinccu/house/acks/set")
        self.assertEqual(wa.acks_topic("merlinccu"), "merlinccu/house/weather/acks")


if __name__ == "__main__":
    unittest.main()
