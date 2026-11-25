#pragma once
// the release number, shown on the title screen (bump it with each build that goes out)
#define SS_VERSION "0.12.0"
// The game data is one RomFS image on the SD card (tools/mkromfs.py or mkromfs3ds: make data); the CIA / .3dsx hold
// only the code. Only the New 3DS family is targeted (124 MB of RAM, 804 MHz).
#define SS_DATADIR  "/3ds/3dsilksong/"
#define SS_DATAFILE "3dsilksong.romfs"
