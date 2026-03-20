#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


#include "valve_fn/valve_process.h"
#include "test_process.h"



// limit test ------------------------------------------------------------

// // use the same pins from menuconfig
// #define CLOSE_LIMIT_PIN_A   CONFIG_CLOSE_LIMIT_PIN_A
// #define CLOSE_LIMIT_PIN_B   CONFIG_CLOSE_LIMIT_PIN_B
// #define OPEN_LIMIT_PIN_A    CONFIG_OPEN_LIMIT_PIN_A
// #define OPEN_LIMIT_PIN_B    CONFIG_OPEN_LIMIT_PIN_B

static const char *LIMIT_TAG = "LIMIT_TEST";

// // create limit switch objects
// static LimitSwitches closeLimit = { CLOSE_LIMIT_PIN_A, CLOSE_LIMIT_PIN_B };
// static LimitSwitches openLimit  = { OPEN_LIMIT_PIN_A,  OPEN_LIMIT_PIN_B };


static void limit_monitor_task(void *arg)
{
    while (1)
    {
        int closeState = limit_switch_click(&closeLimit);
        int openState  = limit_switch_click(&openLimit);

        ESP_LOGI(LIMIT_TAG,
                 "CloseLimit: %d | OpenLimit: %d",
                 closeState,
                 openState);

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void start_limit_test(void)
{
    limit_switch_init(&closeLimit);
    limit_switch_init(&openLimit);

    ESP_LOGI(LIMIT_TAG, "Limit switches initialized");

    xTaskCreate(
        limit_monitor_task,
        "limit_monitor_task",
        2048,
        NULL,
        5,
        NULL
    );
}


// motor test ------------------------------------------------------------

// #define MOTOR_EN_PIN  CONFIG_MOTOR_EN_PIN
// #define MOTOR_IN1_PIN CONFIG_MOTOR_IN1_PIN
// #define MOTOR_IN2_PIN CONFIG_MOTOR_IN2_PIN

// static Motor testMotor = { MOTOR_IN1_PIN, MOTOR_IN2_PIN, MOTOR_EN_PIN, 0 };

static const char *MOTOR_TAG = "MOTOR_TEST";


static void motor_test_task(void *arg)
{
    while (1)
    {
        ESP_LOGI(MOTOR_TAG, "Motor clockwise - 220");
        motor_run_clk(&motor, 220);
        vTaskDelay(pdMS_TO_TICKS(3000));

        ESP_LOGI(MOTOR_TAG, "Motor stop");
        motor_stop(&motor);
        vTaskDelay(pdMS_TO_TICKS(2000));

        ESP_LOGI(MOTOR_TAG, "Motor anticlockwise -255 ");
        motor_run_aclck(&motor, 255);
        vTaskDelay(pdMS_TO_TICKS(3000));

        ESP_LOGI(MOTOR_TAG, "Motor stop");
        motor_stop(&motor);
        vTaskDelay(pdMS_TO_TICKS(2000));

        ESP_LOGI(MOTOR_TAG, "Motor clockwise - 255");
        motor_run_clk(&motor, 255);
        vTaskDelay(pdMS_TO_TICKS(3000));

        ESP_LOGI(MOTOR_TAG, "Motor stop");
        motor_stop(&motor);
        vTaskDelay(pdMS_TO_TICKS(4000));
    }
}


void start_motor_test(void)
{
    motor_init(&motor);

    ESP_LOGI(MOTOR_TAG, "Motor initialized");

    xTaskCreate(
        motor_test_task,
        "motor_test_task",
        2048,
        NULL,
        5,
        NULL
    );
}


// valve process test -----------------------------------------------

static const char *PROCESS_TEST_TAG = "VALVE_TOGGLE";

int valve_toggle(void)
{
    int errorCode = 0;

    // First, check limit switches
    int closeState = limit_switch_click(&closeLimit);  // 10=clicked, 1=not clicked
    int openState  = limit_switch_click(&openLimit);

    ESP_LOGI(PROCESS_TEST_TAG, "Limit States - Close: %d, Open: %d", closeState, openState);

    // Check for errors first
    if (closeState == 0 || openState == 0) {
        ESP_LOGE(PROCESS_TEST_TAG, "Limit switch error detected");
        return 901;  // error code for limit switch failure
    }


    ESP_LOGI(PROCESS_TEST_TAG, "Valve is closed. Opening...");
    errorCode = motor_open();
    if (errorCode != 0) {
        ESP_LOGE(PROCESS_TEST_TAG, "Failed to open valve. Error: %d", errorCode);
        return 902;
    }

    vTaskDelay(pdMS_TO_TICKS(2000));

    // Valve is open → close it

    ESP_LOGI(PROCESS_TEST_TAG, "Valve is open. Closing...");
    errorCode = motor_close();
    if (errorCode != 0) {
        ESP_LOGE(PROCESS_TEST_TAG, "Failed to close valve. Error: %d", errorCode);
        return 903;
    }

    vTaskDelay(pdMS_TO_TICKS(2000));

    return 0;
}

static void valve_toggle_task(void *arg)
{
    while (1)
    {
        int result = valve_toggle();

        if (result != 0) {
            ESP_LOGW(PROCESS_TEST_TAG, "Valve toggle returned: %d", result);
        }

    }
}

void start_valve_toggle_test(void)
{
    init_valve_system();

    xTaskCreate(
        valve_toggle_task,
        "valve_toggle_task",
        4096,
        NULL,
        5,
        NULL
    );
}