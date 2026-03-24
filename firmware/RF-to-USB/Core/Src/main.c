/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"

/* USER CODE BEGIN Includes */
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include "nrf24.h"
#include "stdio.h"
#include "string.h"
/* USER CODE END Includes */

/* USER CODE BEGIN PTD */
typedef enum {
    STATE_INIT,
    STATE_READY,
    STATE_TRANSMIT,
    STATE_RECEIVE,
} State;
/* USER CODE END PTD */

/* USER CODE BEGIN PD */
#define LED_BLINK_READY_MS  500
#define LED_FLASH_TX_MS      80
#define LED_FLASH_RX_MS      80

/* Shared RF configuration — single source of truth for both TX and RX */
static const uint8_t RF_ADDR[5]  = {'N','O','D','E','1'};
#define RF_CHANNEL   2
#define RF_PAYLOAD  32
/* USER CODE END PD */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi3;
PCD_HandleTypeDef hpcd_USB_FS;

/* USER CODE BEGIN PV */
State    currentState = STATE_INIT;
uint32_t lastTxTick   = 0;
uint32_t lastLedTick  = 0;
uint32_t ledTxOffTick = 0;
uint32_t ledRxOffTick = 0;
static State prevState = (State)-1;

static char dispLine1[17] = {0};
static char dispLine2[17] = {0};
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI3_Init(void);
static void MX_USB_PCD_Init(void);
/* USER CODE BEGIN PFP */
void runStateMachine(void);
void UpdateDisplay(const char *line1, const char *line2);
void UpdateDisplayLine2(const char *line2);
/* USER CODE END PFP */

/* USER CODE BEGIN 0 */

void UpdateDisplay(const char *line1, const char *line2)
{
    if (line1) { strncpy(dispLine1, line1, 16); dispLine1[16] = '\0'; }
    if (line2) { strncpy(dispLine2, line2, 16); dispLine2[16] = '\0'; }
    ssd1306_Fill(Black);
    ssd1306_SetCursor(2, 0);
    ssd1306_WriteString(dispLine1, Font_16x15, White);
    ssd1306_SetCursor(2, 17);
    ssd1306_WriteString(dispLine2, Font_16x15, White);
    ssd1306_UpdateScreen();
}

void UpdateDisplayLine2(const char *line2)
{
    UpdateDisplay(NULL, line2);
}

/* USER CODE END 0 */

int main(void)
{
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_SPI3_Init();
  MX_USB_PCD_Init();

  /* USER CODE BEGIN 2 */
  HAL_GPIO_WritePin(LED_TX_GPIO_Port, LED_TX_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LED_RX_GPIO_Port, LED_RX_Pin, GPIO_PIN_RESET);

  ssd1306_Init();

  /* nRF24_Init() and nRF24_Check() are called inside the state machine.
     Do NOT call nRF24_SetAddr / nRF24_SetRXPipe here — nRF24_Check()
     overwrites TX_ADDR with a test pattern, so any addresses written
     before Check() runs will be silently clobbered.  All RF config is
     deferred to the STATE_TRANSMIT / STATE_RECEIVE entry blocks.      */

  /* USER CODE END 2 */

  while (1)
  {
    runStateMachine();
    HAL_Delay(10);
  }
}

/* USER CODE BEGIN 4 */

void runStateMachine(void)
{
    uint32_t now = HAL_GetTick();

    uint8_t isNewState = (currentState != prevState);
    prevState = currentState;   /* Updated at TOP — before any transition */

    /* ------------------------------------------------------------------ */
    /* Entry actions                                                        */
    /* ------------------------------------------------------------------ */
    if (isNewState)
    {
        HAL_GPIO_WritePin(LED_TX_GPIO_Port, LED_TX_Pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(LED_RX_GPIO_Port, LED_RX_Pin, GPIO_PIN_RESET);
        lastLedTick = now;

        switch (currentState)
        {
            /* ---- INIT: verify SPI comms, then move to READY ---- */
            case STATE_INIT:
                UpdateDisplay("Checking...", "");

                /* Step 1: reset chip to a clean known state */
                nRF24_CE_L();
                nRF24_Init();

                /* Step 2: verify the chip is actually there.
                   NOTE: Check() writes a test pattern to TX_ADDR —
                   that is why ALL address config lives in the TX/RX
                   entry blocks below, never here in INIT.           */
                if (nRF24_Check())
                {
                    UpdateDisplay("nRF24 OK", "");
                    HAL_Delay(1000);
                    currentState = STATE_READY;
                    return;
                }
                else
                {
                    UpdateDisplay("nRF Error!", "SPI fault?");
                }
                break;

            /* ---- READY: wait for mode selection ---- */
            case STATE_READY:
                UpdateDisplay("Mode Select", "TX    RX");
                HAL_GPIO_WritePin(LED_TX_GPIO_Port, LED_TX_Pin, GPIO_PIN_SET);
                HAL_GPIO_WritePin(LED_RX_GPIO_Port, LED_RX_Pin, GPIO_PIN_SET);
                break;

            /* ----------------------------------------------------------------
             * RECEIVE entry
             *
             * Full RF config happens here, matching the reference pattern:
             *   Init → configure all registers → SetPowerMode last → CE_H
             *
             * This guarantees no register has been touched by Check() between
             * configuration and activation.
             * ---------------------------------------------------------------- */
            case STATE_RECEIVE:
                nRF24_CE_L();
                nRF24_Init();                                   /* clean slate  */

                nRF24_SetRFChannel(RF_CHANNEL);
                nRF24_SetDataRate(nRF24_DR_1Mbps);
                nRF24_SetTXPower(nRF24_TXPWR_0dBm);
                nRF24_SetCRCScheme(nRF24_CRC_2byte);
                nRF24_SetAddrWidth(5);

                nRF24_DisableAA(0xFF);                          /* all pipes    */

                /* PIPE0 RX address must match the TX address on the other node */
                nRF24_SetAddr(nRF24_PIPE0, RF_ADDR);
                nRF24_SetRXPipe(nRF24_PIPE0, nRF24_AA_OFF, RF_PAYLOAD);

                nRF24_SetOperationalMode(nRF24_MODE_RX);

                nRF24_FlushRX();
                nRF24_FlushTX();
                nRF24_ClearIRQFlags();

                /* Power up AFTER all config (matches reference) */
                nRF24_SetPowerMode(nRF24_PWR_UP);
                HAL_Delay(2);                                   /* ≥1.5 ms osc */

                nRF24_CE_H();                                   /* start listen */

                UpdateDisplay("RX MODE", "Waiting...");
                HAL_GPIO_WritePin(LED_RX_GPIO_Port, LED_RX_Pin, GPIO_PIN_SET);
                break;

            /* ----------------------------------------------------------------
             * TRANSMIT entry — same disciplined init sequence
             * ---------------------------------------------------------------- */
            case STATE_TRANSMIT:
                nRF24_CE_L();
                nRF24_Init();

                nRF24_SetRFChannel(RF_CHANNEL);
                nRF24_SetDataRate(nRF24_DR_1Mbps);
                nRF24_SetTXPower(nRF24_TXPWR_0dBm);
                nRF24_SetCRCScheme(nRF24_CRC_2byte);
                nRF24_SetAddrWidth(5);

                nRF24_DisableAA(0xFF);

                /* TX address — must match PIPE0 address on the RX node */
                nRF24_SetAddr(nRF24_PIPETX, RF_ADDR);

                nRF24_SetOperationalMode(nRF24_MODE_TX);

                nRF24_FlushRX();
                nRF24_FlushTX();
                nRF24_ClearIRQFlags();

                /* Power up AFTER all config */
                nRF24_SetPowerMode(nRF24_PWR_UP);
                HAL_Delay(2);

                /* CE stays low between packets; raised briefly per packet */

                UpdateDisplay("TX MODE", "Sending...");
                HAL_GPIO_WritePin(LED_TX_GPIO_Port, LED_TX_Pin, GPIO_PIN_SET);
                lastTxTick = now - 1000;    /* trigger first packet immediately */
                break;
        }
    }

    /* ------------------------------------------------------------------ */
    /* Continuous / polling actions                                         */
    /* ------------------------------------------------------------------ */
    switch (currentState)
    {
        /* ---- READY: both LEDs blink in unison ---- */
        case STATE_READY:
            if (now - lastLedTick >= LED_BLINK_READY_MS)
            {
                HAL_GPIO_TogglePin(LED_TX_GPIO_Port, LED_TX_Pin);
                HAL_GPIO_TogglePin(LED_RX_GPIO_Port, LED_RX_Pin);
                lastLedTick = now;
            }
            break;

        /* ---- RECEIVE: poll FIFO, update display, flash RX LED ---- */
        case STATE_RECEIVE:
        {
            uint8_t payload[33] = {0};
            uint8_t payloadLen  = 0;

            if (nRF24_GetStatus_RXFIFO() != nRF24_STATUS_RXFIFO_EMPTY)
            {
                nRF24_ReadPayload(payload, &payloadLen);
                if (payloadLen == 0 || payloadLen > 32) payloadLen = 32;
                payload[payloadLen] = '\0';

                nRF24_ClearIRQFlags();
                UpdateDisplayLine2((char *)payload);

                HAL_GPIO_WritePin(LED_RX_GPIO_Port, LED_RX_Pin, GPIO_PIN_SET);
                ledRxOffTick = now + LED_FLASH_RX_MS;
            }

            if (ledRxOffTick && now >= ledRxOffTick)
            {
                HAL_GPIO_WritePin(LED_RX_GPIO_Port, LED_RX_Pin, GPIO_PIN_RESET);
                ledRxOffTick = 0;
            }
            break;
        }

        /* ---- TRANSMIT: send every second, flash TX LED ---- */
        case STATE_TRANSMIT:
        {
            if (now - lastTxTick >= 1000)
            {
                lastTxTick = now;

                uint8_t txData[32] = {0};
                memcpy(txData, "Hello", 5);

                nRF24_WritePayload(txData, RF_PAYLOAD);
                nRF24_CE_H();
                HAL_Delay(1);   /* ≥10 µs CE pulse */
                nRF24_CE_L();

                nRF24_ClearIRQFlags();
                UpdateDisplayLine2("SENT OK");

                HAL_GPIO_WritePin(LED_TX_GPIO_Port, LED_TX_Pin, GPIO_PIN_SET);
                ledTxOffTick = now + LED_FLASH_TX_MS;
            }

            if (ledTxOffTick && now >= ledTxOffTick)
            {
                HAL_GPIO_WritePin(LED_TX_GPIO_Port, LED_TX_Pin, GPIO_PIN_RESET);
                ledTxOffTick = 0;
            }
            break;
        }

        default:
            break;
    }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_PIN)
{
    static uint32_t lastPress = 0;
    if (HAL_GetTick() - lastPress < 200) return;
    lastPress = HAL_GetTick();

    if (GPIO_PIN == BTN1_EXTI_Pin)
    {
        if (currentState == STATE_READY) currentState = STATE_TRANSMIT;
        else                             currentState = STATE_READY;
    }
    else if (GPIO_PIN == BTN2_EXTI_Pin)
    {
        if (currentState == STATE_READY) currentState = STATE_RECEIVE;
        else                             currentState = STATE_READY;
    }
}

/* USER CODE END 4 */

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_CRSInitTypeDef RCC_CRSInitStruct = {0};

  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK) Error_Handler();

  RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.MSIState            = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = 0;
  RCC_OscInitStruct.MSIClockRange       = RCC_MSIRANGE_6;
  RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLM            = 1;
  RCC_OscInitStruct.PLL.PLLN            = 40;
  RCC_OscInitStruct.PLL.PLLP            = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ            = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR            = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) Error_Handler();

  RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                   | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK) Error_Handler();

  __HAL_RCC_CRS_CLK_ENABLE();
  RCC_CRSInitStruct.Prescaler             = RCC_CRS_SYNC_DIV1;
  RCC_CRSInitStruct.Source                = RCC_CRS_SYNC_SOURCE_USB;
  RCC_CRSInitStruct.Polarity              = RCC_CRS_SYNC_POLARITY_RISING;
  RCC_CRSInitStruct.ReloadValue           = __HAL_RCC_CRS_RELOADVALUE_CALCULATE(48000000, 1000);
  RCC_CRSInitStruct.ErrorLimitValue       = 34;
  RCC_CRSInitStruct.HSI48CalibrationValue = 32;
  HAL_RCCEx_CRSConfig(&RCC_CRSInitStruct);
}

static void MX_SPI3_Init(void)
{
  hspi3.Instance               = SPI3;
  hspi3.Init.Mode              = SPI_MODE_MASTER;
  hspi3.Init.Direction         = SPI_DIRECTION_2LINES;
  hspi3.Init.DataSize          = SPI_DATASIZE_8BIT;
  hspi3.Init.CLKPolarity       = SPI_POLARITY_LOW;
  hspi3.Init.CLKPhase          = SPI_PHASE_1EDGE;
  hspi3.Init.NSS               = SPI_NSS_SOFT;
  hspi3.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
  hspi3.Init.FirstBit          = SPI_FIRSTBIT_MSB;
  hspi3.Init.TIMode            = SPI_TIMODE_DISABLE;
  hspi3.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
  hspi3.Init.CRCPolynomial     = 7;
  hspi3.Init.CRCLength         = SPI_CRC_LENGTH_DATASIZE;
  hspi3.Init.NSSPMode          = SPI_NSS_PULSE_ENABLE;
  if (HAL_SPI_Init(&hspi3) != HAL_OK) Error_Handler();
}

static void MX_USB_PCD_Init(void)
{
  hpcd_USB_FS.Instance                     = USB;
  hpcd_USB_FS.Init.dev_endpoints           = 8;
  hpcd_USB_FS.Init.speed                   = PCD_SPEED_FULL;
  hpcd_USB_FS.Init.phy_itface              = PCD_PHY_EMBEDDED;
  hpcd_USB_FS.Init.Sof_enable              = DISABLE;
  hpcd_USB_FS.Init.low_power_enable        = DISABLE;
  hpcd_USB_FS.Init.lpm_enable              = DISABLE;
  hpcd_USB_FS.Init.battery_charging_enable = DISABLE;
  if (HAL_PCD_Init(&hpcd_USB_FS) != HAL_OK) Error_Handler();
}

static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  HAL_GPIO_WritePin(GPIOA, LED_TX_Pin|LED_RX_Pin|OLED_DC_Pin|OLED_RES_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(OLED_CS_GPIO_Port, OLED_CS_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(SPI3_CS_GPIO_Port, SPI3_CS_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(NRF_CE_GPIO_Port,  NRF_CE_Pin,  GPIO_PIN_RESET);

  GPIO_InitStruct.Pin  = BTN2_EXTI_Pin|BTN1_EXTI_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin   = LED_TX_Pin|LED_RX_Pin|OLED_DC_Pin|OLED_CS_Pin|OLED_RES_Pin;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin  = NRF_IRQ_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(NRF_IRQ_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin   = SPI3_CS_Pin|NRF_CE_Pin;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  HAL_NVIC_SetPriority(EXTI0_IRQn,     0, 0); HAL_NVIC_EnableIRQ(EXTI0_IRQn);
  HAL_NVIC_SetPriority(EXTI1_IRQn,     0, 0); HAL_NVIC_EnableIRQ(EXTI1_IRQn);
  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 0, 0); HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

void Error_Handler(void)
{
  __disable_irq();
  while (1) {}
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line) {}
#endif
