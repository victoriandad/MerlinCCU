# Network Deployment

How the CCUs sit on the house network: their addresses and names, how they
are reached from away, and what watches them. The network side (router,
DNS, Tailscale, dashboard, monitoring) is set up outside this repo; this page
records what the firmware expects of it and offers to it. Real addresses and
MACs are kept out of this public repo.

## The units

| Name | Board | Image |
| --- | --- | --- |
| `merlinccu1` | Pico 2 W in the real CCU | `build/` |
| `merlinccu2` | Pico 2 W, development | `build/` |
| `merlinccu3` | Pimoroni Pico Plus 2 W | `build-plus2/` |

Each unit's name is its **Device name** setting (Settings > Device identity,
or the web page's Device Identity card). It is the name the unit gives DHCP
and answers to over NetBIOS, and it shows on the Home page footer. The MQTT
identity is the unit's MAC, so a rename never orphans its Home Assistant
entities (#149).

## Addresses and names

- **DHCP, with a reservation.** Each unit takes its address from the router
  by DHCP, and the router reserves that address for the unit's MAC. The
  firmware does not set DNS servers: it uses the ones DHCP gives it. (The
  optional static-address settings in `wifi_credentials.h` are for bench
  use, not the house.)
- **Names.** The house DNS (the Pi-holes) holds an A record for each unit,
  `merlinccu1.lan` to `merlinccu3.lan`, pointing straight at the reserved
  address. Use these full names in links and monitors: the router's search
  domain is not `lan`, so a bare `merlinccu2` resolves only where Windows
  NetBIOS answers it (the repo's tools rely on that from the laptop).
- **Finding a unit's MAC and name.** `GET /api/firmware` reports `unit=` and
  `mac=`; Status > Connect on the panel shows the IP address.

## Remote access: LAN and Tailscale only

The units serve plain HTTP and have no real authentication yet: the
optional **Web PIN** (#158) is sent unencrypted, and anyone who reaches the
web page can change settings. So:

- **Never expose a unit to the internet.** No router port-forwards, and no
  entry for the units in the public reverse proxy for the house domain.
- **From away, use Tailscale.** A tailnet subnet router advertises the home
  LAN, and the tailnet's DNS is the house Pi-holes, so
  `http://merlinccuN.lan/` works from a phone on Tailscale as it does at
  home, with the tunnel encrypting the plain HTTP.
- **Set a Web PIN** on every unit (docs/web-access.md).

docs/network-trust.md covers what each of the unit's own connections
trusts.

## What watches them

Each unit's web server handles **one request at a time** and answers
`503 Service Unavailable` (with `Retry-After: 1`) while busy. Monitors must
expect an occasional 503 and poll gently. Home Assistant gets each unit's
state over MQTT (docs/mqtt-household-data.md), not by polling HTTP.

| Watcher | What it asks | How often |
| --- | --- | --- |
| Dashboard status dot | ICMP ping | the dashboard's default; no HTTP load |
| Dashboard card data | `GET /api/status.json` | every 60 s or more, only while the dashboard is open |
| Uptime monitor | `GET /api/firmware`, keyword `wifi_connected=1` | every 120 s, 2 retries |

Together that is about one request every 30 to 40 s per unit at most. Both
endpoints are built from state already held, so they answer at once from
the network callback, never waiting on the display or a fetch; both are
public even with a Web PIN set (`web_auth::is_public_request`).

### `GET /api/status.json`

A small JSON object (`status_json.h`, under 640 bytes) for the dashboard's
`customapi` widget:

```json
{"unit":"merlinccu2","label":"Merlin CCU (Dev)","firmware":"cc3953d",
 "board":"pico2_w","uptime_s":3725,"wifi_connected":true,"rssi":-61,
 "ha_connected":true,"mqtt_connected":true,"free_heap":12288,
 "current_page":"HOME","alerts_active":1,"top_alert":"ENV SENSOR",
 "units_online":2,"units_known":2}
```

| Field | Meaning |
| --- | --- |
| `unit`, `label` | Network name and display label |
| `firmware`, `board` | Git commit and `PICO_BOARD` |
| `uptime_s` | Seconds since start-up |
| `wifi_connected`, `ha_connected`, `mqtt_connected` | Link states |
| `rssi` | Wi-Fi signal in dBm, sampled every 10 s; `null` before the first reading |
| `free_heap` | Heap bytes not in use now |
| `current_page` | The page's header title, or `SCREEN SAVER` |
| `alerts_active`, `top_alert` | Active alerts, and the most serious one's summary |
| `units_online`, `units_known` | Other CCUs on the broker (#210) |

### `GET /api/firmware`

Plain `key=value` lines for scripts and the uptime monitor: `unit=`, `mac=`,
`version=`, `board=`, `slot=`, `state=`, then health and memory figures
(docs/ota.md lists them all).

## Related

- #149: multi-unit identity
- #158: web access control (until then, LAN and Tailscale only)
- #210: units that see each other
- docs/ota.md: updating every unit over Wi-Fi
