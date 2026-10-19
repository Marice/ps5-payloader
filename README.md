# PayLoader

A PS5 homebrew app (PPSA67670) that lets you manage your payloads with the controller, on the television. It is a
front-end for [Payload Manager](https://github.com/itsPLK/ps5-payload-manager) (pldmgr) by itsPLK. pldmgr does the
work and keeps every setting; PayLoader shows it as a console app, so you do not need a browser.

**Status:** first release. Tested on one PS5 Pro (firmware 13.60, Y2JB with ps5-autoloader): the Payloads,
Repository and Autoload screens, editing the boot list, installing the Signal suite and making a recovery copy on a
USB stick. Not yet confirmed on a console: starting pldmgr from PayLoader, and switching the chain between the
console and the USB stick. The screens below show made-up preview data.

![The Payloads screen](docs/media/payloader/payloads.png)

## What you need

- A PS5 that runs homebrew apps (for example with etaHEN).
- **pldmgr 0.6.0 or newer.** PayLoader talks to it on `127.0.0.1:8084`, and **brings its own copy**: when pldmgr
  does not run, PayLoader offers to start it (it sends the copy to the ELF loader on port 9021, so an ELF loader such
  as etaHEN's must run). pldmgr then keeps itself up to date from its official repository. PayLoader is pldmgr's
  front-end: it switches off pldmgr's own home screen icon and browser start. pldmgr is not added to the boot
  chain; after a restart PayLoader asks again. The Autoload screen works without pldmgr.

## Quick start

1. **Install PayLoader:** unzip the [release](https://github.com/Marice/ps5-payloader/releases/latest) and copy the
   `PPSA67670` folder to `/data/homebrew/` on the console (FTP).
2. **Open it.** When pldmgr does not run, press X to start the one PayLoader brings. **L1** and **R1** move between the screens; the **touchpad** explains the screen you are on.

When pldmgr does not run, every screen says how to start it and checks again every 3 seconds.

## The screens

| Screen | What it does |
| --- | --- |
| **Payloads** | The payloads pldmgr has installed, favourites first, then the ones on a USB drive. **Cross** starts one. **Triangle** opens its menu: favourite, start with the autoload list, delete. |
| **Repository** | The payloads in pldmgr's repositories. Each row says if it is installed, or which update is ready. **Cross** installs or updates; **Square** refreshes every repository. When the same payload is installed from another repository, PayLoader asks before it replaces it. |
| **Autoload** | What starts with the console, in order, with waits between the payloads: the boot chain your console actually uses (see below). **Cross** on an entry moves it, changes a wait or removes it. For pldmgr's own list also on or off and the countdown; while pldmgr runs it, the screen shows its progress and can stop it. |
| **Running** | The payloads that run now, with their memory. **Cross** stops one, after it asks. The system's own processes and pldmgr itself are not listed. |
| **Settings** | pldmgr's repositories (add one by its address, remove one) and its options: extra repositories, the browser at start, its own home screen icon, the disc player, USB drives. |

<p>
<img src="docs/media/payloader/repository.png" width="49%" alt="The Repository screen">
<img src="docs/media/payloader/autoload.png" width="49%" alt="The Autoload screen">
</p>
<p>
<img src="docs/media/payloader/running.png" width="49%" alt="The Running screen">
<img src="docs/media/payloader/settings.png" width="49%" alt="The Settings screen">
</p>

## Which list the Autoload screen edits

There are two ways a console starts payloads after the exploit, and PayLoader shows the one in use:

- **ps5-autoloader's list.** itsPLK's autoloader looks for `ps5_autoloader/autoload.txt` on a USB drive
  (`/mnt/usb0` to `/mnt/usb7`) first, then in `/data`; folders for single apps (`ps5_autoloader_<TITLE_ID>`) come
  first. PayLoader cannot tell which app the autoloader runs under, so it edits the general folder and names any
  folders for single apps; it uses one of those only when there is no general list. When it finds one, it starts the files it names, one per line, with `!<ms>` waits. PayLoader then edits that
  file directly, also when pldmgr does not run. Before the first change of a day it keeps the list as
  `autoload.txt.bak-payloader-<date>` next to it. A payload installed in pldmgr is copied into the folder when you add
  it; removing an entry keeps the file. When the list starts pldmgr too, the screen also says what pldmgr's own list
  starts.
- **pldmgr's own list.** When the autoloader finds no list, it starts pldmgr, and pldmgr starts its own list
  (`/data/pldmgr/autoload.txt`) when its autoload is on. PayLoader edits that list through pldmgr.

Changes take effect the next time the console starts. The app needs etaHEN's "Allow /data in sandbox" to read
`/data`.

### A USB stick as a safety net

**Make a recovery USB** (Autoload screen) copies the list and the payloads next to it to `ps5_autoloader/` on the
USB stick that is plugged in, **switched off**: the file is named `autoload.txt_disabled`, which the autoloader
ignores, so the stick can stay in. Payloads that pldmgr keeps are named by their full path and not copied.

When a stick with such a folder is plugged in, the Autoload screen shows **Start the chain from: Console / USB
stick**. Switching renames the file on the stick (`autoload.txt_disabled` <-> `autoload.txt`); the autoloader looks
on USB first, so a switched-on stick takes over from the next start. PayLoader always edits the list in use.

**In an emergency**, when the chain no longer starts and PayLoader cannot be reached: put the stick in a PC and
rename `ps5_autoloader/autoload.txt_disabled` to `autoload.txt`. The console then starts the stick's list. Fix the
console's list in PayLoader, then switch back to the console.

## How it works

PayLoader calls pldmgr's HTTP API on `127.0.0.1:8084`, the same API that pldmgr's web page uses. Reads are `GET`;
changes are `POST` with the header `X-PLDMGR-Request: 1`. The API has no version of its own, so PayLoader checks
pldmgr's version first (`/version`) and reads every field with a fallback.

| Screen | Calls |
| --- | --- |
| Payloads | `GET /list_payloads`, `GET /get_config`, `POST /loadpayload:<slot>`, `POST /manage:delete`, `POST /set_config` (favourites, autoload list) |
| Repository | `GET /repository_payloads`, `POST /repository_refresh`, `POST /repository_install` |
| Autoload | `GET /autoload_status`, `POST /set_config`, `POST /abort` |
| Running | `GET /processes_list`, `POST /process_kill` |
| Settings | `GET /sources_list`, `POST /sources_add`, `POST /sources_remove`, `POST /set_config` |

Every request runs on a background thread, so the screen never waits for pldmgr. A screen asks only for what it shows.

**Security.** pldmgr's web page has no password and listens on every network address (port 8084). Anyone on your
network can open it. This is how pldmgr works, not something PayLoader adds or can change. Use them on a network you
trust.

## Building

The app is built on [ps5-homebrew-ui](https://github.com/blackbearreloaded/ps5-homebrew-ui) by BlackBearReloaded.

```bash
docker build -t payloader-build docker/      # once
docker/run.sh make test-unit                  # unit tests, sanitizers on
docker/run.sh make                            # the app folder in dist/PPSA67670
docker/run.sh make lint                       # format, static analysis, metadata
docker/run.sh tools/host-snapshots.sh build/snapshots all   # pictures of every screen
```

The pictures use a pretend pldmgr with made-up payloads (`src/payloader/demo.cpp`); `PAYLOADER_OFFLINE=1` shows the
screens without one. The guides of the UI kit are in [docs/KIT_README.md](docs/KIT_README.md).

## Credits and licence

- [Payload Manager (pldmgr)](https://github.com/itsPLK/ps5-payload-manager) by itsPLK: the payload that PayLoader
  controls, and carries (GPL-3.0; the version is in `assets/payload/pldmgr.txt`, its source is that tag on GitHub;
  `tools/bundle-pldmgr.sh` fetches it). PayLoader follows the rules of pldmgr's web page for versions, updates and
  conflicts.
- [ps5-homebrew-ui](https://github.com/blackbearreloaded/ps5-homebrew-ui) by BlackBearReloaded: the UI kit.

PayLoader is licensed under the GNU General Public License v3.0 or later. See [LICENSE](LICENSE) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
