# neondst

neondst is a Nintendo DS ROM file packing and extraction tool specialized for
ROM hack development. It allows a ROM hacking project to store modified or
custom files in one directory while keeping the "clean" files from the base
game in a separate directory. It supports not only building a ROM from the
different source directories, but also applying changes from the ROM back into
the source directories, e.g. after editing the ROM file with a external tool.

The core idea behind neondst was originally implemented in [Ndst](https://github.com/Gota7/Ndst),
but it relied on an external build system in a way that made the build process
quite slow. In contrast, neondst is fully self-contained and much faster.
While Ndst included certain conversion utilities for specific formats,
neondst assumes that the user (or a build system calling it) wants to
use more specialized tools for such conversions.

## Commands

### `neondst init <clean ROM>`

Initializes a new neondst project in the current directory. Files from the clean
ROM are extracted to `clean/raw` and other relevant directories are created.

### `neondst build [<output ROM>]`

Builds the ROM from the files in the source directories, which are prioritized
in this order:

1. `modified/final`
2. `modified/to-be-compressed`
3. `modified/base`
4. `clean/raw`

If an overlay file in `modified/to-be-compressed` is newer than the corresponding file
in `modified/final`, or if the file in `modified/final` doesn't exist yet,
it is compressed and stored in `modified/final`.
(Note: the compression feature is experimental and only supported for overlays.)
After updating the overlay tables, FNT, FAT and the ROM header, they're stored
in `modified/final`.

### `neondst apply [<input ROM>]`

Applies changes from the ROM to `modified/base`. Files in `modified/to-be-compressed`
or `modified/final` are skipped. If `modified/to-be-compressed` exists, it will be deleted.

### `neondst status [<ROM>]`

Enumerates files that differ between the ROM and the source directories, including ones
that only exist in one or the other. This effectively allows the user to check if
anything will get overwritten or deleted when running `neondst build` or `neondst apply`.

### `neondst decompress <files...>`

Decompresses files from `clean/raw` to `clean/decompressed`. File paths should be relative to
`clean/raw`. At the moment, this is only supported for overlays and the ARM9 binary (arm9.bin).

## Configuration

Certain options can be specified in a `.neondst` file in the directory containing the
neondst project:
- `output <filename>`: Specifies the default output ROM filename, making it optional for the commands
- `ovt_repl_flag <byte>`: Sets the overlay table replacement flag (0xff by default)
- `pad <byte>` if specified, pads the ROM to a power of two with the given byte.
- `arm9_entry <address>`: Sets the ARM9 entry address
- `arm7_entry <address>`: Sets the ARM7 entry address
- `arm9_load <address>`: Sets the ARM9 load address
- `arm7_load <address>`: Sets the ARM7 load address

All numerical values are expected to be in hexadecimal with no prefix.
Lines starting with `#` are ignored. See the [example config file](.neondst).

## Licensing

This project includes code from [NCPatcher](https://github.com/TheGameratorT/NCPatcher)
and [Fireflower](https://github.com/MammaMiaTeam/Fireflower).
NCPatcher is licensed under the GPL version 3, just like this project,
but Fireflower was originally licensed under the BSD 3-clause license.
The full text of its original license can be found in the
[LICENSE.BSD-3-Clause.md](LICENSE.BSD-3-Clause.md) file.

The files [blz.hpp](source/blz.hpp) and [blz.cpp](source/blz.cpp) are from NCPatcher,
and following files in this project are originally from Fireflower:

- [crc.h](source/crc.h) (see the original version [here](https://github.com/MammaMiaTeam/Fireflower/blob/master/nds-build/crc.h))
- [common.h](source/commmon.h) (see the original version [here](https://github.com/MammaMiaTeam/Fireflower/blob/master/common.h))
- [extract.cpp](source/extract.cpp) (originally [nds-extract.cpp](https://github.com/MammaMiaTeam/Fireflower/blob/master/nds-extract/nds-extract.cpp))
- [pack.cpp](source/pack.cpp) (originally [nds-build.cpp](https://github.com/MammaMiaTeam/Fireflower/blob/master/nds-build/nds-build.cpp))

All modifications to these files are licensed under the GPL version 3. You can find the full text of the GPL version 3 license in the [LICENSE](LICENSE) file.
