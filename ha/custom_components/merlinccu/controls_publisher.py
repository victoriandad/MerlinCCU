"""Serves each MerlinCCU's Controls page over MQTT (#152).

Publishes every configured unit's list, retained, once HA has started and
again whenever an entity on it changes state, so a light switched from a
phone shows on the panel within a second. Answers each key press by calling
the entity's service and publishing the result. Payloads: controls.py.
"""

from __future__ import annotations

import asyncio
import logging

from homeassistant.components import mqtt
from homeassistant.core import Event, HomeAssistant, callback
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.event import async_track_state_change_event
from homeassistant.helpers.start import async_at_started

from . import controls

_LOGGER = logging.getLogger(__name__)

# The CCU counts a press as failed after 5 s (kReplyTimeoutMs in
# include/network/home_controls.h), so HA answers before that.
SERVICE_TIMEOUT_SECONDS = 4.0


class ControlsPublisher:
    """Keeps each unit's retained list current and carries out its presses."""

    def __init__(
        self,
        hass: HomeAssistant,
        base_topic: str,
        units: dict[str, list[tuple[str, str | None]]],
    ) -> None:
        self._hass = hass
        self._base_topic = base_topic
        # mac -> [(entity_id, configured name or None)]
        self._units = units
        # mac -> the list last published, so a press can be matched to it.
        self._published: dict[str, list[controls.Control]] = {}

    @callback
    def async_start(self) -> None:
        """Publish once HA has started, then on changes; listen for presses."""

        async def _started(_hass: HomeAssistant) -> None:
            await self._async_subscribe()
            for mac in self._units:
                await self._async_publish(mac)

        async_at_started(self._hass, _started)
        entity_ids = sorted({entity for unit in self._units.values() for entity, _ in unit})
        async_track_state_change_event(self._hass, entity_ids, self._async_changed)

    def _controls(self, mac: str) -> list[controls.Control]:
        """The unit's list with names: configured, else HA's friendly name."""
        result = []
        for entity_id, name in self._units[mac]:
            state = self._hass.states.get(entity_id)
            friendly = state.attributes.get("friendly_name") if state else None
            result.append(
                controls.Control(entity_id, controls.clean_name(name or friendly or entity_id))
            )
        return result

    async def _async_publish(self, mac: str) -> None:
        if not await mqtt.async_wait_for_mqtt_client(self._hass):
            _LOGGER.warning("MerlinCCU controls: MQTT is not available")
            return
        items = self._controls(mac)
        states = {}
        for item in items:
            state = self._hass.states.get(item.entity_id)
            states[item.entity_id] = state.state if state else None
        try:
            payload = controls.list_payload(items, states)
        except ValueError as err:
            _LOGGER.error("MerlinCCU controls for %s not sent: %s", mac, err)
            return
        self._published[mac] = items
        await mqtt.async_publish(
            self._hass,
            controls.topic(self._base_topic, mac, controls.LIST),
            payload,
            qos=0,
            retain=True,
        )

    async def _async_changed(self, event: Event) -> None:
        entity_id = event.data.get("entity_id")
        for mac, unit in self._units.items():
            if any(entity == entity_id for entity, _ in unit):
                await self._async_publish(mac)

    async def _async_subscribe(self) -> None:
        if not await mqtt.async_wait_for_mqtt_client(self._hass):
            _LOGGER.warning("MerlinCCU controls: MQTT is not available")
            return
        await mqtt.async_subscribe(
            self._hass,
            f"{self._base_topic}/+/controls/{controls.PRESS}",
            self._async_press,
            qos=0,
        )

    async def _async_press(self, message: mqtt.ReceiveMessage) -> None:
        mac = controls.mac_from_topic(self._base_topic, message.topic)
        payload = message.payload
        if isinstance(payload, bytes):
            payload = payload.decode("utf-8", "replace")
        press = controls.parse_press(payload)
        if mac is None or press is None:
            _LOGGER.debug("MerlinCCU controls: ignored %s %r", message.topic, payload)
            return
        list_id, index = press
        items = self._published.get(mac)
        if items is None or list_id != controls.list_id(items) or index >= len(items):
            await self._async_result(mac, index, False, "list changed")
            return

        item = items[index]
        domain, service = controls.service_for(item)
        try:
            async with asyncio.timeout(SERVICE_TIMEOUT_SECONDS):
                await self._hass.services.async_call(
                    domain, service, {"entity_id": item.entity_id}, blocking=True
                )
        except (HomeAssistantError, TimeoutError) as err:
            _LOGGER.warning("MerlinCCU %s: %s.%s %s failed: %s", mac, domain, service,
                            item.entity_id, err)
            await self._async_result(mac, index, False, str(err) or "timed out")
            return
        await self._async_result(mac, index, True)

    async def _async_result(self, mac: str, index: int, ok: bool, error: str | None = None) -> None:
        await mqtt.async_publish(
            self._hass,
            controls.topic(self._base_topic, mac, controls.RESULT),
            controls.result_payload(index, ok, error),
            qos=0,
            retain=False,
        )
