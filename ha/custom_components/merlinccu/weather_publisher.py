"""Publishes the house weather to every MerlinCCU over MQTT (#153).

Reads one HA weather entity (and the sun entity), builds the payloads in
house_weather.py and publishes them retained, so a CCU gets the latest
weather as soon as it subscribes. Republished every few minutes, so each
payload's timestamp shows the CCU how fresh the data is, and straight away
when the weather or sun entity changes.
"""

from __future__ import annotations

import logging
import time
from datetime import timedelta
from typing import Any

from homeassistant.components import mqtt
from homeassistant.core import Event, HomeAssistant, callback
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.event import (
    async_track_state_change_event,
    async_track_time_interval,
)
from homeassistant.helpers.start import async_at_started

from . import house_weather

_LOGGER = logging.getLogger(__name__)

# Matches the CCU's own refresh (kRefreshIntervalMs in
# home_assistant_manager.h); the CCU marks data stale after four missed ones.
PUBLISH_INTERVAL = timedelta(minutes=5)


class WeatherPublisher:
    """Keeps the retained weather topics up to date."""

    def __init__(
        self, hass: HomeAssistant, weather_entity: str, sun_entity: str, base_topic: str
    ) -> None:
        self._hass = hass
        self._weather_entity = weather_entity
        self._sun_entity = sun_entity
        self._base_topic = base_topic
        self._running = False

    @callback
    def async_start(self) -> None:
        """Publish once HA has started, then on a timer and on changes."""

        async def _started(_hass: HomeAssistant) -> None:
            await self.async_publish()

        async_at_started(self._hass, _started)
        async_track_time_interval(self._hass, self._async_tick, PUBLISH_INTERVAL)
        async_track_state_change_event(
            self._hass, [self._weather_entity, self._sun_entity], self._async_changed
        )

    async def _async_tick(self, _now: Any) -> None:
        await self.async_publish()

    async def _async_changed(self, _event: Event) -> None:
        await self.async_publish()

    async def _forecast(self, kind: str) -> list[dict[str, Any]] | None:
        try:
            response = await self._hass.services.async_call(
                "weather",
                "get_forecasts",
                {"entity_id": self._weather_entity, "type": kind},
                blocking=True,
                return_response=True,
            )
        except HomeAssistantError as err:
            _LOGGER.debug("No %s forecast from %s: %s", kind, self._weather_entity, err)
            return None
        return (response or {}).get(self._weather_entity, {}).get("forecast")

    async def async_publish(self) -> None:
        """Build and publish all three payloads.

        Nothing is published while the weather entity has no data (e.g. the
        provider is unreachable), so the CCUs keep the last retained data and
        its timestamp shows them it is getting old.
        """
        if self._running:
            return
        self._running = True
        try:
            await self._async_publish()
        finally:
            self._running = False

    async def _async_publish(self) -> None:
        weather = self._hass.states.get(self._weather_entity)
        if weather is None:
            _LOGGER.warning("MerlinCCU weather entity %s not found", self._weather_entity)
            return
        sun = self._hass.states.get(self._sun_entity)
        timestamp = int(time.time())
        now = house_weather.now_payload(
            weather.state, dict(weather.attributes), dict(sun.attributes) if sun else None,
            timestamp,
        )
        if now is None:
            _LOGGER.debug("MerlinCCU weather: %s has no data", self._weather_entity)
            return

        payloads = {house_weather.NOW: now}
        for kind in (house_weather.HOURLY, house_weather.DAILY):
            text = house_weather.forecast_payload(
                kind, await self._forecast(kind), dict(weather.attributes), timestamp
            )
            if text is not None:
                payloads[kind] = text

        if not await mqtt.async_wait_for_mqtt_client(self._hass):
            _LOGGER.warning("MerlinCCU weather: MQTT is not available")
            return
        # "now" goes first: on the CCU it resets the weather warnings, which
        # the forecasts then add back.
        for name in (house_weather.NOW, house_weather.HOURLY, house_weather.DAILY):
            if name in payloads:
                await mqtt.async_publish(
                    self._hass,
                    house_weather.topic(self._base_topic, name),
                    payloads[name],
                    qos=0,
                    retain=True,
                )
