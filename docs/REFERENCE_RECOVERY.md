# Recovering the verified original ARM reference

The strict reconstruction sometimes needs raw instructions from the user's
verified Android library. That library is intentionally ignored and must never
be committed.

The retained RE06 manifests identify the expected reference as:

- path: \`game/original/libspiderman.so\`
- size: 7,713,000 bytes
- SHA-256: \`f35d959d54d43d3d4cce07d1afac4cbe52438997a3bf4d5304c50610cabc2679\`

If the original 17 split project archive volumes are available in one directory
as \`OpenAndroidUSM.zip.001\` through \`OpenAndroidUSM.zip.017\`, run:

\`\`\`text
python tools/recover_original_reference.py --uploads-dir <directory>
\`\`\`

The tool reads only the archived reference library. Before writing, it checks:

1. every split-volume name and exact retained size;
2. the reference member path and uncompressed size;
3. the retained ZIP CRC;
4. agreement between the RE06 original-material and fingerprint manifests;
5. the recovered SHA-256.

The default output is the ignored \`game/original/libspiderman.so\`. If a
different file already exists there, recovery fails rather than overwriting it.

To verify that the archive contains the exact reference without writing the
binary:

\`\`\`text
python tools/recover_original_reference.py --uploads-dir <directory> --check-only
\`\`\`

This tool does not execute ARM code and does not make any gameplay claim. Once
the verified library is recovered, run \`tools/verify_re06_reference.py\` before
using retained RE06 addresses/evidence for further reconstruction.
