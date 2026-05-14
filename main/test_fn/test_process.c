#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


#include "valve_fn/valve_process.h"
#include "test_fn/test_process.h"





// ---------------------- pot test -------------------------

static const char *POT_TAG = "POT_TEST";

static void pot_sensor_read_task(void *arg)
{
    while (1)
    {
        int potValue = pot_read_filtered(&potentiometer);
        float current_angle = pot_to_angle(&potentiometer, potValue);

        ESP_LOGI(POT_TAG, "potentiometer adc read: %d, angle: %.2f", potValue, current_angle);

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void start_pot_test(void)
{
    pot_sensor_init(&potentiometer);

    ESP_LOGI(POT_TAG, "Pot sensor initialized");

    xTaskCreate(
        pot_sensor_read_task,
        "pot_sensor_read_task",
        2048,
        NULL,
        5,
        NULL
    );
}



// ----------------------- motor test ------------------------------------------------------------

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


// --------------------- valve set pos ------------------------------------
static const char *POS_TAG = "POS_TEST";


static void motor_pos_task(void *arg)
{
    valve_test();

    while (1) {
        motor_set_angle(0);

        vTaskDelay(pdMS_TO_TICKS(5000));

        motor_set_angle(45);

        vTaskDelay(pdMS_TO_TICKS(5000));

        motor_set_angle(90);

        vTaskDelay(pdMS_TO_TICKS(5000));
    }

}

void start_motor_pos(void)
{
    pot_sensor_init(&potentiometer);
    motor_init(&motor);

    ESP_LOGI(POS_TAG, "Motor pos initialized");

    xTaskCreate(
        motor_pos_task,
        "motor_pos_task",
        2048,
        NULL,
        5,
        NULL
    );
}



// ---------------------- valve process test -----------------------------------------------

static const char *PROCESS_TEST_TAG = "VALVE_TOGGLE";

int valve_toggle(void)
{
    int errorCode = 0;


    // ESP_LOGI(PROCESS_TEST_TAG, "Valve is closed. Opening...");
    // errorCode = motor_open();
    // if (errorCode != 0) {
    //     ESP_LOGE(PROCESS_TEST_TAG, "Failed to open valve. Error: %d", errorCode);
    //     return 902;
    // }

    // vTaskDelay(pdMS_TO_TICKS(2000));

    // // Valve is open → close it

    // ESP_LOGI(PROCESS_TEST_TAG, "Valve is open. Closing...");
    // errorCode = motor_close();
    // if (errorCode != 0) {
    //     ESP_LOGE(PROCESS_TEST_TAG, "Failed to close valve. Error: %d", errorCode);
    //     return 903;
    // }

    // vTaskDelay(pdMS_TO_TICKS(2000));

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

