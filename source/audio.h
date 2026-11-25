#pragma once
#include "common.h"

// music (channel 0) and ambience (channel 1), streamed from music/<track>.pcm (audio.c)
bool audio_init(void);
// switches channel c to this track (fading); the same track keeps playing; "" fades out to silence
void audio_play(int c, const char *track);
void audio_update(float dt);
void audio_exit(void);
bool audio_on(void);
const char *audio_track(int c);
// a sound effect by the guid of its AudioClip (the FSMs' sounds); vol 0..1, pan -1 left .. 1 right
void sfx_play(const char *guid, float vol, float pan);
void sfx_play_name(const char *name, float vol, float pan);   // sfx/<name>.pcm
int sfx_loop_name(const char *name, float vol);   // played on a loop: its voice, -1 if none; sfx_stop ends it
void sfx_stop(int voice);
