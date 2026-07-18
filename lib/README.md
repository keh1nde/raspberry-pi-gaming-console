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

Wired into the root `Makefile`: sources under `lib/src/` are discovered the
same way as `src/` and built to `build/lib/...`; `-Ilib/include` is on every
compile line (kernel and lib alike), so no per-file setup is needed — just
add files and rebuild.

Currently empty (`.gitkeep` placeholders in `include/lib/` and `src/` keep
the otherwise-empty directories tracked by git until real files land).
