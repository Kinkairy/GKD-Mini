# Licensing and source attribution

The root LICENSE is GNU GPL version 2. Project-owned files without a more specific declaration are licensed GPL-2.0-only. Existing file headers govern where present; this does not remove GPL-2.0-or-later options or Linux-syscall-note exceptions.

| Component | Source / terms |
| --- | --- |
| Linux maintenance patches and board integration | Linux 6.1.28; upstream input commit `91fe78280ac7dd0dae0f58cb271e821bd39ba97e`; existing GPL notices retained. The full upstream kernel is not vendored. |
| libopk metadata/argument handling | [pcercuei/libopk](https://github.com/pcercuei/libopk), commit `5cb5230c4266f866a97edd1740bc4bb0d6801038`, GPL-2.0. See `system/application-core/third_party/libopk-5cb5230/UPSTREAM-PROVENANCE.md` and `system/application-core/LICENSES/GPL-2.0.txt`. |
| WenQuanYi fixed menu glyphs | [WenQuanYi Bitmap Song](https://github.com/AmusementClub/Wenquanyi-Bitmap-Song-TTF), commit `7724da71817090ba92e76d9d40e3dd43afef25de`, GPL-2.0 with font embedding exception. Full notice, authors and subset hashes: `system/ui-core/third_party/wenquanyi/`. |
| Linux fallback font conversion | The generator consumes an external pinned Linux font source; its upstream terms remain applicable. |

BusyBox, SimpleMenu, emulator OPKs, toolchain binaries and original userspace partition contents are external dependencies, not distributed binaries in this repository. Their inclusion in a future firmware release needs separate source/licensing review.

`system/ui-core/assets/recovery-boot.png` is deliberately omitted because its publication provenance has not been established. No rights in that image are implied by this repository's license.

The public release manifest identifies the source baseline and exact exported files. Generic paths replace internal build-host paths; private repository history and operational records are excluded.
