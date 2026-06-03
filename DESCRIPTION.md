## Description

Subnautica 2 Mod Manager is a fast, local app for installing, organizing, and launching
your Subnautica 2 mods, without ever making a mess of your game folder.

Instead of dropping `.pak` files into `~mods` by hand, unpacking UE4SS archives, and
editing `mods.txt` yourself, you just drag a mod onto the window. Every mod is copied
into a managed library, and the game only ever sees the mods you've actually enabled.
Turn a mod off, switch to a different setup, or wipe everything, your base game is never
touched, because the manager only writes files it owns.

It handles both **PAK content mods** and **UE4SS script/blueprint mods**, sorts them into
the right folders automatically, and can even **send your entire modded setup to a friend
over the internet** with a single connection key.

## Installation instructions

1. Download the latest release and unzip it anywhere you like.
2. Run `S2ModManager.exe`.
3. On first launch, the manager auto-detects your Subnautica 2 install. If it can't find
   it, open **Settings** and browse to your game folder.
4. That's it, start adding mods.

To add a mod: **drag its archive (`.zip` / `.rar` / `.7z`) or files onto the window**, or
click **Add mods**. The manager extracts it, figures out what kind of mod it is, and adds
it to your library. Toggle it on in the **Mods** list and you're done.

If a mod needs UE4SS, click **Install UE4SS** and the manager downloads and installs it
for you.

## Main features

- **Drag-and-drop install**: drop an archive or loose files and they're extracted and
  sorted automatically (content PAKs, LogicMods, and UE4SS mods all go to the right
  place).
- **One-click enable / disable**: no uninstalling, no re-downloading. Toggling a mod
  adds or removes it from the game instantly.
- **Load order**: drag mods to set the order they load in.
- **Profiles**: keep separate, named mod setups for different playthroughs and switch
  between them whenever you want.
- **Clean game folder, always**: mods live in a managed library, not loose in your game.
  The base game is never modified, so everything is reversible.
- **One-click UE4SS install**: grabs the latest UE4SS straight from GitHub and sets it
  up, including built-in mods and `mods.txt`.
- **ReShade support**: install ReShade and shader packs and manage per-profile presets,
  without leaving the manager.
- **Conflict warnings**: tells you when two mods clash so you're not left guessing why
  one isn't working.
- **Profile sharing**: send a whole modded setup to a friend peer-to-peer over the
  internet (secured with a unique connection key), or export it to a single `.s2profile`
  file to share offline.
- **One-click launch**: starts Subnautica 2 through Steam with your active setup ready.
- **Update notifications**: checks GitHub for new releases and points you to the download.

## Requirements

- **Windows 10 / 11 (64-bit)**
- **Subnautica 2** installed through **Steam**
- **UE4SS**: only needed for UE4SS-type mods. You don't have to install it yourself; the
  manager can do it for you with one click.
- For **peer-to-peer profile sharing**: a working internet connection. The host's network
  needs UPnP enabled on the router, or a manually forwarded port. (This is optional -
  everything else works fully offline, and you can always share via `.s2profile` files
  instead.)

No accounts, no launcher, and no background services required.