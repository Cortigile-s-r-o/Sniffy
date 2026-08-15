# ST-Link Sync Notes

Sniffy vendors only a runtime subset of upstream `stlink` under `sniffy/libs/stlink`:

- flattened headers from upstream `inc/` and `src/stlink-lib/`
- `config/chips`
- prebuilt Windows artifacts in `bin/` and `lib/`

The sync entrypoint is `sniffy/tools/sync_stlink/sync-stlink.ps1`.

## Typical usage

Preview a source-derived sync:

```powershell
pwsh -File .\sniffy\tools\sync_stlink\sync-stlink.ps1 \
  -SourceRepoPath ..\third_party\stlink-upstream \
  -SkipBinaries \
  -WhatIf
```

Apply a full sync including prebuilt Windows artifacts:

```powershell
pwsh -File .\sniffy\tools\sync_stlink\sync-stlink.ps1 \
  -SourceRepoPath ..\third_party\stlink-upstream \
  -ArtifactRoot ..\third_party\stlink-build
```

## Current branch status

- headers and chip definitions were synced from upstream `stlink` 1.8.0
- vendored Windows `bin/lib` were refreshed from an upstream UCRT64 MinGW build
- `stlinkconnector.cpp` now parses newer flash types and reads `flash_size_reg` generically
- `stlinkinspector.cpp` now handles split `L5_U5` / `H5` and adds a `C5` UID base
- `stlinkwriter.cpp` matches the current `stlink_write_flash(..., erase_type)` signature
- `stlinkeraser.cpp` now recognizes `C5`; unprotected `C5` can use standard mass erase, but protected `C5` still stops with a clear error because a verified RDP regression flow is not available in the current helper set

## Remaining work

- verify `C5`, `H5`, and `WB0` behavior on real hardware
- if needed, replace the manual device-parameter fallback in `stlinkconnector.cpp` with a narrower safety path
- update `sniffy/THIRD-PARTY-NOTICES.md` and `.github/workflows/release.yml` when the shipped upstream ref is finalized

## Validation summary

- the sync script passed dry-run and real source-derived sync
- the upstream Windows library targets `stlink-shared` and `stlink-static` built successfully with UCRT64 GCC
- Sniffy recompiles through the ST-Link changes and currently stops at an existing Qt linker issue (`Qt6EntryPoint` / `__imp___argc`), not at an ST-Link compile error