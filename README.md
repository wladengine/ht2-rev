# Hard Truck 2 — Windows 8.1 baseline

This branch packages the Windows x86 reconstruction as a single commit on upstream `master` at `53627ed25560a0958b5c5a4525dde652bb2a8ed3`. The comparison revision is `2f190428221ec36ad770201a719da2f82fc8c1be`. Building this checkout requires no other branch, tag or private history.

## Build on Windows

Install Git, PowerShell 7, Python 3.12, the MSVC v142 x86/x64 component (14.29.30133) and Windows SDK 10.0.26100.0, including the x86 libraries. Use a legally obtained installation of the game. From PowerShell in this checkout:

```powershell
python -m venv .venv
.\.venv\Scripts\python -m pip install -r requirements-dev.txt
$env:PATH = "$PWD\.venv\Scripts;$env:PATH"
$game = 'C:\Games\Hard Truck 2'
.\dev.cmd doctor
.\dev.cmd check -GameExe "$game\king.exe"
.\dev.cmd run -GameDir "$game"
```

`doctor` prints the resolved tools. `check` builds the game and runs the native sound regression. `build` builds just the game. For a nonstandard installation, set `VCToolsInstallDir` and `WindowsSdkDir` or pass `-VcDir` and `-SdkRoot`. The launcher sets execution policy for its own process. Each build uses an explicit resource donor and records its SHA-256, toolchain, build options and Git revision in a private manifest.

`run` launches a fresh game copy and records its exit status. Use `prepare` to stage the copy for a debugger and set its working directory to the printed path. The user installation and profiles are only read as allowed inputs. The launcher excludes existing profiles when copying resources.

## Verify and contribute

```powershell
.\dev.cmd test -GameExe "$game\king.exe"
```

Use the Russian Steam 8.1 EXE (SHA-256 `31fbd1dbbe7706183f09a3688482791255db2bfb557c02675374b96796ce2ebd`) with its game resources for comparisons. The reference production build has normalized SHA-256 `5e3a9d46416b3584fb6195d338e33a5d0e495ea6ba7a646aa629b08b7920cd2a`. Run `python tools/compare-builds.py FIRST.exe SECOND.exe` to compare builds while masking only PE timestamps and checksum. See the [test catalog](docs/REGRESSIONS.md) for coverage, negative controls and private-input scenarios.

| Path | What belongs there |
|---|---|
| `src/`, `symbols.csv`, `units.csv` | Game source and original address/unit mappings |
| `tools/` | Reusable build, run preparation and binary comparison tools |
| `tests/` | Regression code, synthetic inputs, expected behavior and analyzers for user-supplied data |
| `.local/build/`, `.local/logs/` | Executables, objects, linker maps, resource provenance and build manifests |
| `.local/fixtures/` | Local copies of game inputs, saves and private reproduction materials |
| `.local/runs/`, `.local/regressions/` | Disposable game copies, captures, dumps and test results |

Commit source and reproducible tests. Keep original executables, reference machine code, private profiles, recorded telemetry, videos and reports outside Git. When reporting a defect, give the smallest reproducer, relevant hashes and observed behavior. [Regression report template](.github/ISSUE_TEMPLATE/gameplay-regression.md)

## Game ownership

This unofficial fan project is independent of SoftLab-NSK and Fulqrum Publishing. Building needs resources from your own original executable. Playing needs the installed game assets. The game is available on [Steam](https://store.steampowered.com/app/4487840/), [GOG](https://www.gog.com/game/hard_truck_2_king_of_the_road) and ZOOM Platform. Rights holders can contact the project through its issue tracker.
