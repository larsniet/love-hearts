# Architecture

Engineering notes for this repository: how the firmware fits together, why the
load-bearing decisions are what they are, and the traps that have already cost
time once. Read before changing anything in `firmware/`.

## What this is

ESP32 firmware for two or more networked lamps that mirror one light state over
MQTT. Press the button on any heart and every heart follows.

`src/` holds an unmaintained Next.js app from the original build. **It is out of
scope — do not modify it.** See *Legacy* at the bottom for what it did.

The live code is `firmware/love-lamp/`.

## Commands

```bash
arduino-cli compile --profile esp32 firmware/love-lamp             # pinned build
arduino-cli compile --profile esp32 --warnings all firmware/love-lamp
```

`arduino-cli` is not on PATH. The IDE bundles a current copy (1.5.1) at
`C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe`,
and it shares the IDE's installed cores and libraries by default, so no config
is needed. A 20-second compile check is worth running before every commit — the
sketch this repo shipped for two years never compiled at all.

There is no test suite. Verification is bench testing on real hardware; the
checklist lives in the plan file, not here.

## Firmware

### Toolchain (pinned in `firmware/love-lamp/sketch.yaml`)

| | |
|---|---|
| Core | `esp32:esp32` 3.3.11 |
| FQBN | `esp32:esp32:esp32:PartitionScheme=min_spiffs` |
| Libraries | PubSubClient 2.8, WiFiManager 2.0.17 |
| Boards | COM3 and COM11 (COM8/COM9 are Bluetooth ports, not ESP32s) |

The core jumped 3.0.5 → 3.3.11 on its own at some point; 3.0.5 is still cached
at `%LOCALAPPDATA%\Arduino15\staging\packages\esp32-3.0.5.zip` if a regression
ever needs bisecting. The pinned combination above is verified clean.

`min_spiffs` is not optional. On the default 1.25 MB app partition the sketch is
91% full once the CA root bundle links in; min_spiffs gives 1.9 MB and 61%.

### Configuration is three tiers, and none of it differs between boards

| Tier | Contents | Source |
|---|---|---|
| Identity | client id, portal SSID | derived from `ESP.getEfuseMac()` at runtime |
| Secrets | broker host/port/user/pass, group | NVS, via the config portal |
| Constants | pins, topics, timings | `config.h` |

**Read this before proposing a change to identity.** Both boards previously
shipped `client_id = "ESP32Client-1"`. MQTT brokers evict the existing session
when a duplicate client id connects, so the two lamps kicked each other off
continuously — the headline bug in this project's history. It was caused by
*per-device build variants*: two sketch copies that had to be edited between
uploads, and inevitably weren't.

So identity is derived from the chip, not configured. Do not reintroduce a
per-device build step, a MAC lookup table that supplies the client id, or two
sketch folders. `DEVICE_PROFILES` in `config.h` supplies *cosmetic* names only,
and the firmware must stay correct with that table empty. `ESP.getEfuseMac()` is
used rather than `WiFi.macAddress()` because it reads the eFuse directly and so
works before the Wi-Fi driver starts, which the hostname and portal SSID need.

### Architecture

Two tasks, and the split is load-bearing:

- **`loop()` on core 1** owns the button and the lamp. Never touches the network.
- **`netTask` on core 0** owns Wi-Fi, TLS, MQTT and the state machine.

They meet only through `gIntentQ` and the mutex in `state.cpp`. **PubSubClient is
not thread-safe — every `mqtt.*` call must stay on netTask.** `loop()` publishes
by pushing a `ToggleIntent`.

The reason is not elegance: `mqtt.connect()` can block ~165 s worst case (DNS +
30 s TCP + 120 s TLS handshake + 15 s CONNACK). The old firmware's
"non-blocking" reconnect only throttled how often it froze. `net.cpp` bounds
this to ~30 s and puts it where nobody is waiting.

Files: `net.cpp` (state machine, Wi-Fi events, bounded TLS, MQTT),
`protocol.cpp` (payload codec + reconciliation), `state.cpp` (identity, shared
state, Lamport counter), `store.cpp` (NVS), `button.cpp`, `lamp.cpp`.

### Protocol

Topics under `lovehearts/v1/<group>/`: `state` (retained), `heart/<id>/status`
(retained, LWT), `heart/<id>/tele`. Payload is pipe-delimited, not JSON —
ArduinoJson is deliberately absent because writing JSON is easy and *parsing* it
is where the bugs are:

```
1|ON|heart-34AB12CD5678|97|1757001234
ver state origin         seq epoch
```

A bare `ON`/`OFF` is accepted as legacy v0. **PubSubClient's payload is not
NUL-terminated** — always copy into a bounded buffer first; this is the most
common crash in that library.

Invariants worth not breaking:

- **Receiving a state message never causes a publish.** Publishes come only from
  a button press or the refresh timer. That one rule makes feedback loops
  impossible regardless of how many hearts exist. The single exception is
  suppressing a stale `ON`, which is terminal because an `OFF` cannot be
  suppressed in turn.
- **`OFF` is never age-gated; `ON` older than 6 h is not applied.** This is the
  fix for a lamp lighting itself at 3am off a week-old retained message. Every
  heart reaches the same conclusion independently.
- **A local toggle is published before inbound is drained**, carrying a higher
  Lamport sequence, so a stale retained value loses on merit rather than via a
  timing hack. This is the fix for the lamp snapping back off after an offline
  press.
- `cleanSession = true` deliberately — a persistent session would queue a week of
  toggles and replay them as a strobe.

### Hardware notes

Button GPIO12, light GPIO5, both strapping pins. GPIO12 is safe as wired
(internal pull-down at reset, button shorts to ground) but **must never get an
external pull-up** or the board won't boot. GPIO5 has an internal pull-up and
glitches at boot, so the lamp flickers on every reset — accepted, no rewiring,
which is why the recovery ladder in `net.cpp` prefers radio re-init and treats
`ESP.restart()` as a last resort.

### Two settings that wipe provisioning

- **Tools → Erase All Flash Before Sketch Upload** must stay *Disabled*.
- Don't change **Partition Scheme** after provisioning. (`default` and
  `min_spiffs` happen to place `nvs` identically at `0x9000`/`0x5000`, so the
  one switch this project made was free — but that is not true in general.)

### Secrets

**This repo is public.** `firmware/**/secrets.h` is gitignored and optional
(`__has_include`), so a clean clone and CI both compile without it. Real broker
credentials belong in NVS via the portal, never in a file.

Certificates are *not* secrets and don't belong in `secrets.h` either — the
firmware validates against the Mozilla root bundle already compiled into the
core (`setCACertBundle`). That retires the inline-PEM pattern that previously
left one board carrying an expired CA. (Incidentally the ESP32 does not check
certificate expiry at all: `CONFIG_MBEDTLS_HAVE_TIME_DATE` is unset in the
prebuilt libs. The expired cert was a red herring; the real killers were the
wrong password and the duplicate client id.)

The old broker at `84.82.56.24:9883` is decommissioned. Credentials `lars`/`1602`
that appear in git history and old sketchbook copies are dead strings.

## Legacy: the Next.js app (unmaintained)

Kept as the record of how the original build worked. Nothing should change here.

A Next.js 14 App Router app (package name `lulu`) that toggled the lamp from a
web UI. `pnpm dev` / `build` / `lint`, needs a `.env` from `.env.example`.

- `src/lib/mqttSingleton.ts` connects at module load and stashes the client on
  `global.mqttSingleton` so dev-mode module reloading doesn't reopen broker
  connections. Light state was a plain boolean on that global, recovered after
  restart only because messages were published with `retain: true`.
- `src/components/HomePage.tsx` polled `/api/check-led-status` every 2 s — no
  push, so hardware presses lagged ~2 s.
- `src/middleware.ts` gated everything on one shared password, and
  `api/login/route.ts` stored **the password itself verbatim as the cookie
  value**.
- All routes touching the singleton set `export const dynamic = "force-dynamic"`,
  required or Next would prerender them against a dead connection.

It talks the legacy bare `ON`/`OFF` payload on `home/lights/toggle`, which the
firmware no longer publishes — so it would need the topic and payload updated
before it could work again. `MQTT_CA_FILE` in `.env.example` held certificate
*contents*, not a path.

Dead boilerplate in `src/` (`public/next.svg`, `public/vercel.svg`,
`.text-balance`, the `./src/pages/**` Tailwind glob) is left alone deliberately —
tidying a frozen subtree fixes nothing.

## Notes

The repo used to live on the WSL filesystem for the Next.js toolchain and moved
to the Windows filesystem once the app was frozen, since Arduino IDE is a
Windows app and `arduino-cli` rejects UNC paths. WSL can still reach it under
`/mnt/c/...` if the old app is ever revived.

The host-side protocol test (`firmware/test/run.sh`) needs `g++`; on Ubuntu or
WSL that is `sudo apt-get install -y g++`.
