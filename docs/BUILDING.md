# Build and verification boundaries

This is a source snapshot, not a self-contained firmware SDK. Do not interpret a successful host test as a safe installable image. There is no public flash command or blank-card download in this release.

## External inputs

`build/rc3.7-inputs.json` records pinned hashes for the userspace partition input, BusyBox archive, slot-header template, kernel commit, builder image and toolchains. The partition and template are not provided: they contain device-specific/dependency material whose redistribution has not been cleared. Supply only inputs obtained lawfully and reviewed independently.

The legacy build wrappers retain strict input hashes and output guards. Their original host paths are normalized in this snapshot:

- project checkout: `/opt/gkd-mini`
- temporary outputs: `/tmp/gkd-mini-public`
- dependency root / generic build home: `/opt/gkd-build`
- shared cross-build layout: `/srv/c-builder`

These are documented layout templates, not a claim that those dependencies are installed. The container Dockerfiles are historical dependency recipes; the accepted build image digest is in `build/rc3.7-inputs.json`. Rebuilding a Dockerfile is not guaranteed to reproduce that exact image digest. The entire firmware build has not been rerun from this public tree.

The original `system/ui-core/assets/recovery-boot.png` is omitted pending artwork provenance review. The recovery build and boot-art test still require an appropriately licensed image at that location. Its generator is retained; the omission is deliberate rather than a silent replacement of the accepted artwork.

Because build paths are part of the source identity, this normalized snapshot has a different source hash from the accepted private build. Do not reuse the accepted firmware hashes as verification of a new public build.

## Offline checks

With Python 3, run the source-integrity and configuration checks:

```sh
python3 validate_release.py
python3 system/config-core/tests/test_config_core.py -q
python3 tools/gkd-source-closure.py
```

The broader entry `sh tools/gkd-test-all` contains environment-dependent and boot-art checks. Until external dependencies and artwork are supplied, it is not advertised as a fully passing standalone build command. The UI harness additionally expects Docker, the pinned builder image and an external Linux font source; it is not a native-compiler-only command. Container-backed test scratch paths require the generic temporary parent `/tmp/gkd-mini-public` to exist. Windows card-writer checks additionally require Windows PowerShell 5.1.

`validate_release.py` verifies exact file bytes, executable modes, completeness and the publication manifest. This proves integrity of the source package, not firmware correctness or hardware safety.
