# Web Access and Secrets

Who can use a CCU's web pages, how secrets are kept, and what the protection
is and is not good for (#158).

## Keep the CCU on the Home Network

The web server is plain HTTP on port 80, meant for the home LAN. **Never
port-forward a CCU to the internet** or put it on a guest network. Anything
on the same network can reach it; the PIN below is the only thing between
that network and the settings.

## Web PIN

Optional, set on the config page under **Remote Access**: 4-32 characters.

- With no PIN, anyone on the network can open every page and change
  settings.
- With a PIN, every page and action asks for it: config, firmware page,
  display preview, the virtual keypad, Pinter activity. The exceptions are
  the login page, `/ping` and the read-only `GET /api/firmware`.
- Logging in sets a session cookie (`merlin_session`, `HttpOnly`,
  `SameSite=Strict`) that lasts 12 hours. Up to four browsers can be logged
  in; a fifth replaces the oldest. **Log out** is on the config page.
- Five wrong PINs in a row lock logins for 60 seconds.
- Changing or removing the PIN needs the current PIN as well as a login,
  and ends every session.
- The firmware upload still uses its own token (`X-OTA-Token`, see
  docs/ota.md), so `tools/ota_update.py` works with or without a PIN.

### Forgotten PIN

Clear it on the unit itself: **Settings > Device Identity > WEB PIN [Set]**,
then **CLEAR PIN**. This needs someone at the panel; the web keypad needs
the PIN, so it cannot be used to get round it.

### What it protects against

The PIN stops someone on the network from casually changing settings or
reading the panel. It does not stop someone who can watch the network
traffic: HTTP is unencrypted, so the PIN and the session cookie cross the
network in the clear. Treat it as a lock on a door, not a safe.

The PIN is stored as SHA-256 of a random 16-byte salt and the PIN, never
the PIN itself. A short PIN could still be guessed offline from a copy of
the flash, which needs the unit opened.

## Secrets Are Write-Only

The Wi-Fi password, Home Assistant token, MQTT password, share-feed token
and web PIN are never sent back to the browser. Their boxes are always
empty ("Leave blank to keep current"); leaving one blank keeps the saved
value. The panel shows only whether each is set.

## Change Record

Every settings save records when and from where, for example
`21:45 web 192.168.1.23` or `21:47 panel`. It shows:

- as **SETTINGS** on the panel's Status > Overview page;
- in Home Assistant as the **Last settings change** MQTT sensor;
- in the serial log (`Settings changed: …`).

Only the last change is kept, in RAM, so a restart clears it.
