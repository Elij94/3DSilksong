// 3DSilksong: entry point. For now (step 0) it only proves the setup: New 3DS check, 804 MHz, top screen at
// 800x240, the game data file mounted from the SD card. The room prototype (step 2) plugs in where marked.
#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <sys/stat.h>
#include "version.h"

// the game data: SD:/3ds/3dsilksong/3dsilksong.romfs mounted as romfs:/, else the program's own RomFS
// (a .3dsx / CIA built with the data inside). Returns where it came from, or NULL when neither has it.
static const char *data_mount(void) {
    struct stat st;
    Handle fh;
    if (R_SUCCEEDED(FSUSER_OpenFileDirectly(&fh, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""),
                                            fsMakePath(PATH_ASCII, SS_DATADIR SS_DATAFILE), FS_OPEN_READ, 0))) {
        if (R_SUCCEEDED(romfsMountFromFile(fh, 0, "romfs"))) {
            if (stat("romfs:/version.txt", &st) == 0) return "SD:" SS_DATADIR SS_DATAFILE;
            romfsUnmount("romfs");
        } else FSFILE_Close(fh);
    }
    if (R_SUCCEEDED(romfsMountSelf("romfs"))) {
        if (stat("romfs:/version.txt", &st) == 0) return "the program's own RomFS";
        romfsUnmount("romfs");
    }
    return NULL;
}

int main(void) {
    bool n3ds = false;
    APT_CheckNew3DS(&n3ds);
    if (n3ds) osSetSpeedupEnable(true);           // 804 MHz, the extra L2 cache

    gfxInitDefault();
    gfxSetWide(n3ds);                             // 800x240 top screen (citro2d keeps 400x240 coordinates)
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    C3D_RenderTarget *top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    consoleInit(GFX_BOTTOM, NULL);

    const char *data = data_mount();
    printf("3DSilksong %s\n\n", SS_VERSION);
    printf("New 3DS: %s\n", n3ds ? "yes" : "NO (this port needs a New 3DS)");
    printf("Game data: %s\n", data ? data : "not found");
    if (!data) printf("\nBuild it from your own copy of the\ngame (tools/build.sh) and copy it to\nSD:" SS_DATADIR SS_DATAFILE "\n");
    printf("\nSTART: quit\n");

    u32 clear = C2D_Color32(0x12, 0x0c, 0x10, 0xff);
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        // step 2: room_update() / hero_update() go here
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TargetClear(top, clear);
        C2D_SceneBegin(top);
        // step 2: room_draw() / hero_draw() go here
        C3D_FrameEnd(0);
    }

    if (data) romfsUnmount("romfs");
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
