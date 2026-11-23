# Soak Test

The CCUs run 24/7 as household fixtures, so each release candidate must
survive a week of normal use and induced outages without anyone touching it
(#155). This page is the procedure and the pass criteria.

## What the firmware does about outages

| Outage | What the unit does |
| --- | --- |
| Wi-Fi or router down | Cycles through its credentials, pausing 10 s between passes, doubling to 2 min. The header Wi-Fi icon shows it. |
| MQTT broker or HA restart | Tears the session down and reconnects after 5 s, doubling to 5 min, then republishes availability, discovery and every state. |
| HA REST down | Retries after 10 s, doubling to 5 min. |
| Open-Meteo or share feed down | Retries after 5 min (weather) or 1 min (shares), doubling to 30 min. |
| Main loop hangs | The watchdog restarts the unit; `LAST RESET` and HA's Last reset read `Watchdog`. |

Every back-off is cut by up to a quarter at random so several units do not
retry in step, and resets after a success. Data already on screen stays
there with its age, and goes `STALE` after its threshold (see
[architecture.md](architecture.md), Stale-Data Display Policy).

## Setup

1. Flash the candidate build to every unit under test (`tools/ota_update.py`).
2. Note each unit's `FIRMWARE` and `LAST RESET` on Status > Overview.
3. On a PC that stays on, start the monitor:

   ```bash
   python tools/soak_monitor.py --csv soak-<build>.csv merlinccu merlin-loft
   ```

   It polls each unit's `/api/firmware` once a minute, appends every reading
   to the CSV, and prints a line when a unit stops answering, a link drops
   or recovers (with how long it took), a unit restarts (with its reason) or
   the heap arena grows.
4. In HA, add the CCU's Uptime, Last reset, Stack free and Heap arena
   diagnostics to a history card, as a second record.

## Schedule

Run for 7 days. Induce each outage at least twice, at different times of
day, and leave at least 6 hours between them. Note the time of each.

| Outage | How | Expect within |
| --- | --- | --- |
| Router reboot | Power-cycle the router or access point | 3 min after the network is back |
| HA restart | HA: Settings > System > Restart | 5 min after HA is up |
| Broker restart | HA: Settings > Add-ons > Mosquitto > Restart | 5 min after the broker is up |
| Long HA outage | Stop HA for 1 hour | 6 min after HA is back |
| DHCP loss | Disable the DHCP server for 1 hour | 3 min after it is back |
| Power cut | Unplug the unit for 10 s | 1 min; `LAST RESET` reads `Power on` |

"Expect within" is the time from the service being back to the monitor
showing the unit and every link up again; the longest back-off sets it.

## Pass criteria

- Every unit recovers from every outage with no one touching it.
- No restart other than the power cuts and any updates: the monitor's
  summary shows 0 unexpected restarts (`watchdog`, `cpu_fault`,
  `stack_overflow`, `brownout`).
- Heap arena stays flat after the first hour; Stack free stays above the
  `STACK LOW` threshold.
- The panel stays responsive throughout, and shows last-known data with its
  age while offline.

Record the build, dates, outages and the monitor's summary in the PR or
release notes, and raise an issue for each failure with the CSV attached.
