# Love Hearts

Two (or more) ESP32 lamps that mirror one light state. Press the button on any
heart and every heart follows, over MQTT.

> The Next.js app in `src/` is unmaintained and no longer part of this project.
> The live code is the firmware in [`firmware/love-lamp/`](firmware/love-lamp).

## Hardware

| | |
|---|---|
| Board | ESP32 Dev Module (`esp32:esp32:esp32`) |
| Button | GPIO12, `INPUT_PULLUP`, active low |
| Light | GPIO5 via a MOSFET |
| Serial | 115200 |

GPIO12 is a strapping pin, but the wiring is safe as built: it has an internal
pull-*down* at reset and the button shorts to ground, so it reads low both idle
and pressed. **Never add an external pull-up to GPIO12** — the board will not
boot. For a new build, prefer GPIO27 for the button and GPIO32 for the light
(GPIO5 also strapping, and it glitches briefly during boot, so the lamp flickers
on every reset).

## Gestures

Acted on **release**, and the lamp blips as each threshold is crossed — so you
count blips and let go at the one you want. Over-holding is recoverable.

| Hold | Action | Blips |
|---|---|---|
| tap | toggle the light | 0 |
| 0.6 – 1.9 s | nothing (kills accidental presses) | 0 |
| 2 – 4.9 s | pairing window *(not implemented yet)* | 1 |
| 5 – 10.9 s | Wi-Fi + broker config portal | 2 |
| 11 – 19.9 s | factory reset | 3 |
| 20 s+ | abort, do nothing | 4 |

## What the blinking means

Status patterns play only while the lamp is **off**, so they never mask the
actual mirror state.

| Pattern | Meaning |
|---|---|
| dark | online and off — all well |
| 100 ms every second | connecting |
| brief flash every 3 s | can't reach Wi-Fi |
| 3 short flashes, pause | Wi-Fi password rejected — re-run the portal |
| double flash every 2.5 s | Wi-Fi up, broker unreachable |
| long-short-long | captive portal (hotel/café Wi-Fi) intercepting DNS |
| steady 500 ms blink | config portal is open |
| rapid continuous | factory reset armed — let go to cancel |

## First-time setup

1. Create a free MQTT broker — [HiveMQ Cloud](https://www.hivemq.com/mqtt-cloud-broker/)
   or EMQX Serverless. Note the hostname, port 8883, username and password.
   Give the lamps their own restricted user limited to `lovehearts/v1/#`, so a
   leaked credential can only flick a light.
2. Install the toolchain. Either open the sketch in Arduino IDE and let Library
   Manager fetch **PubSubClient** and **WiFiManager**, or let the pinned profile
   do it:
   ```bash
   arduino-cli compile --profile esp32 firmware/love-lamp
   ```
3. In Arduino IDE set **Tools → Partition Scheme → Minimal SPIFFS (1.9MB APP)**.
   The default 1.25 MB partition leaves the sketch 91% full; this drops it to
   61%.

There is no `secrets.h` to create. It is optional (guarded by `__has_include`)
and only useful as a build-time seed — the intended path is the config portal.

## Flash and provision

Per board, the entire procedure is *switch the port*:

1. **Tools → Port** → COM3 (or COM11)
2. Upload
3. **Tools → Serial Monitor** at 115200 — the banner prints the device id,
   group and broker

Then hold the button 5 s, join the `Love lamp XXXXXX` Wi-Fi network (password
in `secrets.example.h`), and fill in Wi-Fi plus broker host/port/user/password.
Saved values go to NVS and survive every later upload.

Two settings destroy that provisioning, so leave them alone:

- **Tools → Erase All Flash Before Sketch Upload** must stay *Disabled*
- don't switch partition scheme after provisioning (`default` and `min_spiffs`
  happen to share the same nvs region, but that is not true in general)

## Adding another heart

Flash the same sketch, provision it through the portal with the same group id,
and it joins. Nothing to edit — identity comes from the chip's own MAC.

Optionally add its MAC to `DEVICE_PROFILES` in
[`config.h`](firmware/love-lamp/config.h) for a friendlier portal name. That is
purely cosmetic; the client id is always MAC-derived.

## Troubleshooting

**Both lamps keep dropping off.** Historically this was two boards sharing
`client_id = "ESP32Client-1"` — a broker evicts the older session when a
duplicate id connects, so they fought in a loop. Can't recur now, but if you see
it, check the serial banner shows *different* device ids.

**Lamp turns itself back off shortly after pressing it.** Was caused by an
offline press never being published while the broker still held the old retained
value. Fixed by queueing local toggles with a higher sequence number.

**Lamp comes on by itself.** A stale retained `ON`. Now age-gated at 6 hours.

**Portal won't appear.** Hold past 5 s but release before 11 s — watch for the
second blip.

**Lamp flickers on reboot.** Expected; GPIO5 is a strapping pin. The recovery
ladder is deliberately reboot-shy because of it.

## How it started

[Crafting Hello Kitty DIY connected lights](https://larsniet.com/journey/crafting-hello-kitty-diy-connected-lights)
— the original build write-up. The firmware has been rewritten since.
