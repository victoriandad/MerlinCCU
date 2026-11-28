"""Records weather alerts accepted on any MerlinCCU and tells every panel (#210).

Listens on `<base>/house/acks/set`. A new id is stored (so it survives an HA
restart), published retained on `<base>/house/weather/acks` for every panel,
logged, and fired as a `merlinccu_weather_acknowledged` event for automations
and the logbook. Payloads: weather_acks.py.
"""

from __future__ import annotations

import logging

from homeassistant.components import mqtt
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.start import async_at_started
from homeassistant.helpers.storage import Store

from . import weather_acks

_LOGGER = logging.getLogger(__name__)

STORAGE_VERSION = 1
EVENT_ACKNOWLEDGED = "merlinccu_weather_acknowledged"


class WeatherAcksRelay:
    """Keeps the retained acks topic for one base topic."""

    def __init__(self, hass: HomeAssistant, base_topic: str) -> None:
        self._hass = hass
        self._base_topic = base_topic
        self._store: Store = Store(hass, STORAGE_VERSION, f"merlinccu_weather_acks_{base_topic}")
        self._acked: dict[str, str] = {}

    @callback
    def async_start(self) -> None:
        """Once HA has started: load the record, publish it, listen."""

        async def _started(_hass: HomeAssistant) -> None:
            self._acked = weather_acks.clean(await self._store.async_load())
            if not await mqtt.async_wait_for_mqtt_client(self._hass):
                _LOGGER.warning("MerlinCCU weather acks: MQTT is not available")
                return
            await self._async_publish()
            await mqtt.async_subscribe(
                self._hass,
                weather_acks.set_topic(self._base_topic),
                self._async_received,
                qos=0,
            )

        async_at_started(self._hass, _started)

    async def _async_publish(self) -> None:
        await mqtt.async_publish(
            self._hass,
            weather_acks.acks_topic(self._base_topic),
            weather_acks.acks_payload(self._acked),
            qos=0,
            retain=True,
        )

    async def _async_received(self, message: mqtt.ReceiveMessage) -> None:
        ack = weather_acks.parse_ack(message.payload)
        if ack is None:
            _LOGGER.debug("MerlinCCU weather acks: ignored %r", message.payload)
            return
        kind, ack_id, unit = ack
        if self._acked.get(kind) == ack_id:
            return
        self._acked[kind] = ack_id
        await self._store.async_save(self._acked)
        await self._async_publish()
        _LOGGER.info("MerlinCCU %s accepted %s alert %s for every unit", unit, kind, ack_id)
        self._hass.bus.async_fire(
            EVENT_ACKNOWLEDGED, {"unit": unit, "alert": kind, "id": ack_id}
        )
