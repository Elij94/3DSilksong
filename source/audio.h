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
