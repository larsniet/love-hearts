# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A Next.js web app that toggles a physical light. The web UI and an ESP32 device
(`arduino/love-hearts.ino`) are two peers on the same MQTT topic — either can toggle the
light, and both observe the change. Write-up: https://larsniet.com/journey/crafting-hello-kitty-diy-connected-lights

Note the package name is `lulu`, not `love-hearts`.

## Commands

```bash
pnpm dev      # dev server on :3000
pnpm build    # production build
pnpm start    # serve the production build
pnpm lint     # next lint
```

There is no test suite and no test runner configured.

## Running commands from a Windows-side session

This project lives on the WSL filesystem (`\\wsl.localhost\Ubuntu\home\larsv\projects\love-hearts`).
If Claude Code is running on Windows, its shell is Git Bash and the commands above must be
routed through `wsl.exe`. Three rules make that reliable:

```bash
wsl.exe -d Ubuntu -- bash -ic 'pnpm lint'
```

1. **Use `bash -ic`, not `bash -c`.** Node v24 and pnpm come from `nvm`, which is loaded from
   `~/.bashrc` and only runs in an interactive shell. Without `-i` you get
   `node: command not found`.
2. **No `cd` needed.** WSL inherits the session's working directory, so the command already
   starts in the project root.
3. **Never use `$(...)` inside the quoted command.** Git Bash evaluates it on the Windows side
   before `wsl.exe` sees it, so it silently runs against the wrong shell. Use plain commands, or
   put the logic in a script file and run that.

`MSYS_NO_PATHCONV=1` and `MSYS2_ARG_CONV_EXCL=*` are set in `.claude/settings.local.json`
(gitignored, so a fresh clone on Windows needs it recreated). Without
them Git Bash rewrites POSIX arguments into Windows paths — `/home/larsv/foo` becomes
`C:/Program Files/Git/home/larsv/foo`.

Windows `git` also needs the UNC path in `safe.directory` or it refuses the repo with
"dubious ownership". That is already configured globally for this path.

None of this applies when Claude Code runs inside WSL, which is the smoother setup:

```bash
wsl -d Ubuntu -- bash -lic "cd ~/projects/love-hearts && claude"
```

## Environment

Copy `.env.example` to `.env`. Without it the MQTT client silently falls back to placeholder
credentials (`your-public-domain.com`) and every request logs `ENOTFOUND` — the build still
succeeds, so a passing `pnpm build` does not mean the broker is reachable.

`MQTT_CA_FILE` is passed straight into `mqtt.connect()` as the `ca` option, so it holds
certificate *contents*, not a path.

## Architecture

**The MQTT connection is a process-global singleton.** `src/lib/mqttSingleton.ts` connects at
module load and stashes the client on `global.mqttSingleton` so Next's dev-mode module
reloading doesn't open a new broker connection on every request. Anything needing MQTT imports
this default export rather than calling `mqtt.connect()` again.

**Light state is in-memory, not persisted.** `mqttSingleton.isLedOn` is a plain boolean on that
global. It is written from two places — the `message` handler when the broker echoes the topic,
and `toggle-led` immediately after a successful publish. It resets to `false` on every server
restart; the app recovers because messages are published with `retain: true`, so the broker
replays the last value on reconnect.

**State reaches the browser by polling, not push.** `HomePage.tsx` polls
`/api/check-led-status` every 2s. The optimistic toggle is corrected by the next poll, so
expect up to ~2s of lag after a hardware button press. There is no WebSocket or SSE.

Topic `home/lights/toggle` is hardcoded in both `mqttSingleton.ts` and `toggle-led/route.ts`;
changing it means editing both plus the `.ino` sketch.

All routes touching the singleton set `export const dynamic = "force-dynamic"` — required, or
Next would prerender them at build time against a dead connection.

### Auth

`src/middleware.ts` gates everything except `/login` and `/api/login` by comparing the `auth`
cookie against `AUTH_PASSWORD`. There is one shared password and no user model. **The cookie
value is the password itself, stored verbatim** (`login/route.ts`) — so any change to the auth
scheme has to touch the middleware comparison and the cookie write together.

The middleware `matcher` excludes `api`, but `/api/login` is *also* allowlisted inside the
function body; both are load-bearing for different Next matching passes.

### Hardware side

`arduino/love-hearts.ino` is committed with placeholder credentials (`<mqtt-server-ip>` etc.)
that must be filled in before flashing. It subscribes to the same topic and publishes `ON`/`OFF`
with retain on a short button press. Holding the button 5s calls `wm.resetSettings()` and opens
a WiFiManager captive portal — this wipes saved Wi-Fi credentials on the device.

Both sides use `mqtts` (port 8883), but the server sets `rejectUnauthorized: false`, so TLS
certificate errors will not surface as connection failures.

## Conventions

- `@/*` maps to `./src/*`.
- App Router with route handlers under `src/app/api/`. Components live in `src/components/`,
  not colocated with routes.
- Tailwind for all styling; no CSS modules.

## Other agent configs

A Codex config exists at `~/.codex/config.toml`. To bring any of it into Claude Code, reply
`/import` to scan and list what's importable (MCP servers, slash commands, subagents, skills,
instructions), then `/import --yes=<digest>` using the digest the scan prints. If `/import`
isn't available on this surface, run `claude import` from a terminal instead.
