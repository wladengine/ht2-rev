# Working agreements

- Read [README.md](README.md) and [the regression catalog](docs/REGRESSIONS.md) before building or testing.
- Build with `tools/build-windows.ps1` and an explicit original `-GameExe`. Keep MSVC v142, x86, `/arch:IA32` and the separate `/fp:fast` compilation of `x87.cpp`. Require independent evidence before changing arithmetic.
- Use fresh copies for runs. Keep installations and personal profiles intact. Store local inputs, executables, extracted resources, telemetry, dumps and results in ignored `.local/`. Archived runs may lack resources.
- Put reusable regressions and synthetic fixtures in `tests/`. Keep contributor-facing prose in English. Tie comparisons to executable hashes, configuration and inputs. Separate capture integrity from gameplay acceptance. Verify that a memory or ABI regression rejects the old implementation.
- Keep upstream master available for integration. Develop baseline corrections and modernization on separate branches.
