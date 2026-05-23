/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "dma.h"
#include "i2c.h"
#include "usart.h"
#include "rtc.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "printf.h"

#include "hw390_flash.h"

#include "interval.h"

#include "gps_time.h"

#include "gps_nmea.h"
#include "lora_app.h"
#include "lora_config.h"
#include "lora_driver.h"
#include "lora_identity.h"
#include "lora_join.h"
#include "utils.h"

#include "sensor_common.h"
#include "sensor_hw390.h"
#include "sensor_ds18b20.h"
#include "sensor_ina219.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* Node configuration */
#define GPIO_USER_BTN_LETTER GPIOC
#define GPIO_USER_BTN_PIN    GPIO_PIN_13

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
uint32_t last_poll_time = 0;

/* Measurement timing */
uint32_t last_humidity_ms = 0;
uint32_t last_temperature_ms = 0;
uint32_t last_gps_ms = 0;
uint32_t last_status_ms = 0;

/* Force measurement flags */
extern volatile bool force_humidity;
extern volatile bool force_temperature;
extern volatile bool force_gps;
extern volatile bool force_status;

/* Enable/disable flags */
bool need_humidity = true;
bool need_temperature = true;
bool need_gps = true;
bool need_status = true;

/* Interval global variables */
extern uint32_t hum_period_s;
extern uint32_t tmp_period_s;
extern uint32_t gps_period_s;
extern uint32_t stt_period_s;

extern volatile bool system_sleep_mode;

/* Online status */
bool device_online = true;

/* Button debounce */
static uint32_t button_last_press_ms = 0;
#define BUTTON_DEBOUNCE_MS 200

/* Sensor busy flags (for checking in main.c) */
extern volatile bool hw390_busy;
extern volatile bool ds18b20_busy;
extern bool gps_busy;
extern bool ina219_busy;

/* Sensor result ready flags (for checking in main.c) */
extern volatile bool hw390_result_ready;
extern volatile bool ds18b20_result_ready;
extern bool gps_result_ready;
extern bool ina219_result_ready;

extern RTC_HandleTypeDef hrtc;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

bool period_due(uint32_t last_ms, uint32_t period_s);
bool check_need_humidity(void);
bool check_need_temperature(void);
bool check_need_gps(void);
bool check_need_status(void);
int measure_state(int16_t *out_rssi, float *out_snr, float *out_battery);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* Debug version - define to enable debug prints */
#define DEBUG_VERSION

#ifndef DEBUG_VERSION
#define DEBUG_UART_BUFFER_SIZE 1024
static uint8_t debug_uart_buffer[DEBUG_UART_BUFFER_SIZE];
static uint16_t debug_uart_buffer_head = 0;
static uint16_t debug_uart_buffer_tail = 0;

/* Function prototype */
void debug_uart_process(void);
#endif

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_LPUART1_UART_Init();
  MX_USART1_UART_Init();
  MX_ADC1_Init();
  MX_I2C1_Init();
  MX_USART3_UART_Init();
  MX_RTC_Init();
  /* USER CODE BEGIN 2 */


  HAL_Delay(200);
  PRINTF("Wait 2 secs...\r\n");
  HAL_Delay(2000);
  PRINTF("Sensor Node firmware starting...\r\n");
  PRINTF("You can start HW390 calibration - Press USER button! (%d ms)\r\n", HW390_CALIBRATION_BOOT_WINDOW_MS);

  // Калибровки HW390
  uint32_t boot_window_start = HAL_GetTick();
  bool do_calibrate = false;
  while (HAL_GetTick() - boot_window_start < HW390_CALIBRATION_BOOT_WINDOW_MS) {
      if (HAL_GPIO_ReadPin(GPIO_USER_BTN_LETTER, GPIO_USER_BTN_PIN) == GPIO_PIN_RESET) { // active LOW, т.е. 0=нажато, 1=отпущено
          do_calibrate = true;
          break;
      }
      HAL_Delay(10);
  }

  if (do_calibrate) {
      hw390_run_calibration();
      NVIC_SystemReset(); // программный reset
  }
  else {
      PRINTF("Without HW390 calibration\r\n");
  }




  /* Initialize sensors */
  hw390_init();
  ds18b20_init();
  ina219_init();

  /* Initialize LoRa app, id and join */
  lora_identity_init();
  lora_join_init();
  lora_app_init();

  /* Initialize GPS (but keep it powered off initially) */
  gps_test_init(&huart1);  /* Initialize UART and DMA */

  /* Initialize random number generator */
  srand(HAL_GetTick());

  PRINTF("Sensor Node initialized\r\n");
  PRINTF("[HW390] Dry-Wet values: min(wet)=%u, max(dry)=%u\r\n", hw390_raw_wet, hw390_raw_dry);
  hw390_power_on();
  HAL_Delay(500); // на стабилизацию
  HAL_ADC_Start(&hadc1);
  HAL_Delay(500); // на стабилизацию
  if (HAL_ADC_PollForConversion(&hadc1, 10000) == HAL_OK) {
      uint32_t raw = HAL_ADC_GetValue(&hadc1);
      HAL_ADC_Stop(&hadc1);
      float humidity = hw390_normalize(raw);
      PRINTF("[HW390] Check humidity: [RAW %ld] %.2f%%\r\n", raw, humidity);
  }
  else {
      PRINTF("[HW390] Check hum timeout\r\n");
  }

  // Инициализировать интервалы замеров (default or from FLASH)
  interval_init();

  // Time synchronizing
  gps_time_init();
  PRINTF("Waiting for GPS to set RTC...\r\n");
  if (gps_time_sync_blocking(&hrtc)) {
      PRINTF("RTC time synchronized by GPS!\r\n");

      char buf[32];
      rtc_iso8601_string(buf, sizeof(buf), &hrtc);
      PRINTF("Current time: %s\n", buf);
  }
  else {
      PRINTF("RTC time not synchronized (timeout).\r\n");
  }



  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* Call sensor tick functions - FSM (finite state machine) */
    lora_join_tick();
    // ina219_tick();
    if (!system_sleep_mode) {
        hw390_tick();
        ds18b20_tick();
        gps_tick();
    }

    /* LoRa TX pump and RX process (call every iteration) */
    lora_app_tx_pump();
    lora_app_rx_process();

#ifndef DEBUG_VERSION
    /* Process debug UART buffer (non-blocking) */
    debug_uart_process();
#endif

    /* Check USER button for force measurements (active low) */
    if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_RESET)
    {
      uint32_t now = HAL_GetTick();
      if (now - button_last_press_ms >= BUTTON_DEBOUNCE_MS) {
        button_last_press_ms = now;
        PRINTF("Button pressed - forcing all measurements\r\n");
        force_humidity = true;
        force_temperature = true;
        force_gps = true;
        force_status = true;
      }
    }

    const char *node_id = lora_identity_get_node_id(); // точно не NULL, ведь функция просто делает return указателя
    if (system_sleep_mode && !(force_humidity || force_temperature || force_gps || force_status)) {
    	PRINTF("\r\n[SLEEP] sleeping now...\r\n\r\n");
    }
    else {
        PRINTF("ready FSM: HW-%d DS-%d GPS-%d\r\n", hw390_result_ready, ds18b20_result_ready, gps_result_ready);

        if (strcmp(node_id, LORA_UNASSIGNED_NODE_ID) == 0) {
            PRINTF("Node_ID is default\r\n");
        }
        else {
            // can start sensors state machines

            /* HW390 humidity check result */
            if (hw390_result_ready) {
              sensor_reading_t reading;
              if (hw390_get_result(&reading) == 0 && reading.valid) {
                PRINTF("Humidity: %.2f%%\r\n", reading.value);
                lora_app_send_humidity(reading.value);
                last_humidity_ms = HAL_GetTick();
                force_humidity = false;
              } else {
                PRINTF("Humidity    measurement failed\r\n");
                lora_app_send_humidity(HW390_ERROR_VALUE);
                last_humidity_ms = HAL_GetTick();
                force_humidity = false;
              }
            }

            /* HW390 Humidity measurement */
            if (check_need_humidity())
            {
              PRINTF("Humidity measurement needed\r\n");

              if (!hw390_busy) {
                hw390_request_measurement();
              }
            }

            /* DS18B20 Temperature check result */
            if (ds18b20_result_ready) {
              sensor_reading_t reading;
              if (ds18b20_get_result(&reading) == 0 && reading.valid) {
                PRINTF("Temperature: %.2f*C\r\n", reading.value);
                lora_app_send_temperature(reading.value);
                last_temperature_ms = HAL_GetTick();
                force_temperature = false;
              } else {
                PRINTF("Temperature    measurement failed\r\n");
                lora_app_send_temperature(DS18B20_ERROR_VALUE);
                last_temperature_ms = HAL_GetTick();
                force_temperature = false;
              }
            }

            /* Temperature measurement */
            if (check_need_temperature())
            {
              PRINTF("Temperature measurement needed\r\n");

              if (!ds18b20_busy) {
                ds18b20_request_measurement();
              }
            }

            /* GPS check result */
            if (gps_result_ready) {
              float lat, lon;
              if (gps_get_result(&lat, &lon) == 0) {
                lora_app_send_gps(lat, lon);
                PRINTF("GPS %.6f %.6f\r\n", lat, lon);
                last_gps_ms = HAL_GetTick();
                force_gps = false;
              } else {
                PRINTF("GPS    collection failed\r\n");
                lora_app_send_gps(GPS_ERROR_LAT, GPS_ERROR_LON);
                last_gps_ms = HAL_GetTick();
                force_gps = false;
              }
            }

            /* GPS measurement */
            if (check_need_gps()) {
              PRINTF("GPS measurement needed\r\n");

              if (!gps_busy) {
                gps_request_measurement();
              }
            }

            /* State measurement */
            if (check_need_status())
            {
              PRINTF("State measurement needed\r\n");

              int16_t rssi;
              float snr;
              float battery;
              if (measure_state(&rssi, &snr, &battery) == 0)
              {
                lora_app_send_state(rssi, snr, battery, device_online);
              }
              else
              {
                PRINTF("State    measurement failed\r\n");
                lora_app_send_state(STATE_ERROR_RSSI, STATE_ERROR_SNR, STATE_ERROR_BATTERY, device_online);
              }

              last_status_ms = HAL_GetTick();
              force_status = false;
            }
        }
    } // end `if (!system_sleep_mode)`

    /* Log state machine states for debugging */
    static uint32_t last_log_ms = 0;
    if (HAL_GetTick() - last_log_ms >= 500) {  // раз в 500 мс
        PRINTF("States: HW390=%d DS18B20=%d GPS=%d\r\n",
           hw390_state, ds18b20_state, gps_state);
        last_log_ms = HAL_GetTick();
    }

    /* Minimal delay to prevent CPU spinning (1ms) */
    HAL_Delay(1);
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure LSE Drive Capability
  */
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_LSI
                              |RCC_OSCILLATORTYPE_LSE|RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = 0;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_10;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_MSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Enable MSI Auto calibration
  */
  HAL_RCCEx_EnableMSIPLLMode();
}

/* USER CODE BEGIN 4 */

#ifdef DEBUG_VERSION
/* Blocking PRINTF redirect */
int _write(int file, char *ptr, int len)
{
    if (file == STDOUT_FILENO || file == STDERR_FILENO) {
        HAL_UART_Transmit(&huart3, (uint8_t*)ptr, len, HAL_MAX_DELAY);
        return len;
    }
    return -1;
}
#else
/* Non-blocking PRINTF redirect using buffer */
int _write(int file, char *ptr, int len)
{
    if (file == STDOUT_FILENO || file == STDERR_FILENO)
    {
        /* Add to buffer if space available */
        for (int i = 0; i < len; i++) {
            uint16_t next_head = (debug_uart_buffer_head + 1) % DEBUG_UART_BUFFER_SIZE;
            if (next_head != debug_uart_buffer_tail) {
                debug_uart_buffer[debug_uart_buffer_head] = (uint8_t)ptr[i];
                debug_uart_buffer_head = next_head;
            }
        }
        return len;
    }
    return -1;
}

/* Process UART buffer - send data non-blocking */
// void debug_uart_process(void)
// {
//     if (debug_uart_buffer_head != debug_uart_buffer_tail) {
//         uint16_t next_tail = (debug_uart_buffer_tail + 1) % DEBUG_UART_BUFFER_SIZE;
//         uint8_t byte = debug_uart_buffer[debug_uart_buffer_tail];
        
//         if (HAL_UART_Transmit(&huart3, &byte, 1, 10) == HAL_OK) {
//             debug_uart_buffer_tail = next_tail;
//         }
//     }
// }

void debug_uart_process(void)
{
    #define TX_BATCH_SIZE 64  // Отправлять пачками по 64 байта
    
    if (debug_uart_buffer_head != debug_uart_buffer_tail) {
        uint16_t available = (debug_uart_buffer_head - debug_uart_buffer_tail + DEBUG_UART_BUFFER_SIZE) % DEBUG_UART_BUFFER_SIZE;
        uint16_t to_send = (available < TX_BATCH_SIZE) ? available : TX_BATCH_SIZE;
        
        // Временный буфер
        uint8_t temp[TX_BATCH_SIZE];
        for (uint16_t i = 0; i < to_send; i++) {
            uint16_t idx = (debug_uart_buffer_tail + i) % DEBUG_UART_BUFFER_SIZE;
            temp[i] = debug_uart_buffer[idx];
        }
        
        if (HAL_UART_Transmit(&huart3, temp, to_send, 50) == HAL_OK) {
            debug_uart_buffer_tail = (debug_uart_buffer_tail + to_send) % DEBUG_UART_BUFFER_SIZE;
        }
    }
}

#endif

/* Check if measurement period is due */
bool period_due(uint32_t last_ms, uint32_t period_s)
{
    if (period_s == 0) return false;
    uint32_t now = HAL_GetTick();
    return (now - last_ms) >= (period_s * 1000);
}

/* Check if humidity measurement is needed */
bool check_need_humidity(void)
{
    if (!need_humidity) return false;
    if (force_humidity) return true;
    return period_due(last_humidity_ms, hum_period_s);
}

/* Check if temperature measurement is needed */
bool check_need_temperature(void)
{
    if (!need_temperature) return false;
    if (force_temperature) return true;
    return period_due(last_temperature_ms, tmp_period_s);
}

/* Check if GPS measurement is needed */
bool check_need_gps(void)
{
    if (!need_gps) return false;
    if (force_gps) return true;
    return period_due(last_gps_ms, gps_period_s);
}

/* Check if status measurement is needed */
bool check_need_status(void)
{
    if (!need_status) return false;
    if (force_status) return true;
    return period_due(last_status_ms, stt_period_s);
}

/* Measure state: LoRa RSSI/SNR, battery percentage, online status */
/* Returns 0 on success, negative on error */
int measure_state(int16_t *out_rssi, float *out_snr, float *out_battery)
{
    if (!out_rssi || !out_snr || !out_battery) return -1;

    int8_t result = lora_driver_read_rssi_and_snr(out_rssi, out_snr);
    if (result == 0) {
        PRINTF("State: RSSI = %d dBm, SNR = %.2f dB\r\n", *out_rssi, *out_snr);
    }
    else {
       PRINTF("State: Failed to read RSSI and SNR - %d\r\n", result);
       *out_rssi = STATE_ERROR_RSSI;
       *out_snr = STATE_ERROR_SNR;

       /* Read LoRa RSSI only (ambient noise) */
       int16_t rssi = lora_driver_read_rssi(true);
       if (rssi == 0) {
           PRINTF("State: Failed to read RSSI\r\n");
           *out_rssi = STATE_ERROR_RSSI;
       } else {
           *out_rssi = rssi;
           PRINTF("State: RSSI = %d dBm\r\n", rssi);
       }
    }


    // /* SNR is not directly available via the simple RSSI command */
    // /* For now, we'll use a placeholder value */
    // /* no TO DO (already realised): Implement SNR reading if available via E22 registers */
    // *out_snr = 0;  /* Placeholder */

    /* Read battery voltage from INA219 */
    float voltage_mv = 0.0f;
    if (ina219_read_voltage(&voltage_mv) != 0)
    {
        PRINTF("State: Failed to read battery voltage\r\n");
        *out_battery = STATE_ERROR_BATTERY;
    }
    else
    {
        voltage_mv *= 1000.0f;
        if (voltage_mv < 0)
        {
            PRINTF("State: Invalid battery voltage\r\n");
            *out_battery = STATE_ERROR_BATTERY;
        }
        else
        {
            /* Calculate battery percentage */
            float percentage = 0.0f;
            if (voltage_mv >= BATTERY_MAX_VOLTAGE_MV) {
                percentage = 100.0f;
            }
            else if (voltage_mv <= BATTERY_MIN_VOLTAGE_MV) {
                percentage = 0.0f;
            }
            else {
                percentage = 100.0f * (voltage_mv - BATTERY_MIN_VOLTAGE_MV) / (BATTERY_MAX_VOLTAGE_MV - BATTERY_MIN_VOLTAGE_MV);
            }

            *out_battery = percentage;
            PRINTF("State: Battery = %.2fV (%.1f%%)\r\n", voltage_mv / 1000.0f, percentage);
        }
    }

    return 0;
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: PRINTF("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
