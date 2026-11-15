#ifndef SIGNAL_EXPERIMENTAL_MODULE_TEMPLATE_H
#define SIGNAL_EXPERIMENTAL_MODULE_TEMPLATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SIGNAL_MODULE_STATUS_OK = 0,
    SIGNAL_MODULE_STATUS_INVALID_ARGUMENT,
    SIGNAL_MODULE_STATUS_NOT_INITIALIZED,
    SIGNAL_MODULE_STATUS_IO_ERROR,
    SIGNAL_MODULE_STATUS_TIMEOUT
} signal_module_status_t;

typedef signal_module_status_t (*signal_module_transfer_fn)(
    void *user_context,
    const uint8_t *tx_data,
    uint8_t *rx_data,
    size_t length
);

typedef struct {
    signal_module_transfer_fn transfer;
    void *user_context;
} signal_module_ops_t;

typedef struct {
    uint32_t timeout_ms;
} signal_module_config_t;

typedef struct {
    signal_module_ops_t ops;
    signal_module_config_t config;
    bool initialized;
} signal_module_t;

signal_module_status_t signal_module_init(
    signal_module_t *module,
    const signal_module_config_t *config,
    const signal_module_ops_t *ops
);

void signal_module_deinit(signal_module_t *module);

#ifdef __cplusplus
}
#endif

#endif
