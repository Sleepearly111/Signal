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

#endif /* ADVA_REPLAY_H */
