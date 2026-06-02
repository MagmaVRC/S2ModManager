# Subnautica 2 Mod Manager

A fast, local mod manager for **Subnautica 2**, written in native C++ with Dear ImGui
and Direct3D 11. It installs, organizes, and launches **PAK** content mods and **UE4SS**
script/blueprint mods - from loose files or archives - keeps your game folder clean by
storing every mod in a managed library, and can stream an entire modded profile to a
friend over the internet.


![demo_locate](docs/images/2026-06-02_4C59E316.png)
![demo_usage](docs/images/2026-06-02_D6C23965.png)

## What it does

Mods never pile up loose in your game folder. Every mod you add is copied - bytes and
all - into a single packed, LZMA-compressed library file (`Data.dat`). The game folder
only ever holds the **enabled** mods of your **active profile**, and the manager only
touches files it placed there, so disabling a mod, switching profiles, or wiping the
library never disturbs the base game.

- **Install by drag-and-drop or the Add mods button.** Archives (`.zip` / `.rar` /
  `.7z`) are extracted and each mod is classified automatically - content PAKs into
  `Content/Paks/~mods`, blueprint/logic mods into `LogicMods`, and UE4SS mods into the
  UE4SS `Mods` folder.
- **Enable / disable** any mod with one click. Toggling materializes or removes the
  mod's files in the game folder immediately - no uninstall, no re-download.
- **Load order** control by dragging mods in the list.
- **Profiles** - named, isolated sets of mods and order. Switching a profile reconciles
  the game folder: the old profile's files come out, the new profile's enabled mods go
  in.
- **PAK conflict detection** - warns when two PAK mods ship the same pak name, since only
  one of them will load.
- **One-click UE4SS install** - downloads the latest UE4SS release straight from GitHub
  and installs it into `Binaries/Win64`. Built-in UE4SS mods can be toggled, and
  `mods.txt` is rewritten for you.
- **Profile sharing** - send a whole profile two ways:
  - **Peer-to-peer**, directly over the internet. The host opens a port (automatically
    via UPnP, or one you've forwarded manually), publishes a short **connection key**,
    and streams the profile. Transfers are authenticated with an **Ed25519** signature
    pinned to that key, so the recipient knows they're talking to the right host.
  - **Offline `.s2profile` files** - export a profile to a single file and import it
    anywhere, no network required.
- **Launch** Subnautica 2 through Steam with your active profile already in place.


## Usage

1. Launch `S2ModManager.exe`.
2. On first run, confirm the auto-detected game folder, or open **Settings** and browse
   to it.
3. **Drag** a mod archive or file onto the window, or click **Add mods**. It's
   extracted, classified, and added to your library.
4. In **Mods**, toggle mods on/off and drag to set load order - changes apply to the
   game folder right away.
5. (Optional) Create a few **profiles** for different playthroughs and switch between
   them at any time.
6. If a mod needs UE4SS, click **Install UE4SS** - the manager fetches and installs it.
7. Click **Launch** to start the game through Steam.

### Sharing a profile

- **To send:** open **Share**, pick **Send**, and give the generated connection key to
  your friend. Keep the dialog open until the transfer completes.
- **To receive:** pick **Receive**, paste the key, and the profile is downloaded,
  verified, and installed as a new profile.
- **Offline:** use **File** to export the active profile to a `.s2profile`, or import one
  you were sent.

## Building

Requirements:

- Windows 10/11, x64
- Visual Studio 2022+ with the C++ desktop workload (toolset v145, C++20)
- [vcpkg](https://vcpkg.io) with user-wide integration: `vcpkg integrate install`

Dependencies are declared in `vcpkg.json` (manifest mode). With vcpkg integrated into
Visual Studio, **they install automatically on first build** - no manual `vcpkg install`
step. Just open `S2ModManager.slnx`, select **x64**, and build.

> First build downloads and compiles all dependencies, so it takes a while. Subsequent
> builds reuse the vcpkg binary cache.

## How it's organized

| Layer          | Responsibility                                                          |
|----------------|------------------------------------------------------------------------|
| `src/core`     | Game detection, the VFS library (`Data.dat`), profiles, archive extraction, UE4SS install, and the P2P share stack (HTTP, UPnP, Ed25519 keys, peer session). |
| `src/platform` | Win32 window, native dialogs, firewall rules, image decoding, embedded-resource extraction. |
| `src/ui`       | Reusable ImGui widgets, theming, icons, animation, background.          |
| `src/app`      | The application: mod list, details inspector, settings, share modal, onboarding. |

The whole library lives in one packed `Data.dat`, so a profile - every mod's bytes
included - is self-contained and portable, which is what makes P2P and file sharing
possible.

## Dependencies

All managed through vcpkg (`vcpkg.json`):

| Library | Used for |
|---------|----------|
| [Dear ImGui](https://github.com/ocornut/imgui) (`dx11-binding`, `win32-binding`) | UI, rendered on Win32 + Direct3D 11 |
| [nlohmann/json](https://github.com/nlohmann/json) | Settings, profile, and share-manifest serialization |
| [libarchive](https://libarchive.org/) (`bzip2`, `lzma`) | Extracting `.zip` / `.rar` / `.7z` mod archives |
| [liblzma](https://tukaani.org/xz/) | LZMA compression for the packed `Data.dat` store and wire transfers |
| [libcurl](https://curl.se/) | HTTPS downloads (UE4SS releases) and public-IP detection |
| [OpenSSL](https://www.openssl.org/) | Ed25519 key generation, signing, and verification for connection keys |
| [miniupnpc](https://miniupnp.tuxfamily.org/) | UPnP port mapping for P2P hosting |
| [libwebp](https://chromium.googlesource.com/webm/libwebp) | Decoding `.webp` background art |
| [stb](https://github.com/nothings/stb) | `stb_image` PNG/JPEG decoding for icons |

Direct3D 11 and the Windows networking/firewall APIs are provided by the system.

Third-party license and notice information is listed in
[`THIRD-PARTY-LICENSES.md`](THIRD-PARTY-LICENSES.md).

## License

TBD
