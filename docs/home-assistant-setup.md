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
| Weather, when the source is Home Assistant | A `weather.*` entity, and `sun.sun` | Weather Source |
| CCU sensors in HA over MQTT | Mosquitto broker and the MQTT integration | MQTT Discovery |
| Share prices | The MerlinCCU integration in `ha/` | Watched Shares, Share Price Feed |

The HA connection, tracked entity and self entity work with either weather
source. Only HA weather needs the CCU's **Weather source** set to **Home
Assistant**.

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
- **Weather entity** (only for weather source Home Assistant): any `weather.*`
  entity that supports hourly and daily forecasts. The Met.no integration,
  added by default during HA onboarding, provides `weather.forecast_home`.
- **Sun entity**: `sun.sun`, built into HA.

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
docs/ota.md) and Last settings change (see docs/web-access.md).

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

## Fresh-install checklist

- [ ] HA reachable at `http://<host>:<port>/`; host and port noted (step 1)
- [ ] Long-lived token created; `/api/` answers with it (step 2)
- [ ] CCU Home Assistant settings saved; the CCU's Home Assistant status shows
      connected
- [ ] Tracked entity's device added to HA and its entity ID entered on the CCU
      (step 3)
- [ ] Weather source Home Assistant only: `weather.*` entity exists and is
      entered on the CCU
- [ ] Mosquitto add-on running and set to start on boot; MQTT integration
      added; CCU login made (step 4)
- [ ] CCU MQTT settings saved; MerlinCCU device and sensors appear in HA
- [ ] `ha/custom_components/merlinccu` copied; `merlinccu:` block and
      `merlinccu_feed_token` added; HA restarted (step 5)
- [ ] Feed `curl` returns live data; CCU share feed enabled with the token; the
      Shares page shows live prices instead of demo data

## Troubleshooting

| Symptom | Likely cause |
| --- | --- |
| CCU Home Assistant state `unauthorized` | Token wrong or deleted in HA; make a new one (step 2) |
| CCU can't connect to HA | Wrong port. HA on port 80 needs Port 80, or an `http://` host |
| Status page tracked entity blank or `?` | Entity ID wrong, or its device isn't in HA yet; HA answers 404 |
| No MerlinCCU device in HA | MQTT login refused, or the MQTT integration isn't added; check the Mosquitto add-on log |
| Shares stay on demo data | Feed disabled on the CCU, token mismatch (HA answers 401), or the symbol differs from the `merlinccu:` list |
| Feed returns `data_state: error` | Yahoo doesn't know the symbol, or HA can't reach the internet; HA's log names the symbol |
