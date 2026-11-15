#ifndef CALIBRATION_RECORD_H
#define CALIBRATION_RECORD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CALIBRATION_RECORD_SCHEMA_VERSION 1U
#define CALIBRATION_RECORD_MAX_POINTS 32U
#define CALIBRATION_RECORD_HEADER_SIZE 16U
#define CALIBRATION_RECORD_POINT_SIZE 8U

typedef enum {
    CALIBRATION_RECORD_OK = 0,
    CALIBRATION_RECORD_INVALID_ARGUMENT,
    CALIBRATION_RECORD_UNSUPPORTED_PLATFORM,
    CALIBRATION_RECORD_BUFFER_TOO_SMALL,
    CALIBRATION_RECORD_BAD_MAGIC,
    CALIBRATION_RECORD_BAD_VERSION,
    CALIBRATION_RECORD_BAD_LENGTH,
    CALIBRATION_RECORD_BAD_CRC,
    CALIBRATION_RECORD_INVALID_DATA
} calibration_record_status_t;

typedef enum {
    CALIBRATION_KIND_UNSPECIFIED = 0,
    CALIBRATION_KIND_GAIN_VOLTAGE = 1,
    CALIBRATION_KIND_FREQUENCY_AMPLITUDE = 2,
    CALIBRATION_KIND_ADC_LINEARIZATION = 3
} calibration_kind_t;

typedef struct {
    float x;
    float y;
} calibration_point_t;

typedef struct {
    uint16_t schema_version;
    calibration_kind_t kind;
    uint32_t board_id;
    uint16_t point_count;
    calibration_point_t points[CALIBRATION_RECORD_MAX_POINTS];
} calibration_record_t;

size_t calibration_record_encoded_size(uint16_t point_count);
uint32_t calibration_record_crc32(const uint8_t *data, size_t length);
calibration_record_status_t calibration_record_validate(
    const calibration_record_t *record
);
calibration_record_status_t calibration_record_encode(
    const calibration_record_t *record,
    uint8_t *buffer,
    size_t capacity,
    size_t *written
);
calibration_record_status_t calibration_record_decode(
    const uint8_t *buffer,
    size_t length,
    calibration_record_t *record
);

#ifdef __cplusplus
}
#endif

#endif
