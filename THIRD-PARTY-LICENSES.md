# Third-Party Licenses

This project uses third-party open-source software. The inventory below was prepared
from the local dependency manifests and vcpkg-installed package metadata in this
workspace.

For vcpkg packages, the authoritative bundled notice text is the corresponding
`vcpkg_installed/.../share/<package>/copyright` file listed for each dependency.
For npm packages, license metadata comes from `tools/iconbuild/package-lock.json`.

This file is not legal advice. Review the upstream license text before publishing a
binary release.

## Application Dependencies

These dependencies are used by the C++ application and may be linked into the Windows
binary.

| Component | Version | License | Used for | Local notice source |
| --- | --- | --- | --- | --- |
| Dear ImGui | 1.92.7 | MIT | Immediate-mode UI, Win32 binding, Direct3D 11 binding | `vcpkg_installed/x64-windows-static/x64-windows-static/share/imgui/copyright` |
| nlohmann/json | 3.12.0 | MIT | JSON serialization for settings, profiles, and sharing metadata | `vcpkg_installed/x64-windows-static/x64-windows-static/share/nlohmann-json/copyright` |
| libarchive | 3.8.7 | BSD-style permissive notices; see bundled notice file | Reading and extracting archive formats | `vcpkg_installed/x64-windows-static/x64-windows-static/share/libarchive/copyright` |
| liblzma / XZ Utils | 5.8.3 | Public domain / 0BSD / LGPL-2.1-or-later / GPL-2.0-or-later portions; see bundled notice file | LZMA compression and decompression | `vcpkg_installed/x64-windows-static/x64-windows-static/share/liblzma/copyright` |
| libcurl | 8.20.0 | curl license, BSD-3-Clause, ISC portions | HTTP(S) downloads and network requests | `vcpkg_installed/x64-windows-static/x64-windows-static/share/curl/copyright` |
| OpenSSL | 3.6.2 | Apache-2.0 | TLS and Ed25519 cryptographic operations | `vcpkg_installed/x64-windows-static/x64-windows-static/share/openssl/copyright` |
| miniupnpc | 2.3.2 | BSD-3-Clause | UPnP port mapping | `vcpkg_installed/x64-windows-static/x64-windows-static/share/miniupnpc/copyright` |
| libwebp | 1.6.0 | BSD-3-Clause | WebP image decoding | `vcpkg_installed/x64-windows-static/x64-windows-static/share/libwebp/copyright` |
| stb | 2024-07-29 | MIT / public domain dual option | PNG/JPEG image decoding through stb headers | `vcpkg_installed/x64-windows-static/x64-windows-static/share/stb/copyright` |
| zlib | 1.3.2 | Zlib | Compression support used by dependent libraries | `vcpkg_installed/x64-windows-static/x64-windows-static/share/zlib/copyright` |
| bzip2 | 1.0.8 | bzip2 license | Archive compression support used by libarchive | `vcpkg_installed/x64-windows-static/x64-windows-static/share/bzip2/copyright` |
| lz4 | 1.10.0 | BSD-2-Clause | Archive compression support used by libarchive | `vcpkg_installed/x64-windows-static/x64-windows-static/share/lz4/copyright` |
| zstd | 1.5.7 | BSD-3-Clause, GPL-2.0-only portions for programs/tests; see bundled notice file | Archive compression support used by libarchive | `vcpkg_installed/x64-windows-static/x64-windows-static/share/zstd/copyright` |

## Build Tool Dependencies

These dependencies are used by `tools/iconbuild` to generate raster icon assets. They
are development/build-time dependencies and are not part of the C++ application runtime
unless separately redistributed.

| Component | Version | License | Used for | Metadata source |
| --- | --- | --- | --- | --- |
| `@resvg/resvg-js` | 2.6.2 | MPL-2.0 | SVG rendering during icon generation | `tools/iconbuild/package-lock.json` |
| `@resvg/resvg-js-win32-x64-msvc` and related optional platform packages | 2.6.2 | MPL-2.0 | Platform-specific resvg native binaries | `tools/iconbuild/package-lock.json` |
| `lucide-static` | 1.17.0 | ISC | Source SVG icon set for generated app icons | `tools/iconbuild/package-lock.json` |

## System Libraries

The application also uses Microsoft Windows and Direct3D APIs supplied by the operating
system and the Windows SDK. Those system components are not included in this third-party
open-source inventory.

## Maintenance Notes

When dependencies change:

1. Update `vcpkg.json` or `tools/iconbuild/package-lock.json` as usual.
2. Rebuild or restore vcpkg packages so `vcpkg_installed` contains current metadata.
3. Re-check `vcpkg_installed/x64-windows-static/vcpkg/status` and each package's
   `share/<package>/copyright`.
4. Update this file with any added, removed, or version-changed components.
