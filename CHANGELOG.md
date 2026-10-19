# Changelog

All notable changes to PayLoader are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions are the app's content version.

## [Unreleased]

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
