# Local Share Price Feed (issue #42)

**Status (2026-08-13):** implemented. `share_price_manager.cpp` fetches from
the endpoint contract below whenever `RuntimeConfig::shares_feed_enabled` is
on and a Home Assistant host is configured; demo data is shown otherwise (or
before the first successful fetch). This document is the setup/contract
reference the issue asked for, not a pre-implementation proposal.

## Why not a direct third-party provider

Direct Yahoo Finance chart fetching (the original implementation) caused or
contributed to a share-detail page lockup (issue #19) and was disabled. Google
has no supported Pico-friendly Finance REST API. Scraping Google Finance would
be brittle and would move more provider-specific parsing onto the Pico than a
small embedded device should own.

Per the existing Decision Log entry (2026-07-04): the Pico should not own
third-party market API keys, OAuth flows, large provider JSON parsing, or
provider-specific retry rules. A local feed -- Home Assistant itself, or a
small proxy sitting on the same box -- absorbs all of that, and the actual
upstream provider behind it can change without any MerlinCCU firmware change.

## The endpoint contract

MerlinCCU calls one small local endpoint, reusing the configured Home
Assistant host/port (`RuntimeConfig::home_assistant_host`/
`home_assistant_port` -- there is no separate shares-feed host field):

```http
GET /api/merlinccu/shares?period=today|week|month|year|all_time HTTP/1.1
Host: <home_assistant_host>
Authorization: Bearer <shares_feed_token, if configured>
```

Response shape:

```json
{
  "shares": [
    {
      "symbol": "BA.L",
      "name": "BAE SYSTEMS",
      "exchange": "LSE",
      "currency": "GBX",
      "price": "1372.0",
      "change": "+0.02%",
      "data_state": "live",
      "history": [1362, 1364, 1361, 1372]
    }
  ]
}
```

Field notes (matching `share_price_manager.cpp`'s parser exactly):

- `symbol` is required and is the join key -- MerlinCCU only updates watched
  shares (configured via issue #9's watchlist) whose `symbol` matches an
  entry the feed returns. Symbols the feed doesn't mention keep showing
  whatever they last displayed rather than being blanked.
- `price` is accepted as either a raw JSON number or a quoted numeric string
  (as shown above) -- MerlinCCU reformats it through its own thousands-
  separator style, so send a plain decimal either way.
- `change` is copied through **verbatim** as display text -- send it already
  formatted the way it should appear (e.g. `"+0.02%"`, `"-1.4%"`), not a raw
  number. This is the one field MerlinCCU does not reformat.
- `data_state`, if present and not `"live"` (e.g. `"stale"`, `"error"`), tells
  MerlinCCU to skip updating that share this cycle -- treated the same as the
  symbol being absent from the response. Omit the field entirely if your feed
  has no concept of per-share staleness; absence is treated as `"live"`.
- `name`/`exchange`/`currency` are optional; if omitted, MerlinCCU keeps
  whatever was already configured/displayed for that symbol.
- `history` is optional and can be any length -- MerlinCCU downsamples it to
  its fixed 24-point graph buffer. Omit it (or send an empty array) to leave
  the existing graph in place.
- Unknown response fields are ignored. A response with `"shares": []`, a
  missing `"shares"` key entirely, or a symbol not on the watchlist is not an
  error at the HTTP level -- MerlinCCU treats "nothing usable in this
  response" as a soft failure and retries on its normal schedule, without
  blocking navigation or freezing the UI (issue #19's original failure mode).

## Setting it up in Home Assistant

The MerlinCCU Home Assistant integration in
[`ha/custom_components/merlinccu`](../ha/custom_components/merlinccu) serves
this endpoint on HA's own port. It gets prices from Yahoo Finance's chart
API, which needs no API key, and caches each period. Setup steps are in
[home-assistant-setup.md](home-assistant-setup.md), step 5.

Anything else that answers the same request on the configured host and port
works too. For example, a small proxy in front of a different provider such
as Alpha Vantage or Twelve Data: the CCU only ever sees the contract above.

The bearer token (`RuntimeConfig::shares_feed_token`, sent as
`Authorization: Bearer <token>`) is deliberately **separate** from the Home
Assistant long-lived access token. HA's tokens are longer than the CCU's
127-character field, and the endpoint has its own auth story.

## Known limitations

- Only shares already in the configured watchlist (issue #9) are ever
  updated; a symbol the feed doesn't recognize (or a HA automation
  misconfigured against a different watchlist) will silently stay on demo
  data forever rather than surfacing an explicit "we don't know this symbol"
  state. Keep the HA-side automation's symbol list in sync with MerlinCCU's
  watchlist.
- Freshness (issue #16's `"LIVE"`/`"STALE Xm"`/`"NO DATA"` display) is
  tracked once per fetch cycle, not per share -- if the feed returns some but
  not all watched symbols, every configured share still shows the same
  whole-feed freshness state once live fetching is active, even the ones the
  feed hasn't actually reported on recently.
- The response buffer is 4KB (`kResponseBufferSize` in
  `share_price_manager.cpp`), sized for a compact multi-share JSON payload,
  not a paste of full provider chart data. A response that doesn't fit is
  treated as a partial success and parsed up to the truncation point.
