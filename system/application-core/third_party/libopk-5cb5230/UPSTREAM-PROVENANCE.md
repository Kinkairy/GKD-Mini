# Upstream provenance and reuse boundary

This standalone candidate derives only metadata selection and argv expansion from
pcercuei/libopk, repository https://github.com/pcercuei/libopk.

- Pin: 5cb5230c4266f866a97edd1740bc4bb0d6801038
- opkrun.c blob: 17d98a129d915b2e7b9405d7c6f479e7a9fd73c9
- opk.h blob: 493ac94a53d7fa2ba2fc8b0703123997409cd365
- CMakeLists.txt blob: 1d02965da6dbc4db8b0c7955a7fe35d7c8f8f7d6
- License: GPL-2.0; verbatim text is LICENSES/GPL-2.0.txt.

Preserved source behavior:
- metadata iteration and optional metadata filename selection;
- raw OpenDingux Exec splitting on ASCII spaces;
- %f, %F, %u, %U mapping through realpath and file:// URLs;
- Terminal, X-OD-NeedsJoystick, X-OD-NeedsGSensor, and
  X-OD-NeedsDownscaling metadata flags.

Intentional safety boundaries:
- exact metadata keys; duplicate Name or Exec rejects;
- raw Name is retained as mount_name only after rejecting empty, slash, dot and
  dot-dot values; valid UTF-8 bytes are not rewritten;
- empty Exec, embedded NUL metadata, missing required single parameter,
  failed realpath, unconsumed input argv, and argv capacity overflow reject;
- no mount, loop, sysfs, process, signal, cgroup, namespace, or device action.

