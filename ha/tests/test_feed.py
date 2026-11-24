"""Tests for the share feed reshaping, run with: python -m unittest discover ha/tests

The fixtures are real Yahoo chart responses for BA.L. `feed.py` is loaded by
path because the package's __init__ needs Home Assistant installed.
"""

import importlib.util
import json
import pathlib
import sys
import unittest

HERE = pathlib.Path(__file__).parent
FEED_PATH = HERE.parent / "custom_components" / "merlinccu" / "feed.py"
_spec = importlib.util.spec_from_file_location("merlinccu_feed", FEED_PATH)
feed = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = feed  # dataclasses look their module up here
_spec.loader.exec_module(feed)

# The CCU's share feed response buffer (kResponseBufferSize in
# share_price_manager.cpp), which also holds the HTTP headers.
CCU_RESPONSE_BUFFER = 4096
# Generous allowance for the headers HA's web server sends.
HEADER_ALLOWANCE = 512


def fixture(name):
    return json.loads((HERE / "fixtures" / name).read_text(encoding="utf-8"))


BAE = feed.WatchedShare("BA.L", "BAE SYSTEMS")


class ShareFromChartTests(unittest.TestCase):
    def test_today_compares_with_the_previous_close(self):
        chart = fixture("yahoo_ba_l_1d_5m.json")
        meta = chart["chart"]["result"][0]["meta"]
        entry = feed.share_from_chart(BAE, "today", chart, now=meta["regularMarketTime"])

        self.assertEqual(entry["symbol"], "BA.L")
        self.assertEqual(entry["name"], "BAE SYSTEMS")
        self.assertEqual(entry["exchange"], "LSE")
        self.assertEqual(entry["currency"], "GBX")
        self.assertEqual(entry["price"], meta["regularMarketPrice"])
        expected = (meta["regularMarketPrice"] / meta["chartPreviousClose"] - 1.0) * 100.0
        self.assertEqual(entry["change"], f"{expected:+.2f}%")
        self.assertEqual(entry["data_state"], "live")
        self.assertEqual(len(entry["history"]), feed.HISTORY_POINTS)
        # The CCU measures the graph from the base: yesterday's close (#178).
        self.assertEqual(entry["base"], feed.graph_point(meta["chartPreviousClose"]))
        self.assertTrue(all(isinstance(v, (int, float)) for v in entry["history"]))

    def test_today_is_flat_when_nothing_has_traded_today(self):
        # The fixture's last trade was a Friday; ask on the Saturday.
        chart = fixture("yahoo_ba_l_1d_5m.json")
        meta = chart["chart"]["result"][0]["meta"]
        saturday = meta["regularMarketTime"] + 86400
        entry = feed.share_from_chart(BAE, "today", chart, now=saturday)

        self.assertEqual(entry["price"], meta["regularMarketPrice"])
        self.assertEqual(entry["change"], "+0.00%")
        self.assertEqual(entry["history"], [feed.graph_point(meta["regularMarketPrice"])])
        self.assertEqual(entry["base"], entry["history"][0])

    def test_only_today_goes_flat_out_of_hours(self):
        chart = fixture("yahoo_ba_l_1y_1wk.json")
        meta = chart["chart"]["result"][0]["meta"]
        entry = feed.share_from_chart(BAE, "year", chart, now=meta["regularMarketTime"] + 86400)
        self.assertNotEqual(entry["change"], "+0.00%")
        self.assertEqual(len(entry["history"]), feed.HISTORY_POINTS)

    def test_longer_periods_compare_with_the_start_of_the_period(self):
        chart = fixture("yahoo_ba_l_1y_1wk.json")
        closes = [c for c in chart["chart"]["result"][0]["indicators"]["quote"][0]["close"] if c]
        entry = feed.share_from_chart(BAE, "year", chart)

        expected = (entry["price"] / closes[0] - 1.0) * 100.0
        self.assertEqual(entry["change"], f"{expected:+.2f}%")
        self.assertEqual(entry["history"][0], round(closes[0]))
        self.assertEqual(entry["history"][-1], round(closes[-1]))

    def test_name_is_left_out_unless_configured(self):
        # The CCU keeps its own display name when the feed sends none.
        entry = feed.share_from_chart(feed.WatchedShare("BA.L"), "today",
                                      fixture("yahoo_ba_l_1d_5m.json"))
        self.assertNotIn("name", entry)

    def test_unknown_symbol_gives_no_entry(self):
        self.assertIsNone(feed.share_from_chart(BAE, "today", fixture("yahoo_unknown_symbol.json")))

    def test_null_closes_are_skipped(self):
        chart = {"chart": {"result": [{"meta": {"regularMarketPrice": 10.0},
                                       "indicators": {"quote": [{"close": [9.0, None, 11.0]}]}}]}}
        entry = feed.share_from_chart(BAE, "week", chart)
        self.assertEqual(entry["history"], [9, 11])
        self.assertEqual(entry["change"], "+11.11%")


class HelperTests(unittest.TestCase):
    def test_downsample_keeps_both_ends(self):
        values = list(range(100))
        picked = feed.downsample(values, 24)
        self.assertEqual(len(picked), 24)
        self.assertEqual(picked[0], 0)
        self.assertEqual(picked[-1], 99)

    def test_format_change(self):
        self.assertEqual(feed.format_change(0.0213), "+0.02%")
        self.assertEqual(feed.format_change(-1.4), "-1.40%")
        self.assertEqual(feed.format_change(-0.001), "+0.00%")

    def test_stale_and_error_states(self):
        live = feed.share_from_chart(BAE, "today", fixture("yahoo_ba_l_1d_5m.json"))
        self.assertEqual(feed.stale(live)["data_state"], "stale")
        self.assertEqual(live["data_state"], "live")
        self.assertEqual(feed.error_entry(BAE), {"symbol": "BA.L", "data_state": "error"})


class PayloadTests(unittest.TestCase):
    def test_payload_is_compact(self):
        # share_feed_parser.cpp looks for keys such as `"history":[` verbatim.
        entry = feed.share_from_chart(BAE, "today", fixture("yahoo_ba_l_1d_5m.json"))
        text = feed.payload([entry])
        self.assertIn('"symbol":"BA.L"', text)
        self.assertIn('"history":[', text)
        self.assertNotIn(": ", text)

    def test_six_shares_fit_the_ccu_buffer(self):
        entry = feed.share_from_chart(BAE, "all_time", fixture("yahoo_ba_l_1d_5m.json"))
        entries = [dict(entry, symbol=f"SYMBOL{i}.L", name="A" * 23) for i in range(6)]
        size = len(feed.payload(entries).encode("utf-8"))
        self.assertLess(size + HEADER_ALLOWANCE, CCU_RESPONSE_BUFFER)

class GraphPointTests(unittest.TestCase):
    def test_low_prices_keep_their_detail(self):
        # #178: a share under 10 keeps four decimals, others two.
        self.assertEqual(feed.graph_point(0.452349), 0.4523)
        self.assertEqual(feed.graph_point(1372.456), 1372.46)


if __name__ == "__main__":
    unittest.main()
