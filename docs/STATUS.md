# RC3.6 status and next steps

## Acceptance scope

The development RC3.6 A/R firmware was installed, fully read back and boot-tested on the original GKD Mini. Recorded checks include shared UI/loading, input and sound, suspend/resume, game-card removal/insertion, USB STORAGE/DEBUG, A/R startup and normal shutdown. A signed update reaching MARK_GOOD was verified in the preceding accepted development lineage; RC3.6 also rejected an invalid update package. These are retained development acceptance results, not tests of a newly built public firmware image.

35 host test groups and focused service sanitizer/update-entry checks passed during RC3.6 closeout. Public-source packaging checks are separately recorded in the release manifest. Real forced power-loss recovery and clean-card installation have not been accepted. Low-battery protection was exercised by simulated thresholds at a safe charge level, not by deliberately exhausting the cell.

Existing emulator acceptance includes MD32X controls/sound/MENU and TMNT2 controls/sound/menu. TMNT2 starts with X and uses the single-dot key for coin insertion in the tested mapping. DOSBox exposes its native SELECT+START menu; RAW input does not provide the unified MENU adapter. Do not interpret these examples as every game being fully play-tested.

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
