# Patched lwIP MQTT client

`mqtt.c` is lwIP's MQTT client as shipped in Pico SDK 2.2.0
(`lib/lwip/src/apps/mqtt/mqtt.c`). The firmware builds it in place of the
SDK's `pico_lwip_mqtt` library. Its licence is lwIP's BSD licence, kept in
the file header.

## The changes

There are two changes, each marked `MerlinCCU patch` in
`mqtt_parse_incoming()`. Both were found when Home Assistant's hourly
forecast grew in #202.

### Topics split between segments

Upstream passes on the first part of a message as soon as one TCP segment
ends. A PUBLISH whose topic is split between two segments therefore fails
with `Received short PUBLISH packet (topic)`, and the client drops the
connection.

This happens when the broker sends its retained messages in one burst on
subscribe. Whether a topic lands across a segment boundary depends only on
the sizes of those messages, so the same split recurs on every reconnect.
The client can then never stay connected.

The patch keeps the first part of a message in `rx_buffer` until either:

- `MQTT_VAR_HEADER_BUFFER_LEN` bytes have arrived, or
- the message is complete.

Only then is it passed on. That is how upstream already treats every part
after the first.

### Parts of a message spread over several pbufs

Upstream hands on each later part straight from the received pbufs, one
pbuf at a time, without copying it. To stay within one pbuf it compares the
read offset against the length of the first pbuf in the chain. The offset,
however, counts across the whole chain.

- When the offset equals that length, nothing is read and the loop never
  advances.
- Past that length, the subtraction wraps.

Because lwIP runs this in an interrupt, the whole CCU froze. This happens
when a large message arrives in a chain of several pbufs.

The patch measures against the pbuf that actually holds the offset.

## Updating the SDK

When updating the SDK, diff the new `mqtt.c` against this one. Carry the
patch forward, or drop this copy if upstream has fixed the bug.
