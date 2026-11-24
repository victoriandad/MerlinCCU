# Home Assistant Setup

How to set up Home Assistant (HA) from a fresh install so every MerlinCCU
feature that depends on it works. Everything HA needs that isn't built into
HA is kept in [`ha/`](../ha) in this repository.

The CCU settings named below are on the CCU's web config page
(`http://<ccu-address>/config`).

## What the CCU uses HA for

| Feature | HA side | CCU settings |
| --- | --- | --- |
| HA connection | Long-lived access token | Home Assistant: Host, Port, Access token |
| Tracked entity on the Status page | Any entity, such as a smart plug `switch.*` | Home Assistant: Tracked entity |
| CCU shown in HA over REST | Nothing; the CCU creates the entity itself | Home Assistant: Self entity |
| CCU sensors and controls in HA over MQTT | Mosquitto broker and the MQTT integration | MQTT Discovery |
| Weather, when the source is Home Assistant | The MerlinCCU integration in `ha/`, publishing a `weather.*` entity over MQTT | Weather Source, MQTT Discovery |
| Share prices | The MerlinCCU integration in `ha/` | Watched Shares, Share Price Feed |

The HA connection, tracked entity and self entity work with either weather
source. HA weather needs the CCU's **Weather source** set to **Home
Assistant (MQTT)** and MQTT set up (step 4); it doesn't use the HA
connection or token.

## 1. Find HA's address and port

The CCU needs HA's host name or IP address and the port HA answers on.

- A standard HA install answers on port **8123**, e.g. `http://homeassistant.local:8123`.
- An install behind a reverse proxy, or set up to answer on port 80, uses
  port **80**. For example, the current home install is `ha.lan` on port 80.

Check from any computer on the LAN: `http://<host>:<port>/` should show the
HA login page.

On the CCU, enter a bare host (`ha.lan`) with the port in the Port field. You
can also enter a URL: `http://` means port 80 and `https://` port 443, unless
it ends in `:port`.

## 2. Create the access token

1. In HA, open your user profile (bottom left), then the **Security** tab.
2. Under **Long-lived access tokens**, choose **Create token**, name it after
   the CCU (e.g. `MerlinCCU desk`), and copy the token. HA shows it only once.
3. On the CCU, tick **Enable REST integration**, then enter Host, Port and the
   token under Access token and save.

Check: `curl -H "Authorization: Bearer <token>" http://<host>:<port>/api/`
returns `{"message":"API running."}`.

## 3. Entities

- **Tracked entity**: the entity the CCU shows on its Status page, such as a
  smart plug (`switch.sean_s_desk_top_room`). Its device must be added to HA
  first; copy the entity ID from *Settings > Devices & services > Entities*.
- **Self entity**: one per CCU, such as `sensor.merlin_ground_status`. A
  factory-fresh CCU fills in its own, e.g. `sensor.merlin_3a4b5c_status`. The
  CCU creates it with its first update, so HA reports it as missing until then.
- **Weather entity** (only for weather source Home Assistant): set in HA, not
  on the CCU (step 6).

## 4. MQTT

The CCU publishes its status, Wi-Fi, IP address and HA connection state as HA
sensors, set up by MQTT discovery.

1. Install the **Mosquitto broker** add-on (*Settings > Apps*, called *Add-ons* in older HA;
   install it from the store there), start it, and turn on **Start on boot**.
2. Give each CCU its own MQTT login. Either create an HA user for it
   (*Settings > People > Users*, e.g. `merlinccu-desk`; Mosquitto accepts HA
   users), or add it under `logins` in the add-on's configuration:

   ```yaml
   logins:
     - username: merlinccu-desk
       password: <a long random password>
   ```

3. Add the **MQTT** integration (*Settings > Devices & services*). HA offers it
   as soon as the add-on is running.
4. On the CCU, tick **Enable MQTT** and set:
   - Broker host: HA's host, as in step 1. Port: **1883**. Mosquitto listens
     there even when HA's web interface is on port 80.
   - Username and Password: the login from step 2.
   - Discovery prefix: `homeassistant`. Base topic: `merlinccu`. These are
     the defaults.

Check: a device named after the CCU's display label appears under the MQTT
integration with sensors
for Status, Wi-Fi state, IP address, Home Assistant, Home Assistant HTTP,
Tracked entity, Firmware (the build's git commit), Firmware update
(`confirmed`, `on_trial`, `rolled_back` or `not_partitioned`; see
docs/ota.md), Last settings change (see docs/web-access.md), and the
long-run health diagnostics (#155): Last reset (`power_on`, `reset_pin`,
`brownout`, `restart`, `update`, `rollback`, `watchdog`, `cpu_fault`,
`stack_overflow` or `unknown`), Uptime (minutes), Stack free and Heap arena
(bytes, both worst case since boot). A Heap arena that keeps rising over
days points at a leak.

### Seeing the panels on a dashboard

Each CCU also has a **Screen** image entity (#159): a PNG of what its panel
shows, in the panel's amber on dark green. The CCU publishes it to
`<base>/<mac>/screen/state` (retained) when the screen changes, at most
every 5 seconds. HA receives it over MQTT, so a dashboard never polls the
CCU's web server and the keys stay responsive.

To show every unit side by side, add a card like this (one `picture-entity`
per CCU; the entity IDs follow the display labels):

```yaml
type: grid
columns: 3
square: false
cards:
  - type: picture-entity
    entity: image.ground_floor_screen
    show_state: false
  - type: picture-entity
    entity: image.first_floor_screen
    show_state: false
  - type: picture-entity
    entity: image.loft_screen
    show_state: false
```

Notes:

- Most pages are 0.5 to 2 KB. A page too big for the CCU's 3.5 KB message
  limit (a heavily dithered one) is not sent, the serial log says
  `MQTT screen image too large`, and HA keeps the previous picture.
- A browser can fetch the same PNG from `http://<unit>/api/framebuffer.png`.
  With a web PIN set (docs/web-access.md) that needs a login, so HA should
  use the MQTT entity.

### Controlling the CCU from HA

The same device also gets entities HA can change (#150). Each one listens on
`<base>/<mac>/<slug>/set` and reports back on `<base>/<mac>/<slug>/state`,
so HA shows what the panel really does, including changes made at the
panel.

| Entity | Type | Values |
| --- | --- | --- |
| ALRT lamp, TEST lamp | select | `auto` (the CCU decides), `off`, `on`, `flash_slow`, `flash_fast` |
| Panel brightness, Key backlight | select | `off`, `low`, `medium`, `high` (as BRT/DIM) |
| Display | select | `awake`, `screen_saver` |
| Page | select | `home`, `weather`, `calendar`, `status`, `alerts`, `shares`, `pinter`, `local_conditions` |
| Message | notify | text for the alert list; empty text clears it |
| Message state | sensor | `none`, `active`, `acknowledged` (accepted on the panel) |

A message can also carry a title (up to 20 characters, shown in the alert
list) and a severity (`message`, `warning` or `alert`) by publishing JSON
to the message topic:

```yaml
action: mqtt.publish
data:
  topic: merlinccu/<mac>/message/set
  payload: '{"title":"Washing","message":"The machine has finished","severity":"warning"}'
```

Plain text from the Message entity's `notify.send_message` action uses the
title `HA MESSAGE`. An automation can watch Message state for
`acknowledged` to know someone accepted it at the panel. Unknown or
over-long commands are refused and logged on the CCU's serial console.

### Keys that trigger HA automations

Any panel key can belong to HA instead (#151). List them by their legend
on the config page under MQTT Discovery > **HA keys**, e.g. `F1, T FUNC`.
Those keys then do nothing on the panel. Each gets a device trigger on the
CCU's device, so in the automation editor pick **Device**, the CCU, and
`"T FUNC" pressed`. No YAML is needed.

- One event per press; holding a key sends one.
- A press while MQTT is down is dropped, not sent later.
- The topic is `<base>/<mac>/key/<slug>` with payload `press` (not
  retained); the slug is the legend in lower case, e.g. `t_func`.
- Taking a key off the list removes its trigger from HA.
- The web preview's keypad works the same way, which helps when testing.

## Several CCUs

Units can share one network, one HA and one Mosquitto broker. Give each one:

- its own **Device name** (e.g. `merlin-ground`, `merlin-first`). It is the
  unit's network name, so `http://merlin-ground/` reaches it;
- a **Display label** (e.g. `Ground floor`), shown on its Status page and as
  its device name in HA;
- its own **Self entity** and MQTT login.

A factory-fresh unit names itself from its board id (`merlin-3a4b5c`), so two
new units never clash. MQTT topics, client id and HA unique ids come from the
unit's Wi-Fi MAC address, so they are unique already and survive renaming:
HA keeps each unit's entities and history when its name or label changes.

## 5. Share price feed

The CCU never contacts a market-data provider itself
([share-feed-design.md](share-feed-design.md)). It asks HA for
`GET /api/merlinccu/shares?period=...`. The MerlinCCU integration in
[`ha/custom_components/merlinccu`](../ha/custom_components/merlinccu) answers
that request on HA's own port. It gets prices from Yahoo Finance's chart API,
which needs no API key.

1. Copy the `ha/custom_components/merlinccu` folder into HA's
   `/config/custom_components/` folder, so that
   `/config/custom_components/merlinccu/__init__.py` exists. The Samba share,
   SSH or the Studio Code Server add-ons all give access to `/config`.
2. Make a feed token. Any random string of up to 127 characters works:
   `python -c "import secrets; print(secrets.token_urlsafe(32))"`.
   Add it to `/config/secrets.yaml`:

   ```yaml
   merlinccu_feed_token: <the token>
   ```

3. Add the `merlinccu:` block from
   [`ha/configuration.example.yaml`](../ha/configuration.example.yaml) to
   `/config/configuration.yaml`. List the same symbols as the CCU's Watched
   Shares, as Yahoo symbols (LSE shares end in `.L`, e.g. `BA.L`).
4. Restart HA (*Settings > System > Restart*). HA's log shows
   `MerlinCCU share feed serving 1 share(s)`.
5. On the CCU, tick **Enable local share feed** and enter the feed token.

Check:
`curl -H "Authorization: Bearer <feed token>" "http://<host>:<port>/api/merlinccu/shares?period=today"`
returns `{"shares":[{"symbol":"BA.L",...,"data_state":"live",...}]}`.

How the feed behaves:

- **Caching.** Each share is cached per period: Today for 5 minutes, Week for
  30 minutes, Month and Year for 6 hours, All-time for a day. Only the period
  on screen is ever fetched.
- **Out of hours.** Until a share trades today (weekends, holidays, before
  the open), Today shows the last price with no change and a flat graph.
  Longer periods are unaffected.
- **Failures.** If Yahoo fails, the last good data is sent marked `stale`, or
  `error` if there is none. The CCU keeps showing what it had.
- **Token.** The feed checks its own token, not an HA token: HA's tokens are
  longer than the CCU's feed-token field holds.
- **Limitation.** Yahoo's chart API is free but unofficial, and could change.
  If it does, only `ha/custom_components/merlinccu/feed.py` needs updating;
  the CCU is unaffected.

To test the feed's data handling without HA, run
`python -m unittest discover -s ha/tests` from the repository root.

## 6. Weather over MQTT

HA fetches the weather once and publishes it to every CCU over MQTT, so no
CCU needs internet access for it (#153; payloads in
[mqtt-household-data.md](mqtt-household-data.md)). Needs step 4.

1. Copy `ha/custom_components/merlinccu` into HA as in step 5, if it isn't
   there already.
2. Add a `weather:` part to the `merlinccu:` block in
   `/config/configuration.yaml` (see
   [`ha/configuration.example.yaml`](../ha/configuration.example.yaml)):

   ```yaml
   merlinccu:
     weather:
       entity: weather.forecast_home
   ```

   Any `weather.*` entity with hourly and daily forecasts works. The Met.no
   integration, added by default during HA onboarding, provides
   `weather.forecast_home`. Sunrise and sunset come from `sun.sun`; set
   `sun:` to use another sun entity, and `base_topic:` if the CCUs don't use
   the default `merlinccu`. The `shares:` part is optional, so a weather-only
   block is fine.
3. Restart HA. HA's log shows
   `MerlinCCU weather from weather.forecast_home published under merlinccu/house/weather`.
4. On each CCU, set **Weather source** to **Home Assistant (MQTT)**.

Check: in HA, *Settings > Devices & services > MQTT > Configure > Listen to a
topic*, listen to `merlinccu/house/weather/#`. Three retained messages
(`now`, `hourly`, `daily`) arrive at once, and again every 5 minutes. The
CCU's Weather page shows the forecast, credited to the provider (e.g.
MET Norway).

- While the weather entity is unavailable (e.g. HA has no internet), HA
  publishes nothing. The CCUs keep the last forecast, show its age, and mark
  it stale after 20 minutes.
- A CCU can still fetch Open-Meteo itself instead: set its **Weather
  source** to **Open-Meteo** and give it a location.

## 7. Controls page

Each CCU's **Controls** page (Home, R4) switches lights, switches, fans and
input booleans, and runs scenes and scripts (#152). Each unit has its own
list, kept in HA, so the ground floor and the loft can offer different
things and a list changes without touching the CCU. Needs step 4 and the
integration from step 5.

1. Find each unit's MQTT name: open `http://<unit>/api/firmware` and copy
   the `mqtt_unit=` value (12 hex digits, its Wi-Fi MAC).
2. Add a `controls:` part to the `merlinccu:` block:

   ```yaml
   merlinccu:
     controls:
       units:
         "2ccf67a1b2c3":          # ground floor
           - light.hall
           - light.kitchen_spots
           - entity: scene.evening
             name: Evening        # optional; defaults to HA's name
           - script.goodnight
         "2ccf67d4e5f6":          # loft
           - switch.loft_heater
   ```

   Quote each MAC, so YAML never reads one as a number. Up to 24 entities per
   unit, eight to a page. Lights, switches, fans and
   `input_boolean`s toggle; scenes and scripts run. HA refuses other domains
   when it loads the YAML. Names are cut to 20 characters. Set `base_topic:`
   beside `units:` if the CCUs don't use the default `merlinccu`.
3. Restart HA. HA's log shows `MerlinCCU controls published for 2 unit(s)`.

Check: open Controls on the CCU. Each key shows the entity's name and state;
switch a light from your phone and the key follows within a second. Press a
key: it shows `[Switching]` until HA answers. If HA refuses, or does not
answer within 5 seconds, the page says so for 10 seconds and the serial log
says why.

- With MQTT down the page has no keys and says so.
- `NO CONTROLS` means the unit has no list: check its `mqtt_unit` against
  the YAML.

## Fresh-install checklist

- [ ] HA reachable at `http://<host>:<port>/`; host and port noted (step 1)
- [ ] Long-lived token created; `/api/` answers with it (step 2)
- [ ] CCU Home Assistant settings saved; the CCU's Home Assistant status shows
      connected
- [ ] Tracked entity's device added to HA and its entity ID entered on the CCU
      (step 3)
- [ ] Weather source Home Assistant only: `merlinccu:` `weather:` block added,
      HA restarted, `merlinccu/house/weather/#` shows three messages (step 6)
- [ ] Mosquitto add-on running and set to start on boot; MQTT integration
      added; CCU login made (step 4)
- [ ] CCU MQTT settings saved; MerlinCCU device and sensors appear in HA
- [ ] `ha/custom_components/merlinccu` copied; `merlinccu:` block and
      `merlinccu_feed_token` added; HA restarted (step 5)
- [ ] Feed `curl` returns live data; CCU share feed enabled with the token; the
      Shares page shows live prices instead of demo data
- [ ] Controls: each unit's `mqtt_unit` listed under `controls: units:`; HA
      restarted; the CCU's Controls page shows its keys (step 7)

## Troubleshooting

| Symptom | Likely cause |
| --- | --- |
| CCU Home Assistant state `unauthorized` | Token wrong or deleted in HA; make a new one (step 2) |
| CCU can't connect to HA | Wrong port. HA on port 80 needs Port 80, or an `http://` host |
| Status page tracked entity blank or `?` | Entity ID wrong, or its device isn't in HA yet; HA answers 404 |
| No MerlinCCU device in HA | MQTT login refused, or the MQTT integration isn't added; check the Mosquitto add-on log |
| Shares stay on demo data | Feed disabled on the CCU, token mismatch (HA answers 401), or the symbol differs from the `merlinccu:` list |
| Weather page says `Set up MQTT in web config` | Weather source is Home Assistant but MQTT is off on the CCU (step 4) |
| Weather page stays on `WAITING FOR WEATHER` | Nothing on `merlinccu/house/weather/#`: the `weather:` block is missing, HA wasn't restarted, or the base topic differs from the CCU's |
| Weather footer shows `STALE` | HA has stopped publishing: its weather entity is unavailable, or HA or MQTT is down |
| Feed returns `data_state: error` | Yahoo doesn't know the symbol, or HA can't reach the internet; HA's log names the symbol |
