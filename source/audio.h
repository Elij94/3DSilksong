#pragma once
#include "common.h"

// music (channel 0) and ambience (channel 1), streamed from music/<track>.pcm (audio.c)
bool audio_init(void);
// switches channel c to this track (fading); the same track keeps playing; "" fades out to silence
void audio_play(int c, const char *track);
void audio_update(float dt);
void audio_exit(void);
bool audio_on(void);
extern float g_music_vol, g_sfx_vol;   // 0..1, the options (music applies at the next fade step)
const char *audio_track(int c);
// a sound effect by the guid of its AudioClip (the FSMs' sounds); vol 0..1, pan -1 left .. 1 right
void sfx_play(const char *guid, float vol, float pan);
void sfx_play_name(const char *name, float vol, float pan);   // sfx/<name>.pcm
int sfx_loop_name(const char *name, float vol);   // played on a loop: its voice, -1 if none; sfx_stop ends it
void sfx_stop(int voice);
void sfx_stop_all(void);                          // a room change: no loop should carry into the next one
void sfx_set_vol(int voice, float vol);           // sets its volume (0..1) right away, no stop at zero
void sfx_fade(int voice, float to, float time);   // ramps its volume (0..1) over 'time' s; stops it once silent
// a caller-defined tag (the FSM node that started it, say) kept on a voice so a later action can find it back
void sfx_set_owner(int voice, int owner);
int sfx_find_owner(int owner);                    // the still-playing voice tagged with this owner, -1 if none
