#ifndef FPGA_MOCK_SUITE_H
#define FPGA_MOCK_SUITE_H

#include <stddef.h>

#include "fpga_mock_adapter.h"

typedef struct
{
  const char *name;
  FpgaMockSignalConfig signal;
} FpgaMockTestCase;

size_t FpgaMockSuite_Count(void);
const FpgaMockTestCase *FpgaMockSuite_Get(size_t index);

#endif
