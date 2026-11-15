#include "module.h"

#include <string.h>

signal_module_status_t signal_module_init(
    signal_module_t *module,
    const signal_module_config_t *config,
    const signal_module_ops_t *ops
)
{
    if ((module == NULL) || (config == NULL) || (ops == NULL)) {
        return SIGNAL_MODULE_STATUS_INVALID_ARGUMENT;
    }

    if (ops->transfer == NULL) {
        return SIGNAL_MODULE_STATUS_INVALID_ARGUMENT;
    }

    module->ops = *ops;
    module->config = *config;
    module->initialized = true;

    return SIGNAL_MODULE_STATUS_OK;
}

void signal_module_deinit(signal_module_t *module)
{
    if (module == NULL) {
        return;
    }

    (void)memset(module, 0, sizeof(*module));
}
