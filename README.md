# raspberry-pi-gaming-console

> Working codename. A retro gaming console OS for the Raspberry Pi 5, built on a
> bare-metal AArch64 kernel ported from the [kehinde-kernel](https://github.com/keh1nde/kehinde-kernel) project.

This project is in early bring-up. Documentation will follow when project nears it's first milestone.

Includes [FatFs](http://elm-chan.org/fsw/ff/) (R0.16) by ChaN, vendored under
`lib/src/fatfs/` and `lib/include/lib/fatfs/` — see
`lib/src/fatfs/LICENSE.txt` for its license.

Includes USB/xHCI host-controller and HID input-device code adapted from
[Circle](https://github.com/rsta2/circle) by R. Stange, vendored under
`lib/src/circle/` and `lib/include/lib/circle/` — see
`lib/src/circle/LICENSE.txt` for its license.
