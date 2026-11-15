#ifndef DELAY_LINE_H
#define DELAY_LINE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DELAY_LINE_OK = 0,
    DELAY_LINE_ERROR_NULL,
    DELAY_LINE_ERROR_SIZE,
    DELAY_LINE_ERROR_RANGE,
    DELAY_LINE_ERROR_NOT_INITIALIZED
} DelayLine_Status;

typedef struct {
    float *storage;
    size_t storage_capacity;
    size_t max_delay_samples;
    size_t delay_samples;
    size_t write_index;
    int initialized;
} DelayLine;

/*
 * Initialize a streaming integer-sample delay line.
 *
 * storage is owned by the caller and is cleared during initialization.
 * The maximum supported delay is storage_capacity - 1 samples.
 */
DelayLine_Status DelayLine_Init(
    DelayLine *line,
    float *storage,
    size_t storage_capacity,
    size_t delay_samples);

/*
 * Process sample_count consecutive samples while preserving history between
 * calls. input and output may point to the same buffer.
 */
DelayLine_Status DelayLine_Process(
    DelayLine *line,
    const float *input,
    float *output,
    size_t sample_count);

/* Change the integer delay without clearing buffered history. */
DelayLine_Status DelayLine_SetDelay(
    DelayLine *line,
    size_t delay_samples);

/* Clear history while preserving the configured delay and storage binding. */
DelayLine_Status DelayLine_Reset(DelayLine *line);

#ifdef __cplusplus
}
#endif

#endif /* DELAY_LINE_H */
