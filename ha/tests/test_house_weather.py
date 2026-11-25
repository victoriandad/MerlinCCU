"""Tests for the house weather payloads (#153).

Run with: python -m unittest discover -s ha/tests
`house_weather.py` is loaded by path because the package's __init__ needs
Home Assistant installed.
"""

import importlib.util
import json
import pathlib
import sys
import unittest
from datetime import datetime, timedelta, timezone

HERE = pathlib.Path(__file__).parent
MODULE_PATH = HERE.parent / "custom_components" / "merlinccu" / "house_weather.py"
_spec = importlib.util.spec_from_file_location("merlinccu_house_weather", MODULE_PATH)
hw = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = hw
_spec.loader.exec_module(hw)

TS = 1763900000

# As HA's Met.no weather entity reports them.
ATTRIBUTES = {
    "temperature": 7.24,
    "temperature_unit": "°C",
    "wind_speed": 14.4,
    "wind_speed_unit": "km/h",
    "wind_bearing": 225.3,
    "humidity": 93,
    "pressure": 1002.1,
    "attribution": "Weather forecast from met.no, delivered by the Norwegian "
    "Meteorological Institute.",
    "friendly_name": "Forecast Home",
}
SUN = {"next_rising": "2026-11-24T07:41:00+00:00", "next_setting": "2026-11-24T16:01:00+00:00"}


def hourly_rows(count, condition="lightning-rainy"):
    start = datetime(2026, 11, 23, 18, tzinfo=timezone.utc)
    return [
        {
            "datetime": (start + timedelta(hours=i)).isoformat(),
            "condition": condition,
            "temperature": 7.15,
            "wind_speed": 14.44,
            "wind_bearing": 225.0,
            "precipitation": 0.3,
            "humidity": 90,
        }
        for i in range(count)
    ]


class NowPayloadTest(unittest.TestCase):
    def test_now_keeps_only_what_the_ccu_reads(self):
        payload = json.loads(hw.now_payload("rainy", ATTRIBUTES, SUN, TS))
        self.assertEqual(payload["v"], 1)
        self.assertEqual(payload["ts"], TS)
        self.assertEqual(payload["state"], "rainy")
        self.assertEqual(payload["temperature"], 7.2)
        self.assertEqual(payload["wind_bearing"], 225)
        self.assertEqual(payload["temperature_unit"], "°C")
        self.assertEqual(payload["next_rising"], SUN["next_rising"])
        self.assertNotIn("humidity", payload)
        self.assertNotIn("friendly_name", payload)

    def test_now_is_compact_json_the_ccu_scanner_can_read(self):
        text = hw.now_payload("rainy", ATTRIBUTES, SUN, TS)
        self.assertIn('"state":"rainy"', text)
        self.assertNotIn(": ", text)

    def test_no_data_publishes_nothing(self):
        for state in (None, "", "unavailable", "unknown"):
            self.assertIsNone(hw.now_payload(state, ATTRIBUTES, SUN, TS))

    def test_works_without_the_sun_entity(self):
        payload = json.loads(hw.now_payload("sunny", ATTRIBUTES, None, TS))
        self.assertNotIn("next_rising", payload)

    def test_a_long_attribution_is_dropped_to_fit(self):
        attributes = dict(ATTRIBUTES, attribution="x" * 600)
        text = hw.now_payload("sunny", attributes, SUN, TS)
        self.assertLessEqual(len(text.encode("utf-8")), hw.MAX_PAYLOAD_BYTES[hw.NOW])
        self.assertNotIn("attribution", json.loads(text))


class ForecastPayloadTest(unittest.TestCase):
    def test_hourly_is_capped_at_the_ccu_row_count_and_buffer(self):
        text = hw.forecast_payload(hw.HOURLY, hourly_rows(48), ATTRIBUTES, TS)
        payload = json.loads(text)
        self.assertLessEqual(len(payload["forecast"]), hw.MAX_ENTRIES[hw.HOURLY])
        self.assertLessEqual(len(text.encode("utf-8")), hw.MAX_PAYLOAD_BYTES[hw.HOURLY])
        self.assertEqual(payload["wind_speed_unit"], "km/h")
        self.assertNotIn("humidity", payload["forecast"][0])

    def test_a_typical_day_of_hourly_rows_fits_whole(self):
        text = hw.forecast_payload(hw.HOURLY, hourly_rows(24, "cloudy"), ATTRIBUTES, TS)
        self.assertEqual(len(json.loads(text)["forecast"]), 24)

    def test_precipitation_and_gusts_ride_along_for_exceptional_weather(self):
        # #202: a wet, gusty day of Met Office-style rows still fits whole.
        rows = [dict(r, wind_gust_speed=61.27) for r in hourly_rows(24, "rainy")]
        attributes = dict(ATTRIBUTES, precipitation_unit="mm")
        text = hw.forecast_payload(hw.HOURLY, rows, attributes, TS)
        payload = json.loads(text)
        self.assertEqual(len(payload["forecast"]), 24)
        self.assertEqual(payload["precipitation_unit"], "mm")
        self.assertEqual(payload["forecast"][0]["precipitation"], 0.3)
        self.assertEqual(payload["forecast"][0]["wind_gust_speed"], 61.3)

    def test_a_dry_period_sends_no_precipitation(self):
        rows = [dict(r, precipitation=0.0) for r in hourly_rows(2, "sunny")]
        payload = json.loads(hw.forecast_payload(hw.HOURLY, rows, ATTRIBUTES, TS))
        self.assertNotIn("precipitation", payload["forecast"][0])

    def test_daily_carries_the_low_temperature(self):
        rows = [dict(r, templow=2.04) for r in hourly_rows(10, "sunny")]
        payload = json.loads(hw.forecast_payload(hw.DAILY, rows, ATTRIBUTES, TS))
        self.assertEqual(len(payload["forecast"]), hw.MAX_ENTRIES[hw.DAILY])
        self.assertEqual(payload["forecast"][0]["templow"], 2.0)

    def test_datetime_objects_are_sent_as_iso_text(self):
        rows = [{"datetime": datetime(2026, 11, 23, 18, tzinfo=timezone.utc), "temperature": 5}]
        payload = json.loads(hw.forecast_payload(hw.HOURLY, rows, ATTRIBUTES, TS))
        self.assertEqual(payload["forecast"][0]["datetime"], "2026-11-23T18:00:00+00:00")

    def test_no_rows_publishes_nothing(self):
        self.assertIsNone(hw.forecast_payload(hw.HOURLY, None, ATTRIBUTES, TS))
        self.assertIsNone(hw.forecast_payload(hw.DAILY, [{"condition": "sunny"}], ATTRIBUTES, TS))

    def test_topics(self):
        self.assertEqual(hw.topic("merlinccu", hw.NOW), "merlinccu/house/weather/now")


if __name__ == "__main__":
    unittest.main()
