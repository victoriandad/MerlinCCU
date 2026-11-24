"""Tests for the Controls payloads (#152).

Run with: python -m unittest discover -s ha/tests
`controls.py` is loaded by path because the package's __init__ needs Home
Assistant installed.
"""

import importlib.util
import json
import pathlib
import sys
import unittest

HERE = pathlib.Path(__file__).parent
MODULE_PATH = HERE.parent / "custom_components" / "merlinccu" / "controls.py"
_spec = importlib.util.spec_from_file_location("merlinccu_controls", MODULE_PATH)
ctl = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = ctl
_spec.loader.exec_module(ctl)

MAC = "2ccf67a1b2c3"
HALL = ctl.Control("light.hall", "Hall light")
EVENING = ctl.Control("scene.evening", "Evening")
FAN = ctl.Control("fan.study", "Study fan")


class ListPayloadTest(unittest.TestCase):
    def test_names_kinds_and_states_line_up(self):
        payload = json.loads(
            ctl.list_payload(
                [HALL, EVENING, FAN],
                {"light.hall": "on", "scene.evening": "2026-11-24T18:00:00", "fan.study": None},
            )
        )
        self.assertEqual(payload["v"], 1)
        self.assertEqual(payload["names"], ["Hall light", "Evening", "Study fan"])
        self.assertEqual(payload["kinds"], ["toggle", "run", "toggle"])
        # A scene's state is when it last ran; the CCU only needs to know it
        # is there. A missing entity is unavailable.
        self.assertEqual(payload["states"], ["on", "", "unavailable"])
        self.assertEqual(len(payload["list"]), ctl.LIST_ID_LENGTH)

    def test_compact_json_as_the_ccu_parser_expects(self):
        text = ctl.list_payload([HALL], {"light.hall": "off"})
        self.assertNotIn(": ", text)
        self.assertTrue(text.startswith('{"v":1,"list":"'))

    def test_list_id_changes_with_entities_or_names(self):
        base = ctl.list_id([HALL, EVENING])
        self.assertEqual(base, ctl.list_id([HALL, EVENING]))
        self.assertNotEqual(base, ctl.list_id([EVENING, HALL]))
        self.assertNotEqual(base, ctl.list_id([ctl.Control("light.hall", "Hall"), EVENING]))

    def test_unknown_state_is_unavailable(self):
        self.assertEqual(ctl.state_text(HALL, "unknown"), "unavailable")
        self.assertEqual(ctl.state_text(EVENING, "unavailable"), "unavailable")
        self.assertEqual(ctl.state_text(HALL, "off"), "off")

    def test_the_largest_list_fits_the_ccu_buffer(self):
        items = [ctl.Control(f"light.l{i}", "W" * ctl.NAME_LENGTH) for i in range(ctl.MAX_CONTROLS)]
        text = ctl.list_payload(items, {item.entity_id: "unavailable" for item in items})
        self.assertLessEqual(len(text), ctl.MAX_LIST_BYTES)


class NameTest(unittest.TestCase):
    def test_names_are_ascii_without_quotes_and_cut_to_length(self):
        self.assertEqual(ctl.clean_name('Café "lights"'), "Cafe lights")
        self.assertEqual(ctl.clean_name("Back\\door  \t lamp"), "Backdoor lamp")
        self.assertEqual(ctl.clean_name("A" * 30), "A" * ctl.NAME_LENGTH)
        self.assertEqual(ctl.clean_name("Christmas tree lights"), "Christmas tree light")


class PressTest(unittest.TestCase):
    def test_press_payload(self):
        self.assertEqual(ctl.parse_press("ab12cd34:3"), ("ab12cd34", 3))
        self.assertIsNone(ctl.parse_press("ab12cd34"))
        self.assertIsNone(ctl.parse_press("ab12:3"))
        self.assertIsNone(ctl.parse_press("ab12cd34:-1"))

    def test_topics(self):
        self.assertEqual(ctl.topic("merlinccu", MAC, ctl.LIST), f"merlinccu/{MAC}/controls/list")
        self.assertEqual(ctl.mac_from_topic("merlinccu", f"merlinccu/{MAC}/controls/press"), MAC)
        self.assertEqual(ctl.mac_from_topic("home/ccu", f"home/ccu/{MAC}/controls/press"), MAC)
        self.assertIsNone(ctl.mac_from_topic("merlinccu", f"merlinccu/{MAC}/controls/list"))
        self.assertIsNone(ctl.mac_from_topic("merlinccu", f"other/{MAC}/controls/press"))

    def test_services(self):
        self.assertEqual(ctl.service_for(HALL), ("light", "toggle"))
        self.assertEqual(ctl.service_for(EVENING), ("scene", "turn_on"))
        self.assertEqual(ctl.service_for(ctl.Control("script.bedtime", "Bedtime")),
                         ("script", "turn_on"))

    def test_result_fits_the_ccu_buffer(self):
        self.assertEqual(json.loads(ctl.result_payload(2, True)), {"v": 1, "index": 2, "ok": True})
        text = ctl.result_payload(23, False, "Ünïcode \"error\" " * 20)
        self.assertLessEqual(len(text), 160)
        self.assertFalse(json.loads(text)["ok"])


if __name__ == "__main__":
    unittest.main()
