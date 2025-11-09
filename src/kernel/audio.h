#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

// Audio system initialization
void audio_init(void);

// Tone generation
void audio_play_tone(uint16_t frequency, uint16_t duration_ms);
void audio_stop_tone(void);

// Boot sound sequence
void audio_boot_sequence(void);

// Volume control
void audio_set_volume(uint8_t level); // 0-100

#endif // AUDIO_H
