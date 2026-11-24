"""Tests for tools/soak_monitor.py's event detection (#155).

Run with: python -m unittest discover -s tools/tests
"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import soak_monitor  # noqa: E402


def reading(uptime=10, mqtt="1", ha="1", reset="power_on", heap=4096):
    return {"uptime_min": str(uptime), "mqtt_connected": mqtt, "ha_connected": ha,
            "reset": reset, "heap_arena": str(heap)}


class DetectEventsTest(unittest.TestCase):
    def setUp(self):
        self.log = soak_monitor.UnitLog()

    def test_first_reading_is_quiet(self):
        self.assertEqual(soak_monitor.detect_events(self.log, reading(), 0.0), [])

    def test_unreachable_and_back_reports_the_outage(self):
        soak_monitor.detect_events(self.log, reading(), 0.0)
        self.assertEqual(soak_monitor.detect_events(self.log, None, 60.0), ["unreachable"])
        self.assertEqual(soak_monitor.detect_events(self.log, None, 120.0), [])
        events = soak_monitor.detect_events(self.log, reading(uptime=12), 180.0)
        self.assertEqual(events, ["reachable again after 120 s"])

    def test_link_drop_and_recovery_time(self):
        soak_monitor.detect_events(self.log, reading(), 0.0)
        self.assertEqual(soak_monitor.detect_events(self.log, reading(mqtt="0"), 60.0),
                         ["mqtt down"])
        self.assertEqual(soak_monitor.detect_events(self.log, reading(), 150.0),
                         ["mqtt back after 90 s"])
        self.assertEqual(self.log.outages["mqtt_connected"], [90.0])

    def test_restart_flags_unexpected_reasons(self):
        soak_monitor.detect_events(self.log, reading(uptime=500), 0.0)
        events = soak_monitor.detect_events(self.log, reading(uptime=1, reset="watchdog"), 60.0)
        self.assertEqual(events, ["restarted: watchdog (UNEXPECTED), up 1 min"])
        events = soak_monitor.detect_events(self.log, reading(uptime=0, reset="update"), 120.0)
        self.assertEqual(events, ["restarted: update, up 0 min"])
        self.assertIn("1 unexpected: watchdog", soak_monitor.summary("unit", self.log))

    def test_heap_growth_is_reported(self):
        soak_monitor.detect_events(self.log, reading(heap=4096), 0.0)
        self.assertEqual(soak_monitor.detect_events(self.log, reading(heap=4096), 60.0), [])
        self.assertEqual(soak_monitor.detect_events(self.log, reading(heap=8192), 120.0),
                         ["heap arena grew to 8192 B (+4096 B)"])

    def test_parse_status_reads_key_value_lines(self):
        self.assertEqual(soak_monitor.parse_status("version=abc\nuptime_min=5\n"),
                         {"version": "abc", "uptime_min": "5"})


if __name__ == "__main__":
    unittest.main()
