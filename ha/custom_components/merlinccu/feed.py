"""Reshape Yahoo Finance chart data into the MerlinCCU share feed contract.

Pure functions with no Home Assistant imports, so they can be tested with
plain ``python -m unittest`` (see ``ha/tests``). The contract itself is
described in ``docs/share-feed-design.md``.
"""

from __future__ import annotations

import json
import math
from dataclasses import dataclass
from typing import Any, Optional

# The CCU's graph buffer holds 24 points (share_feed_parser.cpp). Sending
# more only fills its 4 KB response buffer, so the feed downsamples to this.
HISTORY_POINTS = 24

# The CCU finds keys by exact text such as `"history":[`, so the payload must
# be compact JSON with no spaces after separators.
JSON_SEPARATORS = (",", ":")


@dataclass(frozen=True)
class Period:
    """One CCU share period and the Yahoo chart query that fills it."""

    range: str
    interval: str
    cache_seconds: int


# Keys are the CCU's `period=` values. Cache times keep Yahoo calls rare:
# intraday data moves, older history barely does.
PERIODS: dict[str, Period] = {
    "today": Period("1d", "5m", 5 * 60),
    "week": Period("5d", "1h", 30 * 60),
    "month": Period("1mo", "1d", 6 * 3600),
    "year": Period("1y", "1wk", 6 * 3600),
    "all_time": Period("max", "1mo", 24 * 3600),
}

DEFAULT_PERIOD = "today"

# Yahoo reports LSE prices in pence as "GBp"; the CCU and most displays use
# the ISO-style "GBX" for that.
CURRENCY_NAMES = {"GBp": "GBX", "ZAc": "ZAC", "ILA": "ILA"}


@dataclass(frozen=True)
class WatchedShare:
    """One share the integration reports, from configuration.yaml."""

    symbol: str
    name: Optional[str] = None
    exchange: Optional[str] = None


def chart_url(symbol: str, period: Period) -> str:
    """Return the Yahoo chart URL for one symbol and period."""
    return (
        "https://query1.finance.yahoo.com/v8/finance/chart/"
        f"{symbol}?range={period.range}&interval={period.interval}"
    )


def downsample(values: list[float], count: int = HISTORY_POINTS) -> list[float]:
    """Pick `count` evenly spaced values, always keeping the first and last."""
    if len(values) <= count:
        return list(values)
    last = len(values) - 1
    return [values[(i * last) // (count - 1)] for i in range(count)]


def format_change(percent: float) -> str:
    """Format a percentage change as the CCU displays it, e.g. `+0.02%`."""
    if abs(percent) < 0.005:
        percent = 0.0
    return f"{percent:+.2f}%"


def _closes(result: dict[str, Any]) -> list[float]:
    """Return the chart's closing prices, skipping the gaps Yahoo leaves as null."""
    quotes = (result.get("indicators") or {}).get("quote") or [{}]
    closes = quotes[0].get("close") or []
    return [float(c) for c in closes if isinstance(c, (int, float)) and math.isfinite(c)]


def share_from_chart(
    share: WatchedShare, period_key: str, chart: dict[str, Any]
) -> Optional[dict[str, Any]]:
    """Build one CCU share entry from a Yahoo chart response.

    Returns None when the response holds no usable price, so the caller can
    fall back to a cached entry or report an error state.
    """
    results = (chart.get("chart") or {}).get("result") or []
    if not results:
        return None
    result = results[0]
    meta = result.get("meta") or {}
    closes = _closes(result)

    price = meta.get("regularMarketPrice")
    if not isinstance(price, (int, float)):
        price = closes[-1] if closes else None
    if price is None:
        return None

    # Today compares with the previous close, as a quote screen does. Longer
    # periods compare with the start of the period, matching their graph.
    if period_key == "today":
        base = meta.get("chartPreviousClose") or meta.get("previousClose")
    else:
        base = closes[0] if closes else None

    entry: dict[str, Any] = {"symbol": share.symbol}
    if share.name:
        entry["name"] = share.name
    exchange = share.exchange or meta.get("exchangeName")
    if exchange:
        entry["exchange"] = exchange
    currency = meta.get("currency")
    if currency:
        entry["currency"] = CURRENCY_NAMES.get(currency, currency.upper())
    entry["price"] = round(float(price), 2)
    if isinstance(base, (int, float)) and base > 0:
        entry["change"] = format_change((float(price) - base) / base * 100.0)
    entry["data_state"] = "live"
    # The CCU graphs whole units (share_feed_parser.cpp rounds each point),
    # so fractions would only cost buffer space.
    entry["history"] = [round(v) for v in downsample(closes)]
    return entry


def stale(entry: dict[str, Any]) -> dict[str, Any]:
    """Return a copy of a cached entry marked stale."""
    copy = dict(entry)
    copy["data_state"] = "stale"
    return copy


def error_entry(share: WatchedShare) -> dict[str, Any]:
    """Return the entry for a share with no data at all."""
    return {"symbol": share.symbol, "data_state": "error"}


def payload(entries: list[dict[str, Any]]) -> str:
    """Serialise the feed response in the compact form the CCU expects."""
    return json.dumps({"shares": entries}, separators=JSON_SEPARATORS)
