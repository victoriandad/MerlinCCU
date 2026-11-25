"""Publishes official weather and flood warnings to every MerlinCCU (#202).

Fetches the Met Office warnings for one region and the Environment Agency
flood warnings near one point every 15 minutes, and publishes them retained
(weather_warnings.py), so a CCU has them as soon as it subscribes.

If a configured source cannot be read, nothing is published that round: the
CCUs keep the last payload, and its timestamp shows them when it goes stale,
rather than being told a warning has ended when it may not have.
"""

from __future__ import annotations

import asyncio
import json
import logging
import time
import xml.etree.ElementTree as ElementTree
from datetime import timedelta
from typing import Any
from zoneinfo import ZoneInfo

import aiohttp

from homeassistant.components import mqtt
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers.event import async_track_time_interval
from homeassistant.helpers.start import async_at_started
from homeassistant.util import dt as dt_util

from . import weather_warnings

_LOGGER = logging.getLogger(__name__)

# The Met Office updates its warnings as they change; 15 minutes keeps the
# CCUs well inside their 2-hour stale limit (kWarningsStaleAfterMs).
PUBLISH_INTERVAL = timedelta(minutes=15)
FETCH_TIMEOUT_SECONDS = 20.0

# The Met Office feed's times are UK local time.
_UK = ZoneInfo("Europe/London")


class WarningsPublisher:
    """Keeps the retained warnings topic up to date."""

    def __init__(
        self,
        hass: HomeAssistant,
        base_topic: str,
        met_office_region: str | None,
        flood_point: tuple[float, float, float] | None,
    ) -> None:
        self._hass = hass
        self._base_topic = base_topic
        self._region = met_office_region
        self._flood_point = flood_point

    @callback
    def async_start(self) -> None:
        """Publish once HA has started, then every 15 minutes."""

        async def _started(_hass: HomeAssistant) -> None:
            await self.async_publish()

        async_at_started(self._hass, _started)
        async_track_time_interval(self._hass, self._async_tick, PUBLISH_INTERVAL)

    async def _async_tick(self, _now: Any) -> None:
        await self.async_publish()

    async def _get(self, url: str) -> str | None:
        session = async_get_clientsession(self._hass)
        try:
            async with asyncio.timeout(FETCH_TIMEOUT_SECONDS):
                async with session.get(url) as response:
                    if response.status != 200:
                        _LOGGER.warning("MerlinCCU warnings: %s returned HTTP %s", url,
                                        response.status)
                        return None
                    return await response.text()
        except (aiohttp.ClientError, TimeoutError) as err:
            _LOGGER.warning("MerlinCCU warnings: %s failed: %s", url, err)
            return None

    async def async_publish(self) -> None:
        """Fetch every configured source and publish the warnings in force."""
        found: list[weather_warnings.OfficialWarning] = []
        if self._region:
            text = await self._get(weather_warnings.met_office_url(self._region))
            if text is None:
                return
            try:
                found += weather_warnings.parse_met_office(text, dt_util.now(_UK))
            except ElementTree.ParseError as err:
                _LOGGER.warning("MerlinCCU warnings: Met Office feed unreadable: %s", err)
                return
        if self._flood_point:
            text = await self._get(weather_warnings.flood_url(*self._flood_point))
            if text is None:
                return
            try:
                data = json.loads(text)
                found += weather_warnings.parse_floods(data if isinstance(data, dict) else {})
            except ValueError as err:
                _LOGGER.warning("MerlinCCU warnings: flood data unreadable: %s", err)
                return

        if not await mqtt.async_wait_for_mqtt_client(self._hass):
            _LOGGER.warning("MerlinCCU warnings: MQTT is not available")
            return
        await mqtt.async_publish(
            self._hass,
            weather_warnings.topic(self._base_topic),
            weather_warnings.payload(found, int(time.time())),
            qos=0,
            retain=True,
        )
        _LOGGER.debug("MerlinCCU warnings: %d in force", len(found))
