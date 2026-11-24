// 3DSilksong: entry point. New 3DS at 804 MHz, top screen at 800x240, the game data file mounted from the SD card,
// then the game loop (game.c) on the top screen and its status on the bottom one.
#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <sys/stat.h>
#include "version.h"
#include "common.h"
#include "room.h"
#include "game.h"
#include "autotest.h"

// Linear memory (what the GPU reads: textures, command buffers) gets 48 MB of the application's memory; the rest is
// the normal heap. Scenery tiles and sprite pages are cached in it and dropped when it runs out.
u32 __ctru_linear_heap_size = 48 * 1024 * 1024;

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

static void wait_start(void) {
    printf("\nSTART: quit\n");
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        gspWaitForVBlank();
    }
}

int main(void) {
    bool n3ds = false;
    APT_CheckNew3DS(&n3ds);
    if (n3ds) osSetSpeedupEnable(true);           // 804 MHz, the extra L2 cache

    gfxInitDefault();
    // 800x240 top screen on the New 3DS; SD:/3ds/3dsilksong/nowide turns it off (emulators that don't do the mode)
    struct stat nw;
    gfxSetWide(n3ds && stat("sdmc:" SS_DATADIR "nowide", &nw) != 0);
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE * 2);
    C2D_Init(8192);
    C2D_Prepare();
    C3D_RenderTarget *top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    consoleInit(GFX_BOTTOM, NULL);

    const char *data = data_mount();
    printf("3DSilksong %s\n\n", SS_VERSION);
    if (!n3ds) printf("This port is made for the New 3DS:\nit will be slow here.\n\n");
    if (!data) {
        printf("Game data: not found\n\nBuild it from your own copy of the\ngame (tools/build.sh) and copy it to\n"
               "SD:" SS_DATADIR SS_DATAFILE "\n");
        wait_start();
        C2D_Fini(); C3D_Fini(); gfxExit();
        return 0;
    }
    printf("Game data: %s\nLoading...\n", data);

    // the view: 240 px for ~17 units of height, as the game's camera shows at 16:9
    View *v = &g_view;
    v->sw = top->frameBuf.height;   // the top screen's framebuffer is rotated: its height is the width
    v->sh = top->frameBuf.width;
    v->sy = 14.0f;
    v->sx = v->sy * v->sw / 400.0f;  // 800 px in the same width as 400: pixels half as wide
    v->hw = v->sw * 0.5f / v->sx;
    v->hh = v->sh * 0.5f / v->sy;
    room_cache_init();
    autotest_init();

    // a first frame so the loading happens with the GPU set up
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    bool ok = game_start();
    C2D_TargetClear(top, C2D_Color32(0, 0, 0, 255));
    C2D_SceneBegin(top);
    C3D_FrameEnd(0);
    if (!ok) {
        printf("\nNo room could be loaded.\nIs the data file complete?\n");
        wait_start();
        romfsUnmount("romfs");
        C2D_Fini(); C3D_Fini(); gfxExit();
        return 0;
    }
    consoleClear();

    u64 last = osGetTime();
    float fps = 60, ms = 0;
    u32 clear = C2D_Color32(0x08, 0x06, 0x08, 0xff);
    Input in = {0};
    while (aptMainLoop()) {
        hidScanInput();
        in.down = hidKeysDown();
        in.held = hidKeysHeld();
        in.up = hidKeysUp();
        if (in.down & KEY_START) break;

        u64 now = osGetTime();
        float dt = (now - last) / 1000.0f;
        last = now;
        if (dt > 0) fps += (1.0f / dt - fps) * 0.05f;
        dt = clampf(dt, 1.0f / 120, 1.0f / 30);    // slow frames slow the game down rather than tunnel through walls
        if (autotest_on()) {
            char sc[48];
            float wx, wy;
            int r = autotest_step(dt, &in, sc, &wx, &wy);
            if (r == 2) break;
            if (r == 1) game_warp(sc, wx, wy);
        }

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        u64 t0 = svcGetSystemTick();
        g_view.frame++;
        game_update(&in, dt);
        C2D_TargetClear(top, clear);
        C2D_SceneBegin(top);
        C2D_Prepare();
        gfx_blend_reset();
        game_draw();
        ms += ((svcGetSystemTick() - t0) / (float)(SYSCLOCK_ARM11 / 1000) - ms) * 0.1f;
        C3D_FrameEnd(0);
        if ((g_view.frame & 15) == 0) game_status(fps, ms);
    }

    game_save();
    autotest_end();
    game_end();                      // closes the room's files before the data goes away
    romfsUnmount("romfs");
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
