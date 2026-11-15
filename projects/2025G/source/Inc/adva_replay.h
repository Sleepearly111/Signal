#ifndef ADVA_REPLAY_H
#define ADVA_REPLAY_H

#include <stdint.h>

#define WAVE_SINE      1U
#define WAVE_TRIANGLE  2U
#define WAVE_SQUARE    3U

/* PB1 ADC1 timer/DMA capture -> FFT -> learned RLC response per harmonic
 * -> Fourier synthesis -> PA5 DAC CH2 timer/DMA circular output. */
void AdvReplay_Init(void);
void AdvReplay_Start(void);
void AdvReplay_Service(void);
void AdvReplay_Stop(void);
uint8_t AdvReplay_IsRunning(void);
uint32_t AdvReplay_GetDacStartCount(void);

/* Adjust DAC output frequency through the 32-bit software-DDS phase step.
 * Each call accumulates 0.1 Hz offset.  No DAC restart and no TIM6 ARR jump.
 * For AD630 FLL frequency correction. */
void AdvReplay_AdjustFrequency(int delta);

#endif /* ADVA_REPLAY_H */
