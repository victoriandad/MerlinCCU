# Network Trust

What each CCU connection trusts, what it protects, and what it does not
(#17). The CCU is a LAN appliance: everything it talks to, apart from
Open-Meteo and NTP, is on the house network, and the design assumes that
network is not hostile. Never port-forward a CCU or expose its broker.

## Connections

| Connection | Transport | Authenticates the other end? | Secrets sent | Notes |
| --- | --- | --- | --- | --- |
| Home Assistant REST (`home_assistant_manager.cpp`) | `http://` (default) or `https://` | Over `https://`, by pinned certificate (#17) | HA long-lived token, in every request | Over `https://` the certificate must hash to the SHA-256 fingerprint saved in the web config, or the handshake fails before the token is sent; with no pin, `https://` is refused. The serial log prints the fingerprint HA presents. No CA store and no clock are needed. |
| MQTT to Mosquitto (`mqtt_manager.cpp`) | Plain TCP, port 1883 | No | The unit's MQTT username and password, at connect | Each unit has its own login (docs/home-assistant-setup.md, step 4), so one leaked password can be revoked alone. MQTT over TLS is not supported. |
| Share feed (`share_price_manager.cpp`) | Plain TCP to HA | No | The feed token | `https://` is refused: the feed lives beside HA on the LAN. |
| Open-Meteo (`home_assistant_manager.cpp`) | Plain HTTP, port 80 | No | None | Only weather; a forged reply can show wrong weather, nothing else. Most units take weather from HA over MQTT instead (#153). |
| NTP (`wifi_manager.cpp`) | SNTP to `pool.ntp.org` | No | None | A forged time shifts the clock, data ages and calendar views. No TLS check depends on the clock today. |
| DNS | The resolver DHCP hands out | No | None | Names (`ha.lan`, the broker) are only as trustworthy as the router. A fixed IP avoids DNS entirely. |
| Web config and preview (`web_config_server.cpp`) | Plain HTTP, port 80 | Optional web PIN (docs/web-access.md) | The PIN at login; the OTA token on uploads | No TLS: a LAN sniffer can see the PIN, session cookie and OTA token. |
| Browser-side lookups (Location card) | HTTPS, from the browser | The browser's own checks | None | Postcode and place searches run in the browser, not on the CCU. |

## Policy

- **Credentials:** every secret the CCU holds (HA token, MQTT password, feed
  token, OTA token, web PIN hash) is scoped to that unit where the service
  allows it, so a lost unit is dealt with by revoking its credentials in HA
  and Mosquitto.
- **Plain HTTP and MQTT on the LAN are accepted.** They keep the firmware
  small and avoid a certificate story on an appliance with no user-facing
  trust store. The cost is that anyone on the LAN can read the traffic.
- **`https://` to HA is pinned.** The certificate HA presents must match the
  fingerprint in the settings (web config, Home Assistant card). This suits
  HA's usual self-signed certificate and survives a wrong clock. Replacing
  HA's certificate means saving the new fingerprint; until then the CCU
  refuses HA and raises the HOME ASSISTANT alert.
- **No internet-facing TLS.** The firmware makes no HTTPS request to the
  internet; anything that would (Met Office, #192) goes through HA, which
  validates certificates properly.

## Open work

None for HA. MQTT over TLS and an HTTPS web UI stay out of scope (see Policy).
