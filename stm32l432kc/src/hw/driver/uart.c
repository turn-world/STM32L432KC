/*
 * uart.c
 *
 *  Created on: May 6, 2026
 *      Author: TEMP
 */


#include "uart.h"
#include "qbuffer.h"


#ifdef _USE_HW_UART

#define _USE_UART1
static bool is_open[UART_MAX_CH];
static bool is_rx_dma_open[UART_MAX_CH];

#ifdef _USE_UART1
static qbuffer_t qbuffer[UART_MAX_CH];
static uint8_t rx_buf[256];

UART_HandleTypeDef huart2;
DMA_HandleTypeDef  hdma_usart2_rx;
#endif

#ifdef _USE_UART2
static qbuffer_t qbuffer[UART_MAX_CH];
static uint8_t rx_buf[256];


UART_HandleTypeDef huart2;
DMA_HandleTypeDef  hdma_usart2_rx;
#endif


bool uartInit(void)
{
  for (int i=0; i<UART_MAX_CH; i++)
  {
    is_open[i] = false;
    is_rx_dma_open[i] = false;
  }


  return true;
}

bool uartOpen(uint8_t ch, uint32_t baud)
{
  bool ret = false;


  switch(ch)
  {
    case _DEF_UART1:
#ifdef _USE_UART1
    	  huart2.Instance = USART2;
    	  huart2.Init.BaudRate = baud;
    	  huart2.Init.WordLength = UART_WORDLENGTH_8B;
    	  huart2.Init.StopBits = UART_STOPBITS_1;
    	  huart2.Init.Parity = UART_PARITY_NONE;
    	  huart2.Init.Mode = UART_MODE_TX_RX;
    	  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    	  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    	  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    	  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

          if (is_open[ch] == true)
          {
            HAL_UART_DeInit(&huart2);
          }
          is_rx_dma_open[ch] = false;

          qbufferCreate(&qbuffer[ch], &rx_buf[0], 256);

          __HAL_RCC_DMA1_CLK_ENABLE();
          HAL_NVIC_SetPriority(DMA1_Channel6_IRQn, 0, 0);
          HAL_NVIC_EnableIRQ(DMA1_Channel6_IRQn);

    if (HAL_UART_Init(&huart2) != HAL_OK)
    {
      ret = false;
    }
    else
    {
      ret = true;
      is_open[ch] = true;
      qbuffer[ch].in  = 0;
      qbuffer[ch].out = 0;

      if(HAL_UART_Receive_DMA(&huart2, (uint8_t *)&rx_buf[0], 256) == HAL_OK &&
         hdma_usart2_rx.Instance != NULL)
      {
        is_rx_dma_open[ch] = true;
        qbuffer[ch].in  = qbuffer[ch].len - hdma_usart2_rx.Instance->CNDTR;
        qbuffer[ch].out = qbuffer[ch].in;
      }
    }
#endif
    break;

    case _DEF_UART2:
      #ifdef _USE_UART2
    	  huart2.Instance 				= USART2;
    	  huart2.Init.BaudRate 			= baud;
    	  huart2.Init.WordLength 		= UART_WORDLENGTH_8B;
    	  huart2.Init.StopBits 			= UART_STOPBITS_1;
    	  huart2.Init.Parity 			= UART_PARITY_NONE;
    	  huart2.Init.Mode 				= UART_MODE_TX_RX;
    	  huart2.Init.HwFlowCtl 		= UART_HWCONTROL_NONE;
    	  huart2.Init.OverSampling 		= UART_OVERSAMPLING_16;

    	  huart2.Init.OneBitSampling 			= UART_ONE_BIT_SAMPLE_DISABLE;
    	  huart2.AdvancedInit.AdvFeatureInit 	= UART_ADVFEATURE_NO_INIT;

      if (is_open[ch] == true)
      {
        HAL_UART_DeInit(&huart2);
      }
      is_rx_dma_open[ch] = false;

      qbufferCreate(&qbuffer[ch], &rx_buf[0], 256);

      __HAL_RCC_DMA1_CLK_ENABLE();
      HAL_NVIC_SetPriority(DMA1_Channel6_IRQn, 0, 0);
      HAL_NVIC_EnableIRQ(DMA1_Channel6_IRQn);


      if (HAL_UART_Init(&huart2) != HAL_OK)
      {
        ret = false;
      }
      else
      {
        ret = true;
        is_open[ch] = true;
        qbuffer[ch].in  = 0;
        qbuffer[ch].out = 0;

        if(HAL_UART_Receive_DMA(&huart2, (uint8_t *)&rx_buf[0], 256) == HAL_OK &&
           hdma_usart2_rx.Instance != NULL)
        {
          is_rx_dma_open[ch] = true;
          qbuffer[ch].in  = qbuffer[ch].len - hdma_usart2_rx.Instance->CNDTR;
          qbuffer[ch].out = qbuffer[ch].in;
        }
      }
      #endif
      break;
  }

  return ret;
}

uint32_t uartAvailable(uint8_t ch)
{
  uint32_t ret = 0;

  switch(ch)
  {
    case _DEF_UART1:
#ifdef _USE_UART1
      if (is_rx_dma_open[ch] != true || hdma_usart2_rx.Instance == NULL)
      {
        break;
      }
        qbuffer[ch].in = (qbuffer[ch].len - hdma_usart2_rx.Instance->CNDTR);
        ret = qbufferAvailable(&qbuffer[ch]);
#endif
      break;

    case _DEF_UART2:
      #ifdef _USE_UART2
      if (is_rx_dma_open[ch] != true || hdma_usart2_rx.Instance == NULL)
      {
        break;
      }
      qbuffer[ch].in = (qbuffer[ch].len - hdma_usart2_rx.Instance->CNDTR);
      ret = qbufferAvailable(&qbuffer[ch]);
      #endif
      break;
  }

  return ret;
}

uint8_t uartRead(uint8_t ch)
{
  uint8_t ret = 0;

  switch(ch)
  {
    case _DEF_UART1:
#ifdef _USE_UART1
        qbufferRead(&qbuffer[_DEF_UART1], &ret, 1);
#endif
      break;

    case _DEF_UART2:
      #ifdef _USE_UART2
      qbufferRead(&qbuffer[_DEF_UART2], &ret, 1);
      #endif
      break;
  }

  return ret;
}

uint32_t uartWrite(uint8_t ch, uint8_t *p_data, uint32_t length)
{
  uint32_t ret = 0;
  HAL_StatusTypeDef status;

  switch(ch)
  {
    case _DEF_UART1:
#ifdef _USE_UART1
        status = HAL_UART_Transmit(&huart2, p_data, length, 100);
        if (status == HAL_OK)
        {
          ret = length;
        }
#endif
      break;

    case _DEF_UART2:
      #ifdef _USE_UART2
      status = HAL_UART_Transmit(&huart2, p_data, length, 100);
      if (status == HAL_OK)
      {
        ret = length;
      }
      #endif
      break;
  }

  return ret;
}

uint32_t uartPrintf(uint8_t ch, char *fmt, ...)
{
  char buf[256];
  va_list args;
  int len;
  uint32_t ret;

  va_start(args, fmt);
  len = vsnprintf(buf, 256, fmt, args);

  ret = uartWrite(ch, (uint8_t *)buf, len);

  va_end(args);


  return ret;
}

uint32_t uartGetBaud(uint8_t ch)
{
  uint32_t ret = 0;


  switch(ch)
  {
    case _DEF_UART1:
#ifdef _USE_UART1
        ret = huart2.Init.BaudRate;
#endif
      break;

    case _DEF_UART2:
      #ifdef _USE_UART2
      ret = huart2.Init.BaudRate;
      #endif
      break;
  }

  return ret;
}

#ifdef _USE_UART1

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART2)
  {
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
#if 0
  if (huart->Instance == USART1)
  {
    qbufferWrite(&qbuffer[_DEF_UART2], &rx_data[_DEF_UART2], 1);

    HAL_UART_Receive_IT(&huart1, (uint8_t *)&rx_data[_DEF_UART2], 1);
  }
#endif
}

void HAL_UART_MspInit(UART_HandleTypeDef* uartHandle)
{

	GPIO_InitTypeDef GPIO_InitStruct = {0};
	  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
	  if(uartHandle->Instance==USART2)
	  {
	  /* USER CODE BEGIN USART2_MspInit 0 */

	  /* USER CODE END USART2_MspInit 0 */

	  /** Initializes the peripherals clock
	  */
	    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART2;
	    PeriphClkInit.Usart2ClockSelection = RCC_USART2CLKSOURCE_PCLK1;
	    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
	    {
	      Error_Handler();
	    }

	    /* USART2 clock enable */
	    __HAL_RCC_USART2_CLK_ENABLE();

	    __HAL_RCC_GPIOA_CLK_ENABLE();
	    /**USART2 GPIO Configuration
	    PA2     ------> USART2_TX
	    PA15 (JTDI)     ------> USART2_RX
	    */
	    GPIO_InitStruct.Pin = GPIO_PIN_2;
	    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
	    GPIO_InitStruct.Pull = GPIO_NOPULL;
	    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
	    GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
	    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

	    GPIO_InitStruct.Pin = GPIO_PIN_15;
	    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
	    GPIO_InitStruct.Pull = GPIO_NOPULL;
	    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
	    GPIO_InitStruct.Alternate = GPIO_AF3_USART2;
	    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

	    /* USART2 DMA Init */
	    /* USART2_RX Init */
	    hdma_usart2_rx.Instance = DMA1_Channel6;
	    hdma_usart2_rx.Init.Request = DMA_REQUEST_2;
	    hdma_usart2_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
	    hdma_usart2_rx.Init.PeriphInc = DMA_PINC_DISABLE;
	    hdma_usart2_rx.Init.MemInc = DMA_MINC_ENABLE;
	    hdma_usart2_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
	    hdma_usart2_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
	    hdma_usart2_rx.Init.Mode = DMA_CIRCULAR;
	    hdma_usart2_rx.Init.Priority = DMA_PRIORITY_LOW;
	    if (HAL_DMA_Init(&hdma_usart2_rx) != HAL_OK)
	    {
	      Error_Handler();
	    }

	    __HAL_LINKDMA(uartHandle,hdmarx,hdma_usart2_rx);

	    /* USART2 interrupt Init */
	    HAL_NVIC_SetPriority(USART2_IRQn, 0, 0);
	    HAL_NVIC_EnableIRQ(USART2_IRQn);
	  /* USER CODE BEGIN USART2_MspInit 1 */

	  /* USER CODE END USART2_MspInit 1 */
  /* USER CODE END USART1_MspInit 1 */
  }
}

void HAL_UART_MspDeInit(UART_HandleTypeDef* uartHandle)
{

	  if(uartHandle->Instance==USART2)
	  {
	  /* USER CODE BEGIN USART2_MspDeInit 0 */

	  /* USER CODE END USART2_MspDeInit 0 */
	    /* Peripheral clock disable */
	    __HAL_RCC_USART2_CLK_DISABLE();

	    /**USART2 GPIO Configuration
	    PA2     ------> USART2_TX
	    PA15 (JTDI)     ------> USART2_RX
	    */
	    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_2|GPIO_PIN_15);

	    /* USART2 DMA DeInit */
	    if (uartHandle->hdmarx != NULL)
	    {
	      HAL_DMA_DeInit(uartHandle->hdmarx);
	    }

	    /* USART2 interrupt Deinit */
	    HAL_NVIC_DisableIRQ(USART2_IRQn);
	  /* USER CODE BEGIN USART2_MspDeInit 1 */

	  /* USER CODE END USART2_MspDeInit 1 */
	  }
}
#endif

#ifdef _USE_UART2
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART2)
  {
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
#if 0
  if (huart->Instance == USART1)
  {
    qbufferWrite(&qbuffer[_DEF_UART2], &rx_data[_DEF_UART2], 1);

    HAL_UART_Receive_IT(&huart1, (uint8_t *)&rx_data[_DEF_UART2], 1);
  }
#endif
}




void HAL_UART_MspInit(UART_HandleTypeDef* uartHandle)
{

	  GPIO_InitTypeDef GPIO_InitStruct = {0};
	  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
  if(uartHandle->Instance==USART2)
    {
    /* USER CODE BEGIN USART2_MspInit 0 */

    /* USER CODE END USART2_MspInit 0 */

    /** Initializes the peripherals clock
    */
      PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART2;
      PeriphClkInit.Usart2ClockSelection = RCC_USART2CLKSOURCE_PCLK1;
      if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
      {
        Error_Handler();
      }

      /* USART2 clock enable */
      __HAL_RCC_USART2_CLK_ENABLE();

      __HAL_RCC_GPIOA_CLK_ENABLE();
      /**USART2 GPIO Configuration
      PA2     ------> USART2_TX
      PA3     ------> USART2_RX
      */
      GPIO_InitStruct.Pin = GPIO_PIN_2|GPIO_PIN_3;
      GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
      GPIO_InitStruct.Pull = GPIO_NOPULL;
      GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
      GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
      HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

      /* USART2 DMA Init */
      /* USART2_RX Init */
      hdma_usart2_rx.Instance = DMA1_Channel6;
      hdma_usart2_rx.Init.Request = DMA_REQUEST_2;
      hdma_usart2_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
      hdma_usart2_rx.Init.PeriphInc = DMA_PINC_DISABLE;
      hdma_usart2_rx.Init.MemInc = DMA_MINC_ENABLE;
      hdma_usart2_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
      hdma_usart2_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
      hdma_usart2_rx.Init.Mode = DMA_NORMAL;
      hdma_usart2_rx.Init.Priority = DMA_PRIORITY_LOW;
      if (HAL_DMA_Init(&hdma_usart2_rx) != HAL_OK)
      {
        Error_Handler();
      }

      __HAL_LINKDMA(uartHandle,hdmarx,hdma_usart2_rx);

    /* USART1 interrupt Init */
    HAL_NVIC_SetPriority(USART2_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
  /* USER CODE BEGIN USART1_MspInit 1 */

  /* USER CODE END USART1_MspInit 1 */
  }
}

void HAL_UART_MspDeInit(UART_HandleTypeDef* uartHandle)
{

  if(uartHandle->Instance==USART1)
  {
	  /* USER CODE BEGIN USART2_MspDeInit 0 */

	  /* USER CODE END USART2_MspDeInit 0 */
	    /* Peripheral clock disable */
	    __HAL_RCC_USART2_CLK_DISABLE();

	    /**USART2 GPIO Configuration
	    PA2     ------> USART2_TX
	    PA3     ------> USART2_RX
	    */
	    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_2|GPIO_PIN_3);

	    /* USART2 DMA DeInit */
	    if (uartHandle->hdmarx != NULL)
	    {
	      HAL_DMA_DeInit(uartHandle->hdmarx);
	    }

    /* USART1 interrupt Deinit */
    HAL_NVIC_DisableIRQ(USART2_IRQn);
  /* USER CODE BEGIN USART1_MspDeInit 1 */

  /* USER CODE END USART1_MspDeInit 1 */
  }
}
#endif

#endif
