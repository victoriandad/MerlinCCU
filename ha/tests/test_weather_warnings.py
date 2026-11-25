"""Tests for the official warnings payload (#202).

Run with: python -m unittest discover -s ha/tests
`weather_warnings.py` is loaded by path because the package's __init__ needs
Home Assistant installed.
"""

import importlib.util
import json
import pathlib
import sys
import unittest
from datetime import datetime, timezone

HERE = pathlib.Path(__file__).parent
MODULE_PATH = HERE.parent / "custom_components" / "merlinccu" / "weather_warnings.py"
_spec = importlib.util.spec_from_file_location("merlinccu_weather_warnings", MODULE_PATH)
ww = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = ww
_spec.loader.exec_module(ww)

TS = 1763900000
# The feed's times are UK local time, which the publisher passes as `now`;
# GMT in November, so UTC stands in without needing tz data installed.
UK = timezone.utc
# Tuesday 24 November 2026, 14:00.
NOW = datetime(2026, 11, 24, 14, 0, tzinfo=UK)


def rss(*items):
    """A region feed shaped like the Met Office's, with these (title, description) items."""
    body = "".join(
        f"<item><title>{title}</title><description>{description}</description></item>"
        for title, description in items
    )
    return (
        '<?xml version="1.0" encoding="UTF-8"?><rss version="2.0"><channel>'
        "<title>Met Office warnings for South West England</title>"
        f"{body}</channel></rss>"
    )


EMPTY_RSS = rss()

FLOODS = {
    "items": [
        {
            "description": "South Devon coast from Start Point to Dawlish Warren",
            "floodArea": {"riverOrSea": "South Devon Coast"},
            "severity": "Flood alert",
            "severityLevel": 3,
        },
        {
            "description": "River Frome at Dorchester",
            "floodArea": {"riverOrSea": "River Frome"},
            "severity": "Flood warning",
            "severityLevel": 2,
        },
        {"description": "Old one", "severity": "Warning no longer in force", "severityLevel": 4},
    ]
}


class MetOfficeTest(unittest.TestCase):
    def test_a_warning_in_force_reads_until_its_end(self):
        text = rss((
            "Amber warning of wind affecting South West England",
            "Amber warning of wind affecting South West England: Devon valid from 0600 Tue 24 Nov"
            " to 2100 Tue 24 Nov",
        ))
        [warning] = ww.parse_met_office(text, NOW)
        self.assertEqual(warning.level, ww.AMBER)
        self.assertEqual(warning.summary_hazard, "WIND")
        self.assertEqual(warning.line, "Amber wind until 21:00 Tue")

    def test_a_warning_still_to_come_reads_from_its_start(self):
        text = rss((
            "Yellow warning of rain, wind affecting South West England",
            "valid from 0600 Wed 25 Nov to 1800 Wed 25 Nov",
        ))
        [warning] = ww.parse_met_office(text, NOW)
        self.assertEqual(warning.summary_hazard, "RAIN/WIND")
        self.assertEqual(warning.line, "Yellow rain, wind from 06:00 Wed")

    def test_an_ended_warning_is_left_out(self):
        text = rss(("Yellow warning of fog affecting South West England",
                    "valid from 0000 Tue 24 Nov to 1000 Tue 24 Nov"))
        self.assertEqual(ww.parse_met_office(text, NOW), [])

    def test_the_year_follows_now_across_new_year(self):
        new_year = datetime(2026, 12, 31, 22, 0, tzinfo=UK)
        text = rss(("Yellow warning of snow and ice affecting Wales",
                    "valid from 1800 Thu 31 Dec to 1200 Fri 01 Jan"))
        [warning] = ww.parse_met_office(text, new_year)
        self.assertEqual(warning.summary_hazard, "SNOW/ICE")
        self.assertEqual(warning.line, "Yellow snow, ice until 12:00 Fri")

    def test_no_times_still_gives_the_warning(self):
        [warning] = ww.parse_met_office(rss(("Red warning of extreme heat", "")), NOW)
        self.assertEqual((warning.level, warning.summary_hazard, warning.line),
                         (ww.RED, "HEAT", "Red extreme heat"))

    def test_an_empty_feed_has_no_warnings(self):
        self.assertEqual(ww.parse_met_office(EMPTY_RSS, NOW), [])


class FloodTest(unittest.TestCase):
    def test_flood_levels_map_to_yellow_amber_red(self):
        warnings = ww.parse_floods(FLOODS)
        self.assertEqual([w.level for w in warnings], [ww.YELLOW, ww.AMBER])
        self.assertEqual(warnings[0].line, "Yellow flood alert: South Devon Coast")
        self.assertEqual(warnings[1].line, "Amber flood warning: River Frome")

    def test_missing_items_mean_no_floods(self):
        self.assertEqual(ww.parse_floods({}), [])


class PayloadTest(unittest.TestCase):
    def test_highest_first_with_its_summary(self):
        warnings = ww.parse_floods(FLOODS) + ww.parse_met_office(rss((
            "Red warning of wind affecting South West England",
            "valid from 0600 Tue 24 Nov to 2100 Tue 24 Nov",
        )), NOW)
        payload = json.loads(ww.payload(warnings, TS))
        self.assertEqual(payload["v"], 1)
        self.assertEqual(payload["ts"], TS)
        self.assertEqual(payload["level"], "red")
        self.assertEqual(payload["summary"], "RED WIND")
        self.assertEqual(payload["lines"][0], "Red wind until 21:00 Tue")
        self.assertEqual(payload["lines"][1], "Amber flood warning: River Frome")

    def test_no_warnings_says_none(self):
        payload = json.loads(ww.payload([], TS))
        self.assertEqual(payload["level"], "none")
        self.assertEqual(payload["lines"], [])

    def test_a_long_summary_keeps_to_the_alert_key(self):
        warning = ww.OfficialWarning(ww.YELLOW, "THUNDERSTORM/RAIN/WIND", "x")
        summary = json.loads(ww.payload([warning], TS))["summary"]
        self.assertLessEqual(len(summary), ww.MAX_SUMMARY_LENGTH)
        self.assertTrue(summary.startswith("YELLOW "))

    def test_too_many_lines_end_in_a_count(self):
        warnings = [
            ww.OfficialWarning(ww.YELLOW, "FLOOD", f"Yellow flood alert: River number {i:02d}")
            for i in range(10)
        ]
        text = ww.payload(warnings, TS)
        lines = json.loads(text)["lines"]
        self.assertLessEqual(len(lines), ww.MAX_LINES)
        self.assertLessEqual(len("\n".join(lines)), ww.MAX_DETAIL_LENGTH)
        self.assertRegex(lines[-1], r"^\+\d+ more$")
        shown = len(lines) - 1
        self.assertEqual(lines[-1], f"+{10 - shown} more")
        self.assertLessEqual(len(text.encode("utf-8")), ww.MAX_PAYLOAD_BYTES)

    def test_text_is_ascii_for_the_ccu_fonts(self):
        text = ww.payload(ww.parse_floods({"items": [{
            "floodArea": {"riverOrSea": "Afon Dyfi – Machynlleth"},
            "severity": "Flood warning", "severityLevel": 2,
        }]}), TS)
        self.assertTrue(text.isascii())
        self.assertIn('"level":"amber"', text)
        self.assertIn("Afon Dyfi Machynlleth", text)

    def test_topic(self):
        self.assertEqual(ww.topic("merlinccu"), "merlinccu/house/weather/warnings")


if __name__ == "__main__":
    unittest.main()
