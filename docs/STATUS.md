# RC3.6 status and next steps

## Acceptance scope

The RC3.6 F 4 GiB image was written to the original GKD Mini system card, read back in full, and boot-tested in both A and R environments. Its private delivery artifact is 4,294,967,296 bytes with SHA-256 `af9c74e0a4952c936a21ce9317528c9170f7f8bf7dce52bd24f41ab07ad887bc`. The public repository does not contain the image or device-specific input data. The public source has normalized build paths, so its source hash differs from the private firmware build.

The accepted device checks cover audio heard by the owner, press and release events from all 18 non-power physical keys, real deep-sleep wake, settings and menus, USB STORAGE/DEBUG return, USB Internet DNS/HTTP, and physical game-card removal and reinsertion with automatic list recovery. A, R and a normal return to A were boot-tested. Representative games from 22 platforms launched; FBN, mGBA and SMS Plus GX state files were restored across emulator processes. These are representative checks, not full playthroughs of every ROM. A previous SMS black-screen observation was withdrawn after a timed readback showed the normal startup sequence with the same state file and emulator binary.

The private source and firmware build passed 38 host test groups and the F A/R build checks. Public packaging validation is recorded separately in `release-manifest.json`; it does not establish that the normalized public tree can reproduce the private firmware image. Real forced power-loss recovery and clean-card installation have not been accepted. Low-battery protection was exercised by simulated thresholds at a safe charge level, not by deliberately exhausting the cell.

Automatic landscape/portrait detection and single-dot-key remapping to Y were deferred by the owner. They are not included in this RC3.6 baseline.

## Next-stage work

- Replacement frontend and its migration/UX acceptance.
- ROM scanning and metadata integration.
- Specific USB network adapter support and hardware acceptance.
- Independent frontend upgrades.
- Redistributable firmware: dependency/license and security review, release keys, clean-card image and writer acceptance.
- Persistent Windows automatic drive-letter assignment; the accepted test used a manually assigned letter.
- Controlled power-loss and recovery testing.
- A-slot kernel size optimization: the accepted RC3.6 build has only 48 bytes of compressed-kernel packing margin.

Card-health scoring is cancelled, not pending work. This public source repository is the first part of publication preparation, not completion of the firmware-distribution tasks above.
