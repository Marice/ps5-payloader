# Changelog

All notable changes to PayLoader are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions are the app's content version.

## [v00.001.000] - 2026-10-19

First release.

### Added

- Payloads screen: start an installed payload; mark favourites; add it to or take it out of the autoload list;
  delete it.
- Repository screen: install payloads from pldmgr's repositories, see and install updates, refresh every
  repository. Asks before it replaces a payload installed from another repository.
- Autoload screen: switch pldmgr's start list on or off, set the countdown, order the payloads, add and change waits.
  Shows the progress of a run and can stop it.
- Running screen: the payloads that run now with their memory; stop one after a question. System processes and
  pldmgr itself are not listed.
- Settings screen: add and remove repositories; pldmgr's options for extra repositories, the browser at start, its
  own home screen icon, the disc player and USB drives.
- A card on every screen when pldmgr does not run, is older than 0.6.0, or something else uses port 8084.
- Autoload screen: the boot chain the console uses. ps5-autoloader's `autoload.txt` (USB before `/data`) is edited
  directly, also without pldmgr, with a daily backup of the list; pldmgr's own list when the autoloader has none.
  Payloads screen: "Start with autoload" acts on the same chain.
- The Signal suite: PayLoader keeps its repository in pldmgr's sources, installs or updates the whole suite in one
  go, recommends it when ps5-homeassistant is installed alone, and offers to start it with the console.
- PayLoader brings pldmgr: when it does not run, X starts the bundled copy through the ELF loader. pldmgr's own
  icon and browser start are switched off; PayLoader is its front-end.
- Autoload screen: waits on a slider (0.5 to 25 seconds, steps of 0.5). Entries that pldmgr keeps are named by
  their full path; comments and directives in the list (`#`, `@sync`) are kept.
- A recovery copy of the boot list on a USB stick, stored switched off (`autoload.txt_disabled`), and a switch
  between the console's list and the stick's.
