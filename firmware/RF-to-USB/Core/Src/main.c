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
#include "usbd_cdc_if.h"   /* CDC_Transmit_FS — for echoing back to host     */
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
#define LED_BLINK_READY_MS   500
#define LED_FLASH_TX_MS       80
#define LED_FLASH_RX_MS       80

/* Shared RF config */
static const uint8_t RF_ADDR[5] = {'N','O','D','E','1'};
#define RF_CHANNEL  2
#define RF_PAYLOAD 32

/* Maximum user message length is capped at the nRF24 payload size */
#define MSG_MAX_LEN RF_PAYLOAD
/* USER CODE END PD */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi3;
extern PCD_HandleTypeDef hpcd_USB_FS;

/* USER CODE BEGIN PV */
State    currentState = STATE_INIT;
uint32_t lastLedTick  = 0;
uint32_t ledTxOffTick = 0;
uint32_t ledRxOffTick = 0;
static State prevState = (State)-1;

/* ---- USB CDC receive buffer -------------------------------------------- */
/* Written from CDC_DataReceivedCallback (USB interrupt context).
   Read and cleared from the main loop (STATE_TRANSMIT polling).
   volatile ensures the compiler doesn't cache stale values.               */
static volatile uint8_t  usbMsgPending = 0;          /* 1 = new message     */
static volatile uint8_t  usbMsg[MSG_MAX_LEN + 1];    /* +1 for '\0'         */
static volatile uint8_t  usbMsgLen = 0;

/* ---- Display cache ------------------------------------------------------- */
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

/**
  * @brief  Called from CDC_Receive_FS in usbd_cdc_if.c (USB interrupt context).
  *         Copies the incoming bytes into the pending-message buffer.
  *         Strip any trailing CR / LF so the raw text hits the RF payload.
  */
void CDC_DataReceivedCallback(uint8_t *buf, uint32_t len)
{
    if (usbMsgPending) return;          /* previous message not sent yet — drop */

    /* Clamp to payload capacity */
    if (len > MSG_MAX_LEN) len = MSG_MAX_LEN;

    /* Strip trailing CR / LF (common when sending from a terminal) */
    while (len > 0 && (buf[len - 1] == '\r' || buf[len - 1] == '\n'))
        len--;

    if (len == 0) return;               /* blank line — ignore */

    memcpy((uint8_t *)usbMsg, buf, len);
    usbMsg[len] = '\0';
    usbMsgLen   = (uint8_t)len;
    usbMsgPending = 1;                  /* signal main loop */
}

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
  MX_USB_DEVICE_Init();

  /* USER CODE BEGIN 2 */
  HAL_GPIO_WritePin(LED_TX_GPIO_Port, LED_TX_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LED_RX_GPIO_Port, LED_RX_Pin, GPIO_PIN_RESET);

  ssd1306_Init();

  /* USB device stack is started by MX_USB_PCD_Init via the generated
     usb_device.c / MX_USB_DEVICE_Init().  Nothing extra needed here.  */

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
    prevState = currentState;

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
            case STATE_INIT:
                UpdateDisplay("Checking...", "");
                nRF24_CE_L();
                nRF24_Init();
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

            case STATE_READY:
                UpdateDisplay("Mode Select", "TX    RX");
                HAL_GPIO_WritePin(LED_TX_GPIO_Port, LED_TX_Pin, GPIO_PIN_SET);
                HAL_GPIO_WritePin(LED_RX_GPIO_Port, LED_RX_Pin, GPIO_PIN_SET);
                break;

            case STATE_RECEIVE:
                nRF24_CE_L();
                nRF24_Init();
                nRF24_SetRFChannel(RF_CHANNEL);
                nRF24_SetDataRate(nRF24_DR_1Mbps);
                nRF24_SetTXPower(nRF24_TXPWR_0dBm);
                nRF24_SetCRCScheme(nRF24_CRC_2byte);
                nRF24_SetAddrWidth(5);
                nRF24_DisableAA(0xFF);
                nRF24_SetAddr(nRF24_PIPE0, RF_ADDR);
                nRF24_SetRXPipe(nRF24_PIPE0, nRF24_AA_OFF, RF_PAYLOAD);
                nRF24_SetOperationalMode(nRF24_MODE_RX);
                nRF24_FlushRX();
                nRF24_FlushTX();
                nRF24_ClearIRQFlags();
                nRF24_SetPowerMode(nRF24_PWR_UP);
                HAL_Delay(2);
                nRF24_CE_H();
                UpdateDisplay("RX MODE", "Waiting...");
                HAL_GPIO_WritePin(LED_RX_GPIO_Port, LED_RX_Pin, GPIO_PIN_SET);
                break;

            case STATE_TRANSMIT:
                nRF24_CE_L();
                nRF24_Init();
                nRF24_SetRFChannel(RF_CHANNEL);
                nRF24_SetDataRate(nRF24_DR_1Mbps);
                nRF24_SetTXPower(nRF24_TXPWR_0dBm);
                nRF24_SetCRCScheme(nRF24_CRC_2byte);
                nRF24_SetAddrWidth(5);
                nRF24_DisableAA(0xFF);
                nRF24_SetAddr(nRF24_PIPETX, RF_ADDR);
                nRF24_SetOperationalMode(nRF24_MODE_TX);
                nRF24_FlushRX();
                nRF24_FlushTX();
                nRF24_ClearIRQFlags();
                nRF24_SetPowerMode(nRF24_PWR_UP);
                HAL_Delay(2);

                /* Discard any USB data that arrived before we entered TX mode */
                usbMsgPending = 0;

                UpdateDisplay("TX MODE", "Waiting USB...");
                /* TX LED on steadily while idle */
                HAL_GPIO_WritePin(LED_TX_GPIO_Port, LED_TX_Pin, GPIO_PIN_SET);
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
            uint8_t payload[RF_PAYLOAD + 1];
            uint8_t payloadLen = 0;

            if (nRF24_GetStatus_RXFIFO() != nRF24_STATUS_RXFIFO_EMPTY)
            {
                nRF24_ReadPayload(payload, &payloadLen);
                if (payloadLen == 0 || payloadLen > RF_PAYLOAD) payloadLen = RF_PAYLOAD;
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

        /* ---- TRANSMIT: send whatever arrives over USB CDC ---- */
        case STATE_TRANSMIT:
        {
            if (usbMsgPending)
            {
                /* Snapshot the message and release the buffer immediately so
                   the USB ISR can accept the next packet without waiting for
                   the RF transmission to complete.                          */
                uint8_t txData[RF_PAYLOAD] = {0};
                uint8_t msgLen = usbMsgLen;
                memcpy(txData, (const uint8_t *)usbMsg, msgLen);
                usbMsgPending = 0;              /* release buffer */

                /* Show the outgoing message on line 2 of the display */
                UpdateDisplay("TX MODE", (char *)txData);

                /* Transmit over RF */
                nRF24_WritePayload(txData, RF_PAYLOAD);
                nRF24_CE_H();
                HAL_Delay(1);                   /* ≥10 µs CE pulse */
                nRF24_CE_L();
                nRF24_ClearIRQFlags();

                /* Echo confirmation back to the host terminal */
                char echo[48];
                int echoLen = snprintf(echo, sizeof(echo),
                                       "SENT: %.*s\r\n", msgLen, txData);
                CDC_Transmit_FS((uint8_t *)echo, (uint16_t)echoLen);

                /* Flash TX LED */
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
