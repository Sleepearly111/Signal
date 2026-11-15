#include "calibration_record.h"

#include <float.h>
#include <stdbool.h>
#include <string.h>

static void put_u16(uint8_t *buffer, uint16_t value)
{
    buffer[0] = (uint8_t)(value & 0xFFU);
    buffer[1] = (uint8_t)((value >> 8U) & 0xFFU);
}

static void put_u32(uint8_t *buffer, uint32_t value)
{
    buffer[0] = (uint8_t)(value & 0xFFU);
    buffer[1] = (uint8_t)((value >> 8U) & 0xFFU);
    buffer[2] = (uint8_t)((value >> 16U) & 0xFFU);
    buffer[3] = (uint8_t)((value >> 24U) & 0xFFU);
}

static uint16_t get_u16(const uint8_t *buffer)
{
    return (uint16_t)((uint16_t)buffer[0] |
                      ((uint16_t)buffer[1] << 8U));
}

static uint32_t get_u32(const uint8_t *buffer)
{
    return (uint32_t)buffer[0] |
           ((uint32_t)buffer[1] << 8U) |
           ((uint32_t)buffer[2] << 16U) |
           ((uint32_t)buffer[3] << 24U);
}

static bool supported_float(void)
{
    return (sizeof(float) == sizeof(uint32_t)) &&
           (FLT_RADIX == 2) &&
           (FLT_MANT_DIG == 24);
}

static bool finite_value(float value)
{
    return (value <= FLT_MAX) && (value >= -FLT_MAX);
}

static void put_float(uint8_t *buffer, float value)
{
    uint32_t bits;
    (void)memcpy(&bits, &value, sizeof(bits));
    put_u32(buffer, bits);
}

static float get_float(const uint8_t *buffer)
{
    const uint32_t bits = get_u32(buffer);
    float value;
    (void)memcpy(&value, &bits, sizeof(value));
    return value;
}

size_t calibration_record_encoded_size(uint16_t point_count)
{
    if ((point_count == 0U) ||
        (point_count > CALIBRATION_RECORD_MAX_POINTS)) {
        return 0U;
    }
    return CALIBRATION_RECORD_HEADER_SIZE +
           ((size_t)point_count * CALIBRATION_RECORD_POINT_SIZE) +
           sizeof(uint32_t);
}

uint32_t calibration_record_crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = UINT32_C(0xFFFFFFFF);
    size_t index;
    unsigned bit;

    if ((data == NULL) && (length != 0U)) {
        return 0U;
    }
    for (index = 0U; index < length; ++index) {
        crc ^= data[index];
        for (bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask =
                (uint32_t)(0U - (uint32_t)(crc & 1U));
            crc = (crc >> 1U) ^
                  (UINT32_C(0xEDB88320) & mask);
        }
    }
    return ~crc;
}

calibration_record_status_t calibration_record_validate(
    const calibration_record_t *record
)
{
    uint16_t index;

    if (record == NULL) {
        return CALIBRATION_RECORD_INVALID_ARGUMENT;
    }
    if (!supported_float()) {
        return CALIBRATION_RECORD_UNSUPPORTED_PLATFORM;
    }
    if (record->schema_version != CALIBRATION_RECORD_SCHEMA_VERSION) {
        return CALIBRATION_RECORD_BAD_VERSION;
    }
    if ((record->kind <= CALIBRATION_KIND_UNSPECIFIED) ||
        (record->kind > CALIBRATION_KIND_ADC_LINEARIZATION) ||
        (record->point_count == 0U) ||
        (record->point_count > CALIBRATION_RECORD_MAX_POINTS)) {
        return CALIBRATION_RECORD_INVALID_DATA;
    }
    for (index = 0U; index < record->point_count; ++index) {
        if (!finite_value(record->points[index].x) ||
            !finite_value(record->points[index].y) ||
            ((index > 0U) &&
             (record->points[index].x <=
              record->points[index - 1U].x))) {
            return CALIBRATION_RECORD_INVALID_DATA;
        }
    }
    return CALIBRATION_RECORD_OK;
}

calibration_record_status_t calibration_record_encode(
    const calibration_record_t *record,
    uint8_t *buffer,
    size_t capacity,
    size_t *written
)
{
    calibration_record_status_t status;
    size_t required;
    uint16_t index;
    uint32_t crc;

    if ((record == NULL) || (buffer == NULL) || (written == NULL)) {
        return CALIBRATION_RECORD_INVALID_ARGUMENT;
    }
    status = calibration_record_validate(record);
    if (status != CALIBRATION_RECORD_OK) {
        return status;
    }
    required = calibration_record_encoded_size(record->point_count);
    if (capacity < required) {
        return CALIBRATION_RECORD_BUFFER_TOO_SMALL;
    }

    buffer[0] = (uint8_t)'S';
    buffer[1] = (uint8_t)'C';
    buffer[2] = (uint8_t)'A';
    buffer[3] = (uint8_t)'L';
    put_u16(&buffer[4], record->schema_version);
    put_u16(&buffer[6], (uint16_t)record->kind);
    put_u32(&buffer[8], record->board_id);
    put_u16(&buffer[12], record->point_count);
    put_u16(&buffer[14], 0U);
    for (index = 0U; index < record->point_count; ++index) {
        const size_t offset = CALIBRATION_RECORD_HEADER_SIZE +
                              ((size_t)index *
                               CALIBRATION_RECORD_POINT_SIZE);
        put_float(&buffer[offset], record->points[index].x);
        put_float(&buffer[offset + 4U], record->points[index].y);
    }
    crc = calibration_record_crc32(buffer, required - sizeof(uint32_t));
    put_u32(&buffer[required - sizeof(uint32_t)], crc);
    *written = required;
    return CALIBRATION_RECORD_OK;
}

calibration_record_status_t calibration_record_decode(
    const uint8_t *buffer,
    size_t length,
    calibration_record_t *record
)
{
    uint16_t point_count;
    size_t required;
    uint16_t index;
    uint32_t expected_crc;

    if ((buffer == NULL) || (record == NULL)) {
        return CALIBRATION_RECORD_INVALID_ARGUMENT;
    }
    if (!supported_float()) {
        return CALIBRATION_RECORD_UNSUPPORTED_PLATFORM;
    }
    if (length < (CALIBRATION_RECORD_HEADER_SIZE + sizeof(uint32_t))) {
        return CALIBRATION_RECORD_BAD_LENGTH;
    }
    if ((buffer[0] != (uint8_t)'S') ||
        (buffer[1] != (uint8_t)'C') ||
        (buffer[2] != (uint8_t)'A') ||
        (buffer[3] != (uint8_t)'L')) {
        return CALIBRATION_RECORD_BAD_MAGIC;
    }
    if (get_u16(&buffer[4]) != CALIBRATION_RECORD_SCHEMA_VERSION) {
        return CALIBRATION_RECORD_BAD_VERSION;
    }
    point_count = get_u16(&buffer[12]);
    required = calibration_record_encoded_size(point_count);
    if ((required == 0U) || (length != required)) {
        return CALIBRATION_RECORD_BAD_LENGTH;
    }
    expected_crc = get_u32(&buffer[length - sizeof(uint32_t)]);
    if (calibration_record_crc32(
            buffer,
            length - sizeof(uint32_t)
        ) != expected_crc) {
        return CALIBRATION_RECORD_BAD_CRC;
    }

    (void)memset(record, 0, sizeof(*record));
    record->schema_version = get_u16(&buffer[4]);
    record->kind = (calibration_kind_t)get_u16(&buffer[6]);
    record->board_id = get_u32(&buffer[8]);
    record->point_count = point_count;
    for (index = 0U; index < point_count; ++index) {
        const size_t offset = CALIBRATION_RECORD_HEADER_SIZE +
                              ((size_t)index *
                               CALIBRATION_RECORD_POINT_SIZE);
        record->points[index].x = get_float(&buffer[offset]);
        record->points[index].y = get_float(&buffer[offset + 4U]);
    }
    return calibration_record_validate(record);
}
