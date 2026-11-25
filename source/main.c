// 3DSilksong: entry point. New 3DS at 804 MHz, top screen at 400x240, the game data file mounted from the SD card,
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
#include "audio.h"

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

// a thread that notices when the game loop stops advancing (testing: with autotest on) and logs where it is
static volatile u32 s_beat;
static volatile bool s_wd_quit;
static void watchdog(void *arg) {
    (void)arg;
    u32 last = 0, same = 0;
    while (!s_wd_quit) {
        svcSleepThread(250000000LL);
        if (s_beat == last) {
            if (++same == 8) {
                char msg[96];
                snprintf(msg, sizeof msg, "WATCHDOG: no frame for 2 s, in %s", (const char *)g_where);
                autotest_note(msg);
            }
        } else same = 0;
        last = s_beat;
    }
}

// testing: a CPU exception writes where it happened (pc, lr, address) to SD:/3ds/3dsilksong/crash.txt
static u8 s_exc_stack[0x1000] __attribute__((aligned(8)));
static void on_exception(ERRF_ExceptionInfo *e, CpuRegisters *r) {
    FILE *f = fopen("sdmc:/3ds/3dsilksong/crash.txt", "w");
    if (f) {
        fprintf(f, "type %d pc %08lx lr %08lx sp %08lx far %08lx fsr %08lx where %s\n", (int)e->type,
                (unsigned long)r->pc, (unsigned long)r->lr, (unsigned long)r->sp, (unsigned long)e->far,
                (unsigned long)e->fsr, (const char *)g_where);
        for (int i = 0; i < 13; i++) fprintf(f, "r%d %08lx\n", i, (unsigned long)r->r[i]);
        fclose(f);
    }
    for (;;) svcSleepThread(1000000000LL);
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
    // the 800x240 mode stays off: on a New 3DS XL (and in Azahar) the screen kept showing 400 columns of the 800,
    // the left half zoomed in, even set up as devkitPro's wide_mode_3d example does. SD:/3ds/3dsilksong/wide
    // turns it on, for trying it again.
    FILE *ww = fopen("sdmc:/3ds/3dsilksong/wide", "r");
    bool wide = n3ds && ww;
    if (ww) fclose(ww);
    gfxSetWide(wide);
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE * 2);
    C2D_Init(8192);
    C2D_Prepare();
    C3D_RenderTarget *top = C3D_RenderTargetCreate(240, wide ? 800 : 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    C3D_RenderTargetSetOutput(top, GFX_TOP, GFX_LEFT,
                              GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
                              GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                              GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
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

    // the view: 240 px for ~19 units of height
    View *v = &g_view;
    v->sw = top->frameBuf.height;   // the top screen's framebuffer is rotated: its height is the width
    v->sh = top->frameBuf.width;
    v->sy = 12.5f;
    v->sx = v->sy * v->sw / 400.0f;   // the same view, with twice the pixels across in 800 mode

    v->hw = v->sw * 0.5f / v->sx;
    v->hh = v->sh * 0.5f / v->sy;
    room_cache_init();
    audio_init();
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

    Thread wd = autotest_on() ? threadCreate(watchdog, NULL, 8 * 1024, 0x18, 1, false) : NULL;
    if (!wd && autotest_on()) wd = threadCreate(watchdog, NULL, 8 * 1024, 0x18, -2, false);
    if (autotest_on()) threadOnException(on_exception, s_exc_stack + sizeof s_exc_stack, WRITE_DATA_TO_HANDLER_STACK);
    u64 last = osGetTime();
    float fps = 60, ms = 0;
    u32 clear = C2D_Color32(0x08, 0x06, 0x08, 0xff);
    Input in = {0};
    while (aptMainLoop()) {
        hidScanInput();
        in.down = hidKeysDown();
        in.held = hidKeysHeld();
        in.up = hidKeysUp();

        u64 now = osGetTime();
        float dt = (now - last) / 1000.0f;
        last = now;
        if (dt > 0) fps += (1.0f / dt - fps) * 0.05f;
        // the game moves by whole screen refreshes: the millisecond clock jitters by 1 ms around 16.7, and that
        // uneven step showed as a stutter even at 60 fps. A late frame counts as 2 refreshes, at most 3.
        int vb = (int)(dt * 60.0f + 0.5f);
        dt = (vb < 1 ? 1 : vb > 3 ? 3 : vb) / 60.0f;
        if (autotest_on()) {
            char sc[48];
            float wx, wy;
            int r = autotest_step(dt, &in, sc, &wx, &wy);
            if (r == 2) break;
            if (r == 1) game_warp(sc, wx, wy);
        }

        s_beat++;
        g_where = "FrameBegin";
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        u64 t0 = svcGetSystemTick();
        g_view.frame++;
        game_update(&in, dt);
        if (game_quit()) { C3D_FrameEnd(0); break; }
        g_where = "audio_update";
        audio_update(dt);
        g_where = "draw";
        C2D_TargetClear(top, clear);
        C2D_SceneBegin(top);
        C2D_Prepare();
        gfx_blend_reset();
        game_draw();
        ms += ((svcGetSystemTick() - t0) / (float)(SYSCLOCK_ARM11 / 1000) - ms) * 0.1f;
        g_where = "FrameEnd";
        C3D_FrameEnd(0);
        g_where = "status";
        if ((g_view.frame & 15) == 0) game_status(fps, ms);
    }

    s_wd_quit = true;
    if (wd) { threadJoin(wd, U64_MAX); threadFree(wd); }
    game_save();
    autotest_end();
    game_end();                      // closes the room's files before the data goes away
    room_cache_exit();
    audio_exit();
    romfsUnmount("romfs");
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
