# neondst

> [!WARNING]
> This is still WIP and can't be used in the intended way quite yet.

This is a tool for packing and extracting Nintendo DS ROMs. The goal is to support ROM hack development setups with an extracted "clean ROM" folder containing all of the original files and binaries from a given game, as well as a "patch" folder for files and binaries that should replace their original counterparts. This idea was previously implemented in [Ndst](https://github.com/Gota7/Ndst), but it turned out to be too slow to be actually practical. `neondst` is an attempt to do the same thing better and faster.

## Licensing

This project includes code from [Fireflower](https://github.com/MammaMiaTeam/Fireflower), which was originally licensed under the BSD 3-clause license. The full text of the original license can be found in the [LICENSE.BSD-3-Clause.md](LICENSE.BSD-3-Clause.md) file.

The following files in this project are originally from Fireflower:

- [crc.h](source/crc.h) (see the original version [here](https://github.com/MammaMiaTeam/Fireflower/blob/master/nds-build/crc.h))
- [common.h](source/commmon.h) (see the original version [here](https://github.com/MammaMiaTeam/Fireflower/blob/master/common.h))
- [extract.cpp](source/extract.cpp) (originally [nds-extract.cpp](https://github.com/MammaMiaTeam/Fireflower/blob/master/nds-extract/nds-extract.cpp))
- [pack.cpp](source/pack.cpp) (originally [nds-build.cpp](https://github.com/MammaMiaTeam/Fireflower/blob/master/nds-build/nds-build.cpp))

All modifications to these files are licensed under the GPL version 3. You can find the full text of the GPL version 3 license in the [LICENSE](LICENSE) file.
