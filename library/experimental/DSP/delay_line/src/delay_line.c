#include "delay_line.h"

static DelayLine_Status DelayLine_CheckInitialized(const DelayLine *line)
{
    if (line == NULL) {
        return DELAY_LINE_ERROR_NULL;
    }

    if ((line->initialized == 0) ||
        (line->storage == NULL) ||
        (line->storage_capacity == 0U)) {
        return DELAY_LINE_ERROR_NOT_INITIALIZED;
    }

    return DELAY_LINE_OK;
}

DelayLine_Status DelayLine_Init(
    DelayLine *line,
    float *storage,
    size_t storage_capacity,
    size_t delay_samples)
{
    size_t index;

    if ((line == NULL) || (storage == NULL)) {
        return DELAY_LINE_ERROR_NULL;
    }

    if (storage_capacity == 0U) {
        return DELAY_LINE_ERROR_SIZE;
    }

    if (delay_samples >= storage_capacity) {
        return DELAY_LINE_ERROR_RANGE;
    }

    for (index = 0U; index < storage_capacity; ++index) {
        storage[index] = 0.0f;
    }

    line->storage = storage;
    line->storage_capacity = storage_capacity;
    line->max_delay_samples = storage_capacity - 1U;
    line->delay_samples = delay_samples;
    line->write_index = 0U;
    line->initialized = 1;

    return DELAY_LINE_OK;
}

DelayLine_Status DelayLine_Process(
    DelayLine *line,
    const float *input,
    float *output,
    size_t sample_count)
{
    DelayLine_Status status = DelayLine_CheckInitialized(line);
    size_t index;

    if (status != DELAY_LINE_OK) {
        return status;
    }

    if (sample_count == 0U) {
        return DELAY_LINE_OK;
    }

    if ((input == NULL) || (output == NULL)) {
        return DELAY_LINE_ERROR_NULL;
    }

    for (index = 0U; index < sample_count; ++index) {
        float current_sample = input[index];

        if (line->delay_samples == 0U) {
            output[index] = current_sample;
        } else {
            size_t read_index =
                (line->write_index + line->storage_capacity -
                 line->delay_samples) % line->storage_capacity;
            output[index] = line->storage[read_index];
        }

        line->storage[line->write_index] = current_sample;
        line->write_index =
            (line->write_index + 1U) % line->storage_capacity;
    }

    return DELAY_LINE_OK;
}

DelayLine_Status DelayLine_SetDelay(
    DelayLine *line,
    size_t delay_samples)
{
    DelayLine_Status status = DelayLine_CheckInitialized(line);

    if (status != DELAY_LINE_OK) {
        return status;
    }

    if (delay_samples > line->max_delay_samples) {
        return DELAY_LINE_ERROR_RANGE;
    }

    line->delay_samples = delay_samples;
    return DELAY_LINE_OK;
}

DelayLine_Status DelayLine_Reset(DelayLine *line)
{
    DelayLine_Status status = DelayLine_CheckInitialized(line);
    size_t index;

    if (status != DELAY_LINE_OK) {
        return status;
    }

    for (index = 0U; index < line->storage_capacity; ++index) {
        line->storage[index] = 0.0f;
    }
    line->write_index = 0U;

    return DELAY_LINE_OK;
}
