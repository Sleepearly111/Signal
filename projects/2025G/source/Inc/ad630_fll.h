#ifndef AD630_FLL_H
#define AD630_FLL_H

#include <stdint.h>

/* =========================================================================
 * AD630-based FLL v2 for 2025G 发挥(2) replay frequency stabilisation.
 *
 * Main-loop-serviced AD630 sampling, using measured window elapsed time.
 * SEARCH: beat-frequency zero-crossing detection + perturb-and-observe.
 * HOLD: freeze the best DDS trim and monitor fresh beat periods for unlock.
 *
 * Pure frequency lock — no phase register writes, no capture sweep.
 * ========================================================================= */

/* FLL run state (for debug) */
typedef enum {
    FLL_STATE_IDLE = 0,
    FLL_STATE_SEARCH,
    FLL_STATE_CALIBRATE,
    FLL_STATE_HOLD
} AD630_FLL_State;

/* Initialise FLL.  ch = ADS8688 channel AD630 OUT is connected to (0-7). */
void AD630_FLL_Init(uint8_t ads8688_channel);

/* Call from the main loop every iteration.  The accumulator is non-blocking
 * apart from one ADS8688 manual-channel transaction per accepted sample. */
void AD630_FLL_Accumulate(void);

/* Call from main loop.  Processes a 100-sample window when full.  Returns:
 *   0 — no action needed
 *   1 — freq adjustment pending (read with AD630_FLL_GetAdjustment)
 *   2 — FLL state changed (debug)  */
uint8_t AD630_FLL_Service(void);

/* Returns a signed DDS trim command.  One unit is 0.1 Hz in replay. */
int AD630_FLL_GetAdjustment(void);

/* Clear pending adjustment after applying it. */
void AD630_FLL_AdjustmentApplied(void);

/* Clear beat-period and current-window history after a forced frequency step. */
void AD630_FLL_ResetBeatTracking(void);

/* Stop FLL and reset state.  Call when replay stops. */
void AD630_FLL_Stop(void);

/* Return current FLL state (for debug output). */
AD630_FLL_State AD630_FLL_GetState(void);

/* Return 1 if FLL hold is valid (frequency stable). */
uint8_t AD630_FLL_IsHoldValid(void);

#endif /* AD630_FLL_H */
