#include "contest_app.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    contest_app_mode_outcome_t basic_outcome;
    contest_app_mode_outcome_t advanced_outcome;
    contest_app_mode_t active_mode;
    unsigned start_basic_calls;
    unsigned start_advanced_calls;
    unsigned service_calls;
    unsigned report_start_calls;
    unsigned report_poll_calls;
    unsigned safe_stop_calls;
    bool mode_done;
    bool report_done;
    bool fail_report_start;
} fake_app_port_t;

static contest_app_port_status_t fake_start_mode(
    void *user_context,
    contest_app_mode_t mode
)
{
    fake_app_port_t *port = (fake_app_port_t *)user_context;
    port->active_mode = mode;
    port->mode_done = false;
    if (mode == CONTEST_APP_MODE_BASIC) {
        port->start_basic_calls++;
    } else {
        port->start_advanced_calls++;
    }
    return CONTEST_APP_PORT_OK;
}

static contest_app_port_status_t fake_service_mode(
    void *user_context,
    contest_app_mode_t mode,
    uint32_t now_ms
)
{
    fake_app_port_t *port = (fake_app_port_t *)user_context;
    (void)mode;
    (void)now_ms;
    port->service_calls++;
    port->mode_done = true;
    return CONTEST_APP_PORT_OK;
}

static contest_app_port_status_t fake_is_mode_done(
    void *user_context,
    contest_app_mode_t mode,
    bool *done
)
{
    fake_app_port_t *port = (fake_app_port_t *)user_context;
    (void)mode;
    *done = port->mode_done;
    return CONTEST_APP_PORT_OK;
}

static contest_app_port_status_t fake_get_mode_result(
    void *user_context,
    contest_app_mode_t mode,
    contest_app_mode_outcome_t *outcome,
    uint32_t *mode_result_code
)
{
    fake_app_port_t *port = (fake_app_port_t *)user_context;
    *outcome = (mode == CONTEST_APP_MODE_BASIC)
        ? port->basic_outcome
        : port->advanced_outcome;
    *mode_result_code = (mode == CONTEST_APP_MODE_BASIC) ? 10U : 20U;
    return CONTEST_APP_PORT_OK;
}

static contest_app_port_status_t fake_start_report(
    void *user_context,
    const contest_app_result_t *result
)
{
    fake_app_port_t *port = (fake_app_port_t *)user_context;
    (void)result;
    port->report_start_calls++;
    port->report_done = false;
    return port->fail_report_start
        ? CONTEST_APP_PORT_ERROR
        : CONTEST_APP_PORT_OK;
}

static contest_app_port_status_t fake_poll_report(
    void *user_context,
    bool *done
)
{
    fake_app_port_t *port = (fake_app_port_t *)user_context;
    port->report_poll_calls++;
    port->report_done = true;
    *done = port->report_done;
    return CONTEST_APP_PORT_OK;
}

static void fake_safe_stop(void *user_context)
{
    fake_app_port_t *port = (fake_app_port_t *)user_context;
    port->safe_stop_calls++;
}

static contest_app_ops_t make_ops(fake_app_port_t *port)
{
    contest_app_ops_t ops;
    (void)memset(&ops, 0, sizeof(ops));
    ops.start_mode = fake_start_mode;
    ops.service_mode = fake_service_mode;
    ops.is_mode_done = fake_is_mode_done;
    ops.get_mode_result = fake_get_mode_result;
    ops.start_report = fake_start_report;
    ops.poll_report = fake_poll_report;
    ops.safe_stop = fake_safe_stop;
    ops.user_context = port;
    return ops;
}

static contest_app_config_t make_config(void)
{
    contest_app_config_t config;
    config.total_timeout_ms = 100U;
    config.fallback_on_advanced_failure = true;
    return config;
}

static void finish_report(contest_app_t *app, uint32_t now_ms)
{
    assert(contest_app_service(app, now_ms)
           == CONTEST_APP_STATUS_OK);
}

static void test_basic_success(void)
{
    fake_app_port_t port = {0};
    contest_app_t app;
    contest_app_result_t result;
    contest_app_request_t request = {CONTEST_APP_MODE_BASIC};
    contest_app_config_t config = make_config();
    contest_app_ops_t ops = make_ops(&port);

    assert(contest_app_init(&app, &config, &ops)
           == CONTEST_APP_STATUS_OK);
    assert(contest_app_start(&app, &request, 0U)
           == CONTEST_APP_STATUS_OK);
    assert(contest_app_service(&app, 1U)
           == CONTEST_APP_STATUS_IN_PROGRESS);
    finish_report(&app, 2U);
    assert(contest_app_get_result(&app, &result)
           == CONTEST_APP_STATUS_OK);
    assert(result.reason == CONTEST_APP_REASON_SUCCESS);
    assert(result.completed_mode == CONTEST_APP_MODE_BASIC);
    assert(!result.used_fallback);
    assert(port.safe_stop_calls == 1U);
}

static void test_advanced_success(void)
{
    fake_app_port_t port = {0};
    contest_app_t app;
    contest_app_result_t result;
    contest_app_request_t request = {CONTEST_APP_MODE_ADVANCED};
    contest_app_config_t config = make_config();
    contest_app_ops_t ops = make_ops(&port);

    assert(contest_app_init(&app, &config, &ops)
           == CONTEST_APP_STATUS_OK);
    assert(contest_app_start(&app, &request, 10U)
           == CONTEST_APP_STATUS_OK);
    assert(contest_app_service(&app, 11U)
           == CONTEST_APP_STATUS_IN_PROGRESS);
    finish_report(&app, 12U);
    assert(contest_app_get_result(&app, &result)
           == CONTEST_APP_STATUS_OK);
    assert(result.reason == CONTEST_APP_REASON_SUCCESS);
    assert(result.completed_mode == CONTEST_APP_MODE_ADVANCED);
    assert(port.start_advanced_calls == 1U);
}

static void test_advanced_fallback_success(void)
{
    fake_app_port_t port = {0};
    contest_app_t app;
    contest_app_result_t result;
    contest_app_request_t request = {CONTEST_APP_MODE_ADVANCED};
    contest_app_config_t config = make_config();
    contest_app_ops_t ops = make_ops(&port);

    port.advanced_outcome = CONTEST_APP_MODE_OUTCOME_FAILED;
    assert(contest_app_init(&app, &config, &ops)
           == CONTEST_APP_STATUS_OK);
    assert(contest_app_start(&app, &request, 0U)
           == CONTEST_APP_STATUS_OK);
    assert(contest_app_service(&app, 1U)
           == CONTEST_APP_STATUS_IN_PROGRESS);
    assert(contest_app_get_state(&app)
           == CONTEST_APP_STATE_RUNNING_FALLBACK);
    assert(contest_app_service(&app, 2U)
           == CONTEST_APP_STATUS_IN_PROGRESS);
    finish_report(&app, 3U);
    assert(contest_app_get_result(&app, &result)
           == CONTEST_APP_STATUS_OK);
    assert(result.reason == CONTEST_APP_REASON_FALLBACK_SUCCESS);
    assert(result.completed_mode == CONTEST_APP_MODE_BASIC);
    assert(result.used_fallback);
    assert(port.start_basic_calls == 1U);
    assert(port.safe_stop_calls == 2U);
}

static void test_failure_without_fallback(void)
{
    fake_app_port_t port = {0};
    contest_app_t app;
    contest_app_result_t result;
    contest_app_request_t request = {CONTEST_APP_MODE_ADVANCED};
    contest_app_config_t config = make_config();
    contest_app_ops_t ops = make_ops(&port);

    config.fallback_on_advanced_failure = false;
    port.advanced_outcome = CONTEST_APP_MODE_OUTCOME_FAILED;
    assert(contest_app_init(&app, &config, &ops)
           == CONTEST_APP_STATUS_OK);
    assert(contest_app_start(&app, &request, 0U)
           == CONTEST_APP_STATUS_OK);
    assert(contest_app_service(&app, 1U)
           == CONTEST_APP_STATUS_IN_PROGRESS);
    finish_report(&app, 2U);
    assert(contest_app_get_result(&app, &result)
           == CONTEST_APP_STATUS_OK);
    assert(result.reason == CONTEST_APP_REASON_MODE_FAILED);
    assert(contest_app_get_state(&app) == CONTEST_APP_STATE_FAULT);
}

static void test_timeout_abort_and_busy(void)
{
    fake_app_port_t port = {0};
    contest_app_t app;
    contest_app_result_t result;
    contest_app_request_t request = {CONTEST_APP_MODE_BASIC};
    contest_app_config_t config = make_config();
    contest_app_ops_t ops = make_ops(&port);

    config.total_timeout_ms = 10U;
    assert(contest_app_init(&app, &config, &ops)
           == CONTEST_APP_STATUS_OK);
    assert(contest_app_start(&app, &request, UINT32_MAX - 5U)
           == CONTEST_APP_STATUS_OK);
    assert(contest_app_start(&app, &request, 0U)
           == CONTEST_APP_STATUS_BUSY);
    assert(contest_app_service(&app, 4U)
           == CONTEST_APP_STATUS_OK);
    assert(contest_app_get_result(&app, &result)
           == CONTEST_APP_STATUS_OK);
    assert(result.reason == CONTEST_APP_REASON_TIMEOUT);

    assert(contest_app_start(&app, &request, 20U)
           == CONTEST_APP_STATUS_OK);
    assert(contest_app_abort(&app) == CONTEST_APP_STATUS_OK);
    assert(contest_app_get_result(&app, &result)
           == CONTEST_APP_STATUS_OK);
    assert(result.reason == CONTEST_APP_REASON_ABORTED);
}

static void test_invalid_and_report_failure(void)
{
    fake_app_port_t port = {0};
    contest_app_t app;
    contest_app_result_t result;
    contest_app_request_t request = {CONTEST_APP_MODE_BASIC};
    contest_app_config_t config = make_config();
    contest_app_ops_t ops = make_ops(&port);

    assert(contest_app_init(NULL, &config, &ops)
           == CONTEST_APP_STATUS_INVALID_ARGUMENT);
    ops.safe_stop = NULL;
    assert(contest_app_init(&app, &config, &ops)
           == CONTEST_APP_STATUS_INVALID_ARGUMENT);
    ops = make_ops(&port);
    request.requested_mode = (contest_app_mode_t)99;
    assert(contest_app_init(&app, &config, &ops)
           == CONTEST_APP_STATUS_OK);
    assert(contest_app_start(&app, &request, 0U)
           == CONTEST_APP_STATUS_INVALID_ARGUMENT);
    request.requested_mode = CONTEST_APP_MODE_BASIC;
    port.fail_report_start = true;
    assert(contest_app_start(&app, &request, 0U)
           == CONTEST_APP_STATUS_OK);
    assert(contest_app_service(&app, 1U)
           == CONTEST_APP_STATUS_IO_ERROR);
    assert(contest_app_get_state(&app) == CONTEST_APP_STATE_FAULT);
    assert(contest_app_get_result(&app, &result)
           == CONTEST_APP_STATUS_OK);
    assert(result.reason == CONTEST_APP_REASON_REPORT_FAILED);
}

int main(void)
{
    test_basic_success();
    test_advanced_success();
    test_advanced_fallback_success();
    test_failure_without_fallback();
    test_timeout_abort_and_busy();
    test_invalid_and_report_failure();
    (void)puts("contest_app tests passed");
    return 0;
}
