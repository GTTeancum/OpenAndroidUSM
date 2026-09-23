# RE02 verification records

The final recorded results are `final-release`, `final-sanitized`, and
`clean-build-tests`. Each has an actual process exit file containing zero and
17/17 passed CTest targets. `test-summary.json` indexes these logs and hashes.

The `baseline-*` files record the RE01 starting point. `iteration*` and the
older `sanitized-build-tests` logs are retained as development history, not
replaced by the successful final logs. Intermediate failed fixtures exposed
incorrect test assumptions about a drag QTE, an audio path, and a synthetic
cinematic ID; see the final asset-level test and `asset-witness.json` for the
correct linked first-level cinematic 20004 and real outcome IDs.

`original-material-integrity.json` compares all 599 used original files against
the supplied split ZIP members. It does not certify every unused archive entry.
`reference-identity.json` / `reference-verifier.log` verify the original library
and 48 retained function fingerprints. `reference-rejection.log` records a
changed temporary copy failing that check; no altered original is delivered.

`source-change-manifest.json` compares changed source/documentation to RE01.
`source-files.sha256` identifies all cumulative baseline and RE02 source files,
excluding this verification directory and the RE02 patch itself.
`patch-apply-check.json` records clean patch application and byte comparisons
with the delivered source and the source tree used for the clean native build.
`package-files.sha256` covers the cumulative package payload except the package
hash manifests themselves. `overlay-files.sha256` covers the overlay payload.
The complete-source manifest intentionally references unchanged files not
included in the incremental overlay; apply the overlay to RE01 before checking
`source-files.sha256`.

No original ARM code was executed. No Windows executable, graphical gameplay,
physical XInput controller, or audible XAudio2 output was tested for RE02.
