These portable decoder files are copied without modification from bb_epaper
2.1.9 (`src/Group5.h` and `src/g5dec.inl`). Their original GPL-3.0-or-later
copyright and license notices are retained. The repository's GPL license
applies. Only host builds compile this copy; firmware uses its existing
bb_epaper dependency. This lets the host display decode the exact same
compressed font assets without introducing hardware-driver dependencies.
