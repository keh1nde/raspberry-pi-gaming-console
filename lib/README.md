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
