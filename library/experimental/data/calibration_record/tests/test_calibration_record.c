#include "calibration_record.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static calibration_record_t make_record(void)
{
    calibration_record_t record;
    (void)memset(&record, 0, sizeof(record));
    record.schema_version = CALIBRATION_RECORD_SCHEMA_VERSION;
    record.kind = CALIBRATION_KIND_FREQUENCY_AMPLITUDE;
    record.board_id = UINT32_C(0x12345678);
    record.point_count = 3U;
    record.points[0].x = 500.0F;
    record.points[0].y = 1.01F;
    record.points[1].x = 1000.0F;
    record.points[1].y = 1.02F;
    record.points[2].x = 2000.0F;
    record.points[2].y = 1.04F;
    return record;
}

static void test_round_trip(void)
{
    calibration_record_t source = make_record();
    calibration_record_t decoded;
    uint8_t buffer[128];
    size_t written = 0U;

    assert(calibration_record_encode(
               &source,
               buffer,
               sizeof(buffer),
               &written
           ) == CALIBRATION_RECORD_OK);
    assert(written == calibration_record_encoded_size(3U));
    assert(calibration_record_decode(buffer, written, &decoded)
           == CALIBRATION_RECORD_OK);
    assert(decoded.board_id == source.board_id);
    assert(decoded.kind == source.kind);
    assert(decoded.point_count == 3U);
    assert(decoded.points[2].x == 2000.0F);
    assert(decoded.points[2].y == 1.04F);
}

static void test_corruption_version_and_magic(void)
{
    calibration_record_t source = make_record();
    calibration_record_t decoded;
    uint8_t buffer[128];
    size_t written = 0U;

    assert(calibration_record_encode(
               &source,
               buffer,
               sizeof(buffer),
               &written
           ) == CALIBRATION_RECORD_OK);
    buffer[20] ^= 0x01U;
    assert(calibration_record_decode(buffer, written, &decoded)
           == CALIBRATION_RECORD_BAD_CRC);
    buffer[20] ^= 0x01U;
    buffer[4] = 2U;
    assert(calibration_record_decode(buffer, written, &decoded)
           == CALIBRATION_RECORD_BAD_VERSION);
    buffer[4] = 1U;
    buffer[0] = (uint8_t)'X';
    assert(calibration_record_decode(buffer, written, &decoded)
           == CALIBRATION_RECORD_BAD_MAGIC);
}

static void test_invalid_table_and_capacity(void)
{
    calibration_record_t source = make_record();
    uint8_t buffer[16];
    size_t written = 0U;

    source.points[1].x = source.points[0].x;
    assert(calibration_record_validate(&source)
           == CALIBRATION_RECORD_INVALID_DATA);
    source = make_record();
    assert(calibration_record_encode(
               &source,
               buffer,
               sizeof(buffer),
               &written
           ) == CALIBRATION_RECORD_BUFFER_TOO_SMALL);
    source.schema_version = 2U;
    assert(calibration_record_validate(&source)
           == CALIBRATION_RECORD_BAD_VERSION);
}

int main(void)
{
    test_round_trip();
    test_corruption_version_and_magic();
    test_invalid_table_and_capacity();
    (void)puts("calibration_record tests passed");
    return 0;
}
