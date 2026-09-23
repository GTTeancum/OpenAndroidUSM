# OpenAndroidUSM RE01

**Source continuation, not a completed or newly verified playable Windows build.**

Based on uploaded commit `59903b8037945c24ad81a5f947d69f01dc4de5b7`.

This checkpoint routes the original R1-release rescue request from XInput RB,
separates A/Cross QTE taps from X/Square punches, preserves the native two-update
combat press window, and gives hostage mash decay the evidenced real-time delta.
The production XInput translation code now has deterministic regression tests.

## Apply

Extract the overlay into your existing `OpenAndroidUSM` project directory.
It contains source-file replacements; no second patch step is required.
Alternatively apply `RE01.patch` to the unmodified base checkout.
Keep your existing `game/` folder. The complete source ZIP is an alternative
for a separate checkout, not an asset or dependency bundle.

## Windows commands

```powershell
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release
ctest --test-dir build/windows-msvc -C Release -R "^OpenAndroidUSM\.(CoreTests$|InputParity\.)" --output-on-failure
.\build\windows-msvc\Release\OpenAndroidUSM.exe
```

## Verification boundary

Seven selected native Linux CTest targets passed with the supplied game data.
The adapter/router also passed 100,000 deterministic transition frames under
address/undefined-behavior sanitizers. All 599 extracted original data/library
files match the upload. No original ARM code was executed by these tests.

No newly built Windows executable, rendered gameplay capture, audible-audio
verification, or physical-controller verification is included. Timeout-clock
parity, hostage sound start timing, switch consumption, disconnect cancellation,
full input arbitration, and Sandman's uploaded WIP remain explicitly unresolved.

Read `docs/RE01_RECONSTRUCTION_REPORT.md` for exact native addresses, tested
scope, inherited limitations, and instructions. Actual logs and integrity
records are in `RE01_verification/`.
