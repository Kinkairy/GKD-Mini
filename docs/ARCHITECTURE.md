# Architecture

- `kernel/current/`: X1830 board configuration, Linux maintenance patch and initramfs supervision.
- `system/application-core/`: service event loop, asynchronous operations, game launcher, input routing, USB/power adapters and SimpleMenu integration.
- `system/ui-core/`: shared menus, action footer, scrollbars, English text confirmation, OSD, loading, framebuffer and input helpers.
- `system/config-core/`: schema validation, defaults, profile overlays and persistent configuration transactions.
- `system/ar-boot-selector/`: A/R selection and recovery entry.
- `system/rc33-system-update/`: signed package verification and update/backup/trial-boot/confirmation/recovery transactions.
- `system/gkd-card-writer/`: card-package and Windows writer source; a public blank-card workflow is not yet accepted.
- `build/`, `tools/`: pinned input declarations, build wrappers and host tests.

Historical directory names such as `rc33` and `rc34` identify module lineage, not additional supported firmware releases. RC3.6 is the current accepted baseline. The current frontend is SimpleMenu; a replacement frontend and independent frontend upgrades remain future work.
