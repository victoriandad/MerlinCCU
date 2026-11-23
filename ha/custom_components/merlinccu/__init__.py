"""MerlinCCU support for Home Assistant: share feed and house weather.

Share feed: serves `GET /api/merlinccu/shares?period=...` on Home Assistant's
own port, in the shape the MerlinCCU firmware expects
(docs/share-feed-design.md). Prices come from Yahoo Finance's chart API,
fetched here so the CCU never talks to a market-data provider itself.

House weather (#153): publishes one HA weather entity to every CCU over MQTT
(weather_publisher.py, docs/mqtt-household-data.md).

Configured in configuration.yaml; see docs/home-assistant-setup.md.
"""

from __future__ import annotations

import asyncio
import hmac
import logging
import time
from typing import Any

import aiohttp
import voluptuous as vol
from aiohttp import web

from homeassistant.components.http import HomeAssistantView
from homeassistant.core import HomeAssistant
from homeassistant.helpers import config_validation as cv
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers.typing import ConfigType

from . import feed

_LOGGER = logging.getLogger(__name__)

DOMAIN = "merlinccu"
FEED_PATH = "/api/merlinccu/shares"

CONF_FEED_TOKEN = "feed_token"
CONF_SHARES = "shares"
CONF_SYMBOL = "symbol"
CONF_NAME = "name"
CONF_EXCHANGE = "exchange"
CONF_WEATHER = "weather"
CONF_ENTITY = "entity"
CONF_SUN = "sun"
CONF_BASE_TOPIC = "base_topic"

# The CCU gives up on a response after 5 s (kIoTimeoutMs in
# share_price_manager.cpp), so Yahoo gets less than that; a slow fetch is
# answered from the cache instead.
UPSTREAM_TIMEOUT_SECONDS = 4.0

# Yahoo refuses (HTTP 429) requests without a browser-like User-Agent.
UPSTREAM_HEADERS = {"User-Agent": "Mozilla/5.0 (compatible; MerlinCCU-HA/1.0)"}

# Matches the CCU's watchlist size (kMaxWatchedShares in console_model.h).
MAX_SHARES = 6

SHARE_SCHEMA = vol.Schema(
    {
        vol.Required(CONF_SYMBOL): vol.All(cv.string, vol.Upper, vol.Length(min=1, max=9)),
        vol.Optional(CONF_NAME): vol.All(cv.string, vol.Length(max=23)),
        vol.Optional(CONF_EXCHANGE): vol.All(cv.string, vol.Length(max=7)),
    }
)

WEATHER_SCHEMA = vol.Schema(
    {
        vol.Required(CONF_ENTITY): cv.entity_domain("weather"),
        vol.Optional(CONF_SUN, default="sun.sun"): cv.entity_domain("sun"),
        # The CCUs' MQTT base topic setting (default "merlinccu").
        vol.Optional(CONF_BASE_TOPIC, default="merlinccu"): vol.All(
            cv.string, vol.Length(min=1, max=40)
        ),
    }
)

CONFIG_SCHEMA = vol.Schema(
    {
        DOMAIN: vol.Schema(
            {
                vol.Optional(CONF_FEED_TOKEN): vol.All(cv.string, vol.Length(max=127)),
                vol.Optional(CONF_SHARES): vol.All(
                    cv.ensure_list, [SHARE_SCHEMA], vol.Length(min=1, max=MAX_SHARES)
                ),
                vol.Optional(CONF_WEATHER): WEATHER_SCHEMA,
            }
        )
    },
    extra=vol.ALLOW_EXTRA,
)


async def async_setup(hass: HomeAssistant, config: ConfigType) -> bool:
    """Set up the share feed and the weather publisher from the `merlinccu:` YAML block."""
    conf = config[DOMAIN]
    if CONF_SHARES in conf:
        shares = [
            feed.WatchedShare(s[CONF_SYMBOL], s.get(CONF_NAME), s.get(CONF_EXCHANGE))
            for s in conf[CONF_SHARES]
        ]
        hass.http.register_view(SharesFeedView(hass, shares, conf.get(CONF_FEED_TOKEN)))
        _LOGGER.info("MerlinCCU share feed serving %d share(s) at %s", len(shares), FEED_PATH)

    if CONF_WEATHER in conf:
        # Imported here so a share-feed-only setup never needs MQTT.
        from .weather_publisher import WeatherPublisher

        weather = conf[CONF_WEATHER]
        WeatherPublisher(
            hass, weather[CONF_ENTITY], weather[CONF_SUN], weather[CONF_BASE_TOPIC]
        ).async_start()
        _LOGGER.info(
            "MerlinCCU weather from %s published under %s/house/weather",
            weather[CONF_ENTITY],
            weather[CONF_BASE_TOPIC],
        )
    return True


class SharesFeedView(HomeAssistantView):
    """Answers the CCU's share feed request from a per-period cache."""

    url = FEED_PATH
    name = "api:merlinccu:shares"
    # HA's long-lived tokens are longer than the CCU's 127-character feed
    # token field, so this endpoint checks its own shorter token instead.
    requires_auth = False

    def __init__(
        self, hass: HomeAssistant, shares: list[feed.WatchedShare], token: str | None
    ) -> None:
        self._hass = hass
        self._shares = shares
        self._token = token
        # (symbol, period) -> (monotonic time fetched, entry)
        self._cache: dict[tuple[str, str], tuple[float, dict[str, Any]]] = {}
        self._lock = asyncio.Lock()

    def _authorised(self, request: web.Request) -> bool:
        if not self._token:
            return True
        expected = f"Bearer {self._token}"
        return hmac.compare_digest(request.headers.get("Authorization", ""), expected)

    async def get(self, request: web.Request) -> web.Response:
        """Return every configured share for the requested period."""
        if not self._authorised(request):
            return web.Response(status=401, text="Unauthorized")

        period_key = request.query.get("period", feed.DEFAULT_PERIOD)
        if period_key not in feed.PERIODS:
            period_key = feed.DEFAULT_PERIOD

        # One refresh at a time, so a CCU retry can't start a second round
        # of Yahoo calls while the first is still running.
        async with self._lock:
            entries = await asyncio.gather(
                *(self._entry(share, period_key) for share in self._shares)
            )
        return web.Response(text=feed.payload(list(entries)), content_type="application/json")

    async def _entry(self, share: feed.WatchedShare, period_key: str) -> dict[str, Any]:
        period = feed.PERIODS[period_key]
        key = (share.symbol, period_key)
        cached = self._cache.get(key)
        now = time.monotonic()
        if cached and now - cached[0] < period.cache_seconds:
            return cached[1]

        entry = await self._fetch(share, period_key)
        if entry is not None:
            self._cache[key] = (now, entry)
            return entry
        if cached:
            return feed.stale(cached[1])
        return feed.error_entry(share)

    async def _fetch(self, share: feed.WatchedShare, period_key: str) -> dict[str, Any] | None:
        session = async_get_clientsession(self._hass)
        url = feed.chart_url(share.symbol, feed.PERIODS[period_key])
        try:
            async with asyncio.timeout(UPSTREAM_TIMEOUT_SECONDS):
                async with session.get(url, headers=UPSTREAM_HEADERS) as response:
                    if response.status != 200:
                        _LOGGER.warning(
                            "Yahoo chart for %s returned HTTP %s", share.symbol, response.status
                        )
                        return None
                    chart = await response.json(content_type=None)
        except (aiohttp.ClientError, TimeoutError, ValueError) as err:
            _LOGGER.warning("Yahoo chart for %s failed: %s", share.symbol, err)
            return None

        entry = feed.share_from_chart(share, period_key, chart)
        if entry is None:
            _LOGGER.warning("Yahoo chart for %s had no price", share.symbol)
        return entry
