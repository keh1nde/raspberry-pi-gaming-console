# lib/

Global library code. Anything here should be
usable by `src/kernel/` and future userspace code without
modification, which is why it lives in its own tree instead of nested under
`src/kernel/`.

## Layout

```
lib/
  include/
    lib/
      <headers>.h
  src/
    <module>/
      <sources>.cpp
```

Headers live under `include/lib/` (not flat) so the include convention
matches the kernel's: `#include "lib/foo.h"`, the same shape as
`#include "kernel/foo.h"`.

## Build

Wired into the root `Makefile`: `.cpp`/`.S`/`.c` sources under `lib/src/`
are discovered the same way as `src/` and built to `build/lib/...`;
`-Ilib/include` is on every compile line (kernel and lib alike), so no
per-file setup is needed for our own code — just add files and rebuild.

## Vendored third-party code

`lib/src/fatfs/` + `lib/include/lib/fatfs/` — FatFs R0.16 (ChaN,
elm-chan.org), vendored unmodified except `ffconf.h`'s option values (that
file is meant to be edited per-port; see FatFs's own docs). Plain C, unlike
the rest of this tree — compiled via `$(CC)`/`CFLAGS`, not `$(CXX)`.
FatFs's own sources use unqualified `#include "ff.h"` (upstream's own
flat-directory convention); rather than edit vendored files to match our
`#include "lib/foo.h"` convention, the Makefile adds an extra include path
scoped to just `build/lib/fatfs/%.o`. License: 1-clause BSD-style
(`lib/src/fatfs/LICENSE.txt`), compatible with this project's GPL-3.0-only.

The disk I/O port FatFs requires (`diskio.c`'s five functions) is *not*
vendored — it's kernel-specific glue calling into this board's SD driver,
so it lives at `src/kernel/fatfs_retarget/diskio.c` instead, the same
reasoning as `libc_retarget.cpp`. Currently stub bodies (SD driver doesn't
exist yet).

`lib/src/circle/usb/` + `lib/include/lib/circle/usb/` — a subset of
[Circle](https://github.com/rsta2/circle) (R. Stange)'s USB stack, vendored
unmodified, for Phase 3's USB HID work (see `p-docs/CLAUDE.md`). Only the
"requisite" slice is vendored, not Circle's full `lib/usb/`: the xHCI host
controller driver, generic USB core (function/endpoint/request/device/
standard-hub), the HID base class, and two input drivers (boot-protocol
keyboard + Xbox One gamepad) — chosen since an Xbox One controller is the
only gamepad on hand right now. Explicitly *not* vendored: every other
vendor gamepad driver (PS3/PS4/switch-pro/Xbox 360), mouse, touchscreen —
none needed yet, and adding one later is cheap once the base class exists.
Also excluded: `xhcisharedmemallocator.cpp` (Circle's own DMA-buffer
allocator — this project already has one, see `src/kernel/dma/dma.cpp`) and
`usbdevicefactory.cpp` (Circle's device-to-driver dispatch table, which
matches every gamepad Circle ships — needs a rewrite scoped to just
keyboard + Xbox One, not a straight port). Both of those files' *headers*
(`xhcisharedmemallocator.h`, `usbdevicefactory.h`) are still vendored, since
other already-vendored files reference their declarations — only the two
`.cpp` implementations are ours to write. Also vendored: `usb.h`/`usbhid.h`
(shared USB/HID type and constant definitions), `usbconfigparser.{h,cpp}`
(configuration-descriptor parsing, part of generic USB enumeration),
`usbstring.{h,cpp}` (USB string-descriptor helper), and the header-only
abstract bases `usbcontroller.h`/`usbhcirootport.h`/`usbhub.h` — all
transitively required by the driver files above but missed by the first
vendoring pass (a directory-listing summary turned out to be incomplete;
recheck via the GitHub API rather than a fetched/summarized listing if
vendoring more of this tree later).

Circle's own framework headers this code depends on (`circle/device.h`,
`circle/timer.h`, `circle/logger.h`, `circle/spinlock.h`, etc.) are *not*
vendored — those get a **compatibility shim** instead: new headers under
`lib/include/lib/circle/` (outside `usb/`) that define Circle's expected
classes with matching names/signatures, implemented against this kernel's
own primitives (`timer.h`, `spinlock.h`, `io.h`) rather than porting
Circle's own implementations — porting those would pull in their own
further Circle-internal dependencies rather than terminating the
dependency graph. Same shape as `diskio.c` above, just a larger surface.
Written incrementally, driven by the compiler's actual missing-symbol
errors rather than guessed upfront — see the shim files themselves for
what's covered so far. License: GPL-3.0 (`lib/src/circle/LICENSE.txt`),
compatible with this project's GPL-3.0-only. Uses Circle's own
upstream-qualified `#include <circle/usb/foo.h>` convention unmodified in
every vendored file (see the Makefile's `build/lib/circle/%.o` rule for how
that's resolved without editing the vendored files, same reasoning as
FatFs's flat-include scoping above).
