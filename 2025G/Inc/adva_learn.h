#ifndef ADVA_LEARN_H
#define ADVA_LEARN_H

#include <stdint.h>

/* Filter types required by the problem statement. */
#define FILTER_LOW_PASS    1U
#define FILTER_HIGH_PASS   2U
#define FILTER_BAND_PASS   3U
#define FILTER_BAND_STOP   4U

/* AD637 magnitude sweep: 1 kHz .. 60 kHz, 200 Hz step. */
#define SWEEP_FREQ_START_HZ  1000U
#define SWEEP_FREQ_END_HZ   60000U
#define SWEEP_FREQ_STEP_HZ    200U
#define SWEEP_POINT_COUNT \
    (((SWEEP_FREQ_END_HZ - SWEEP_FREQ_START_HZ) / SWEEP_FREQ_STEP_HZ) + 1U)

typedef struct {
    uint8_t valid;
    uint8_t filter_type;
    float gain;
    float f0_hz;
    float q;
} AdvLearnModel;

void AdvLearn_Init(void);
void AdvLearn_Start(void);
void AdvLearn_Service(void);
void AdvLearn_ContinueAfterRewire(void);
uint8_t AdvLearn_IsWaitingForRewire(void);
uint8_t AdvLearn_IsDone(void);
uint8_t AdvLearn_GetFilterType(void);
const AdvLearnModel *AdvLearn_GetModel(void);

/* Returns measured magnitude and fitted phase at an arbitrary frequency. */
uint8_t AdvLearn_GetResponse(float freq_hz, float *magnitude, float *phase_rad);

#endif /* ADVA_LEARN_H */
