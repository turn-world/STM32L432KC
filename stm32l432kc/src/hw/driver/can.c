/*
 * can.c
 *
 *  Created on: May 5, 2026
 *      Author: TEMP
 *
 *      CAN ID
 *      DLC
 *      DATA
 *      STD / EXT
 */

#include "can.h"

#ifdef _USE_HW_CAN
#include "qbuffer.h"
#include "cli.h"


#define CAN_RECOVERY_FAIL_CNT_MAX     6

#define CAN_BAMOCAR_CMD_ID            0x201
#define CAN_BAMOCAR_RSP_ID            0x181
#define CAN_BAMOCAR_STATUS_REQ        0x3D
#define CAN_BAMOCAR_STATUS_REG        0x40
#define CAN_BAMOCAR_STATUS_TIMEOUT    200


typedef struct
{
  uint32_t prescaler;
  uint32_t sjw;
  uint32_t tseg1;
  uint32_t tseg2;
} can_baud_cfg_t;

const can_baud_cfg_t can_baud_cfg_80m_normal[] =
{
  {50, CAN_SJW_1TQ, CAN_BS1_13TQ, CAN_BS2_2TQ}, // 100K, 87.5%
  {40, CAN_SJW_1TQ, CAN_BS1_13TQ, CAN_BS2_2TQ}, // 125K, 87.5%
  {20, CAN_SJW_1TQ, CAN_BS1_13TQ, CAN_BS2_2TQ}, // 250K, 87.5%
  {10, CAN_SJW_1TQ, CAN_BS1_13TQ, CAN_BS2_2TQ}, // 500K, 87.5%
  { 5, CAN_SJW_1TQ, CAN_BS1_13TQ, CAN_BS2_2TQ}, // 1M,   87.5%
};

const can_baud_cfg_t can_baud_cfg_80m_data[] =
{
  {50, CAN_SJW_1TQ, CAN_BS1_13TQ, CAN_BS2_2TQ}, // 100K, 87.5%
  {40, CAN_SJW_1TQ, CAN_BS1_13TQ, CAN_BS2_2TQ}, // 125K, 87.5%
  {20, CAN_SJW_1TQ, CAN_BS1_13TQ, CAN_BS2_2TQ}, // 250K, 87.5%
  {10, CAN_SJW_1TQ, CAN_BS1_13TQ, CAN_BS2_2TQ}, // 500K, 87.5%
  { 5, CAN_SJW_1TQ, CAN_BS1_13TQ, CAN_BS2_2TQ}, // 1M,   87.5%
};

const can_baud_cfg_t *p_baud_normal = can_baud_cfg_80m_normal;
const can_baud_cfg_t *p_baud_data   = can_baud_cfg_80m_data;


const uint32_t dlc_len_tbl[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};

const uint32_t dlc_tbl[] = {0, 1, 2, 3, 4, 5, 6, 7, 8,
      8, 8, 8, 8, 8, 8, 8 };

static const uint32_t mode_tbl[] =
{
  CAN_MODE_NORMAL,
  CAN_MODE_SILENT,
  CAN_MODE_LOOPBACK
};


typedef struct
{
  bool is_init;
  bool is_open;

  uint32_t err_code;
  uint8_t  state;
  uint32_t recovery_cnt;

  uint32_t q_rx_full_cnt;
  uint32_t q_tx_full_cnt;
  uint32_t fifo_full_cnt;
  uint32_t fifo_lost_cnt;

  uint32_t   fifo_idx;
  uint32_t   enable_int;
  CanMode_t  mode;
  CanFrame_t frame;
  CanBaud_t  baud;
  CanBaud_t  baud_data;

  uint32_t rx_cnt;
  uint32_t tx_cnt;
  uint32_t pre_time;

  CAN_HandleTypeDef *h_can;
  bool (*handler)(uint8_t ch, CanEvent_t evt, can_msg_t *arg);

  qbuffer_t q_msg;
  can_msg_t can_msg[CAN_MSG_RX_BUF_MAX];
} can_tbl_t;


CAN_HandleTypeDef hcan1;

static can_tbl_t can_tbl[CAN_MAX_CH];

static volatile uint32_t err_int_cnt = 0;



#ifdef _USE_HW_CLI
static void cliCan(cli_args_t *args);
#endif

static void canErrUpdate(uint8_t ch);




bool canInit(void)
{
  bool ret = true;

  uint8_t i;

  for(i = 0; i < CAN_MAX_CH; i++)
  {
    can_tbl[i].is_init  = true;
    can_tbl[i].is_open  = false;

    can_tbl[i].err_code = CAN_ERR_NONE;
    can_tbl[i].state    = 0;
    can_tbl[i].recovery_cnt = 0;

    can_tbl[i].q_rx_full_cnt = 0;
    can_tbl[i].q_tx_full_cnt = 0;
    can_tbl[i].fifo_full_cnt = 0;
    can_tbl[i].fifo_lost_cnt = 0;

    qbufferCreateBySize(&can_tbl[i].q_msg, (uint8_t *)&can_tbl[i].can_msg[0], sizeof(can_msg_t), CAN_MSG_RX_BUF_MAX);
  }

  hcan1.Instance = CAN1;
  can_tbl[_DEF_CAN1].h_can = &hcan1;

// logPrintf("[OK] canInit()\n");

#ifdef _USE_HW_CLI
  cliAdd("can", cliCan);
#endif
  return ret;
}

bool canLock(void)
{
  bool ret = true;
  return ret;
}

bool canUnLock(void)
{
  return true;
}

bool canOpen(uint8_t ch, CanMode_t mode, CanFrame_t frame, CanBaud_t baud, CanBaud_t baud_data)
{
  bool ret = true;
  CAN_HandleTypeDef  *p_can;

  if (ch >= CAN_MAX_CH) return false;

  p_can = can_tbl[ch].h_can;

  switch(ch)
  {
    case _DEF_CAN1:
      p_can->Init.Mode                 = mode_tbl[mode];
      p_can->Init.AutoRetransmission   = ENABLE;
      p_can->Init.Prescaler            = p_baud_normal[baud].prescaler;
      p_can->Init.SyncJumpWidth        = p_baud_normal[baud].sjw;
      p_can->Init.TimeSeg1             = p_baud_normal[baud].tseg1;
      p_can->Init.TimeSeg2             = p_baud_normal[baud].tseg2;
      p_can->Init.TimeTriggeredMode    = DISABLE;
      p_can->Init.AutoBusOff           = DISABLE;
      p_can->Init.AutoWakeUp           = DISABLE;
      p_can->Init.ReceiveFifoLocked    = DISABLE;
      p_can->Init.TransmitFifoPriority = DISABLE;

      can_tbl[ch].mode       = mode;
      can_tbl[ch].frame      = frame;
      can_tbl[ch].baud       = baud;
      can_tbl[ch].baud_data  = baud_data;
      can_tbl[ch].fifo_idx   = CAN_RX_FIFO0;
      can_tbl[ch].enable_int = CAN_IT_RX_FIFO0_MSG_PENDING |
                               CAN_IT_BUSOFF |
                               CAN_IT_ERROR_WARNING |
                               CAN_IT_ERROR_PASSIVE |
                               CAN_IT_LAST_ERROR_CODE |
                               CAN_IT_ERROR;

      can_tbl[ch].err_code     = CAN_ERR_NONE;
      can_tbl[ch].recovery_cnt = 0;


      can_tbl[ch].err_code     = CAN_ERR_NONE;
      can_tbl[ch].recovery_cnt = 0;
      ret = true;
      break;
  }

  if (ret != true)
  {
    return false;
  }

  if (can_tbl[ch].is_open)
  {
    HAL_CAN_ResetError(p_can);
    HAL_CAN_DeInit(p_can);
  }

  can_tbl[ch].is_open = false;

  if (HAL_CAN_Init(p_can) != HAL_OK)
  {
    return false;
  }

  canConfigFilter(ch, 0, CAN_STD, CAN_ID_MASK, 0x0000, 0x0000);

  if (HAL_CAN_ActivateNotification(p_can, can_tbl[ch].enable_int) != HAL_OK)
  {
    return false;
  }
  if (HAL_CAN_Start(p_can) != HAL_OK)
  {
    return false;
  }

  can_tbl[ch].is_open = true;

  return ret;
}

bool canIsOpen(uint8_t ch)
{
  if(ch >= CAN_MAX_CH) return false;

  return can_tbl[ch].is_open;
}

void canClose(uint8_t ch)
{
  if(ch >= CAN_MAX_CH) return;

  if (can_tbl[ch].is_open)
  {
    can_tbl[ch].is_open = false;
    HAL_CAN_DeInit(can_tbl[ch].h_can);
  }
  return;
}

bool canGetInfo(uint8_t ch, can_info_t *p_info)
{
  if(ch >= CAN_MAX_CH) return false;

  p_info->baud = can_tbl[ch].baud;
  p_info->baud_data = can_tbl[ch].baud_data;
  p_info->frame = can_tbl[ch].frame;
  p_info->mode = can_tbl[ch].mode;

  return true;
}

CanDlc_t canGetDlc(uint8_t length)
{
  CanDlc_t ret;

  if (length >= 64)
    ret = CAN_DLC_64;
  else if (length >= 48)
    ret = CAN_DLC_48;
  else if (length >= 32)
    ret = CAN_DLC_32;
  else if (length >= 24)
    ret = CAN_DLC_24;
  else if (length >= 20)
    ret = CAN_DLC_20;
  else if (length >= 16)
    ret = CAN_DLC_16;
  else if (length >= 12)
    ret = CAN_DLC_12;
  else if (length >= 8)
    ret = CAN_DLC_8;
  else
    ret = (CanDlc_t)length;

  return ret;
}

uint8_t canGetLen(CanDlc_t dlc)
{
  return dlc_len_tbl[(int)dlc];
}

bool canConfigFilter(uint8_t ch,
                     uint8_t index,
                     CanIdType_t id_type,
                     CanFilterType_t ft_type,
                     uint32_t id,
                     uint32_t id_mask)
{
  bool              ret = false;
  CAN_FilterTypeDef sFilterConfig;

  if (ch >= CAN_MAX_CH) return false;
  // assert(index <= 13);
  // assert(ft_type <= CAN_ID_LIST);

  if (can_tbl[ch].h_can->Instance == CAN1)
  {
    sFilterConfig.FilterBank = index;
  }
  else
  {
    sFilterConfig.FilterBank = 14 + index;
  }
  sFilterConfig.FilterScale          = CAN_FILTERSCALE_32BIT;
  sFilterConfig.FilterFIFOAssignment = can_tbl[ch].fifo_idx;
  sFilterConfig.FilterActivation     = ENABLE;
  sFilterConfig.SlaveStartFilterBank = 14;


  if (id_type == CAN_STD)
  {
    sFilterConfig.FilterIdHigh     = ((id << 5) & 0xFFFF);
    sFilterConfig.FilterIdLow      = 0;
    sFilterConfig.FilterMaskIdHigh = ((id_mask << 5) & 0xFFFF);
    sFilterConfig.FilterMaskIdLow  = 0;
  }
  else
  {
    sFilterConfig.FilterIdHigh     = ((id >> 13) & 0xFFFF);
    sFilterConfig.FilterIdLow      = ((id << 3) & 0xFFF8);
    sFilterConfig.FilterMaskIdHigh = ((id_mask >> 13) & 0xFFFF);
    sFilterConfig.FilterMaskIdLow  = ((id_mask << 3) & 0xFFF8);
  }

  if (ft_type == CAN_ID_MASK)
    sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
  else
    sFilterConfig.FilterMode = CAN_FILTERMODE_IDLIST;


  if (HAL_CAN_ConfigFilter(can_tbl[ch].h_can, &sFilterConfig) == HAL_OK)
  {
    ret = true;
  }

  return ret;
}

uint32_t canMsgAvailable(uint8_t ch)
{
  if(ch > CAN_MAX_CH) return 0;

  return qbufferAvailable(&can_tbl[ch].q_msg);
}

bool canMsgInit(can_msg_t *p_msg, CanFrame_t frame, CanIdType_t  id_type, CanDlc_t dlc)
{
  p_msg->frame   = frame;
  p_msg->id_type = id_type;
  p_msg->dlc     = dlc;
  p_msg->length  = dlc_len_tbl[dlc];
  return true;
}

bool canMsgWrite(uint8_t ch, can_msg_t *p_msg, uint32_t timeout)
{
  CAN_HandleTypeDef  *p_can;
  CAN_TxHeaderTypeDef tx_header;
  uint32_t pre_time;
  uint32_t tx_mailbox;
  bool ret = true;


  if(ch > CAN_MAX_CH) return false;

  if (can_tbl[ch].err_code & CAN_ERR_BUS_OFF) return false;

  p_can = can_tbl[ch].h_can;

  switch(p_msg->id_type)
  {
    case CAN_STD :
      tx_header.StdId = p_msg->id & 0x7FF;
      tx_header.ExtId = p_msg->id & 0x1FFFFFFF;
      tx_header.IDE   = CAN_ID_STD;
      break;

    case CAN_EXT :
      tx_header.StdId = p_msg->id & 0x7FF;
      tx_header.ExtId = p_msg->id & 0x1FFFFFFF;
      tx_header.IDE   = CAN_ID_EXT;
      break;
  }
  tx_header.DLC = dlc_tbl[p_msg->dlc];
  tx_header.RTR = CAN_RTR_DATA;
  tx_header.TransmitGlobalTime = DISABLE;

  if (HAL_CAN_GetTxMailboxesFreeLevel(p_can) == 0)
  {
    can_tbl[ch].fifo_full_cnt++;
    return false;
  }

  pre_time = millis();
  if(HAL_CAN_AddTxMessage(p_can, &tx_header, p_msg->data, &tx_mailbox) == HAL_OK)
  {
    /* Wait transmission complete */
    while(HAL_CAN_GetTxMailboxesFreeLevel(p_can) == 0)
    {
      if (millis()-pre_time >= timeout)
      {
        ret = false;
        break;
      }
#ifdef _USE_HW_RTOS
      osThreadYield();
#endif
    }
  }
  else
  {
    ret = false;
  }

  return ret;
}

bool canMsgRead(uint8_t ch, can_msg_t *p_msg)
{
  bool ret = true;


  if(ch > CAN_MAX_CH) return 0;

  ret = qbufferRead(&can_tbl[ch].q_msg, (uint8_t *)p_msg, 1);

  return ret;
}

uint16_t canGetRxErrCount(uint8_t ch)
{
  uint16_t ret = 0;

  if(ch > CAN_MAX_CH) return 0;

  ret = (can_tbl[ch].h_can->Instance->ESR >> 24) & 0xFF;

  return ret;
}

uint16_t canGetTxErrCount(uint8_t ch)
{
  uint16_t ret = 0;

  if(ch > CAN_MAX_CH) return 0;

  ret = (can_tbl[ch].h_can->Instance->ESR >> 16) & 0xFF;

  return ret;
}

uint32_t canGetError(uint8_t ch)
{
  if(ch > CAN_MAX_CH) return 0;

  return can_tbl[ch].err_code;
}

uint32_t canGetRxCount(uint8_t ch)
{
  if(ch >= CAN_MAX_CH) return 0;

  return can_tbl[ch].rx_cnt;
}

uint32_t canGetTxCount(uint8_t ch)
{
  if(ch >= CAN_MAX_CH) return 0;

  return can_tbl[ch].tx_cnt;
}

uint32_t canGetState(uint8_t ch)
{
  if(ch > CAN_MAX_CH) return 0;

  return 0;
}

void canAttachRxInterrupt(uint8_t ch, bool (*handler)(uint8_t ch, CanEvent_t evt, can_msg_t *arg))
{
  if(ch > CAN_MAX_CH) return;

  can_tbl[ch].handler = handler;
}

void canDetachRxInterrupt(uint8_t ch)
{
  if(ch > CAN_MAX_CH) return;

  can_tbl[ch].handler = NULL;
}

void canRecovery(uint8_t ch)
{
  if (ch > CAN_MAX_CH) return;
  if (can_tbl[ch].is_open != true) return;

  can_tbl[ch].err_code             = CAN_ERR_NONE;
  can_tbl[ch].h_can->Instance->ESR = 0;

  HAL_CAN_ResetError(can_tbl[ch].h_can);
  HAL_CAN_Stop(can_tbl[ch].h_can);
  HAL_CAN_Start(can_tbl[ch].h_can);
}

bool canIsRecoveryFail(uint8_t ch)
{
  return can_tbl[ch].recovery_cnt == CAN_RECOVERY_FAIL_CNT_MAX ? true:false;
}

bool canUpdate(void)
{
  enum
  {
    CAN_STATE_IDLE,
    CAN_STATE_WAIT
  };
  bool ret = false;
  can_tbl_t *p_can;


  for (int i=0; i<CAN_MAX_CH; i++)
  {
    p_can = &can_tbl[i];


    canErrUpdate(i);

    switch(p_can->state)
    {
      case CAN_STATE_IDLE:
        if (p_can->err_code & CAN_ERR_PASSIVE)
        {
          canRecovery(i);
          can_tbl[i].recovery_cnt++;
          can_tbl[i].pre_time = millis();
          p_can->state = CAN_STATE_WAIT;
          ret = true;
        }
        break;

      case CAN_STATE_WAIT:
        if ((p_can->err_code & CAN_ERR_PASSIVE) == 0)
        {
          p_can->state = CAN_STATE_IDLE;
        }
        if (millis()-can_tbl[i].pre_time >= 1000)
        {
          can_tbl[i].pre_time = millis();
          if (can_tbl[i].recovery_cnt < CAN_RECOVERY_FAIL_CNT_MAX)
          {
            canRecovery(i);
            can_tbl[i].recovery_cnt++;
          }
        }
        if (can_tbl[i].recovery_cnt == 0)
        {
          p_can->state = CAN_STATE_IDLE;
        }
        break;
    }
  }

  return ret;
}

void canRxFifoCallback(uint8_t ch, CAN_HandleTypeDef *h_can)
{
  can_msg_t *rx_buf;
  CAN_RxHeaderTypeDef rx_header;


  rx_buf  = (can_msg_t *)qbufferPeekWrite(&can_tbl[ch].q_msg);

  if (HAL_CAN_GetRxMessage(h_can, can_tbl[ch].fifo_idx, &rx_header, rx_buf->data) == HAL_OK)
  {
    if(rx_header.IDE == CAN_ID_STD)
    {
      rx_buf->id      = rx_header.StdId;
      rx_buf->id_type = CAN_STD;
    }
    else
    {
      rx_buf->id      = rx_header.ExtId;
      rx_buf->id_type = CAN_EXT;
    }
    rx_buf->length = rx_header.DLC;
    rx_buf->dlc = canGetDlc(rx_buf->length);
    rx_buf->frame = CAN_CLASSIC;
    rx_buf->timestamp = millis();

    can_tbl[ch].rx_cnt++;

    if (qbufferWrite(&can_tbl[ch].q_msg, NULL, 1) != true)
    {
      can_tbl[ch].q_rx_full_cnt++;
    }

    if( can_tbl[ch].handler != NULL )
    {
      if ((*can_tbl[ch].handler)(ch, CAN_EVT_MSG, (void *)rx_buf) == true)
      {
        qbufferRead(&can_tbl[ch].q_msg, NULL, 1);
      }
    }
  }
}

void canErrClear(uint8_t ch)
{
  if(ch > CAN_MAX_CH) return;

  can_tbl[ch].err_code = CAN_ERR_NONE;
}

void canErrPrint(uint8_t ch)
{
  uint32_t err_code;


  if(ch > CAN_MAX_CH) return;

  err_code = can_tbl[ch].err_code;

  if (err_code & CAN_ERR_PASSIVE)   logPrintf("  ERR : CAN_ERR_PASSIVE\n");
  if (err_code & CAN_ERR_WARNING)   logPrintf("  ERR : CAN_ERR_WARNING\n");
  if (err_code & CAN_ERR_BUS_OFF)   logPrintf("  ERR : CAN_ERR_BUS_OFF\n");
  if (err_code & CAN_ERR_BUS_FAULT) logPrintf("  ERR : CAN_ERR_BUS_FAULT\n");
  if (err_code & CAN_ERR_ETC)       logPrintf("  ERR : CAN_ERR_BUS_ETC\n");
  if (err_code & CAN_ERR_MSG)       logPrintf("  ERR : CAN_ERR_BUS_MSG\n");

  logPrintf("  ESR : 0x%lX\n", can_tbl[ch].h_can->Instance->ESR );
}

void canErrUpdate(uint8_t ch)
{
  CanEvent_t can_evt = CAN_EVT_NONE;

  if (can_tbl[ch].h_can->Instance->ESR & (1<<1))
  {
    can_tbl[ch].err_code |= CAN_ERR_PASSIVE;
    can_evt = CAN_EVT_ERR_PASSIVE;
  }
  else
  {
    can_tbl[ch].err_code &= ~CAN_ERR_PASSIVE;
  }

  if (can_tbl[ch].h_can->Instance->ESR & (1<<0))
  {
    can_tbl[ch].err_code |= CAN_ERR_WARNING;
    can_evt = CAN_EVT_ERR_WARNING;
  }
  else
  {
    can_tbl[ch].err_code &= ~CAN_ERR_WARNING;
  }

  if (can_tbl[ch].h_can->Instance->ESR & (1<<2))
  {
    can_tbl[ch].err_code |= CAN_ERR_BUS_OFF;
    can_evt = CAN_EVT_ERR_BUS_OFF;
  }
  else
  {
    can_tbl[ch].err_code &= ~CAN_ERR_BUS_OFF;
  }

  if (can_tbl[ch].h_can->Instance->ESR & (7<<4))
  {
    can_tbl[ch].err_code |= CAN_ERR_MSG;
    can_evt = CAN_EVT_ERR_MSG;
  }
  else
  {
    can_tbl[ch].err_code &= ~CAN_ERR_MSG;
  }


  if( can_tbl[ch].handler != NULL)
  {
    if (can_evt != CAN_EVT_NONE)
    {
      (*can_tbl[ch].handler)(ch, can_evt, NULL);
    }
  }
}

void canInfoPrint(uint8_t ch)
{
  can_tbl_t *p_can = &can_tbl[ch];

  #ifdef _USE_HW_CLI
  #define canPrintf   cliPrintf
  #else
  #define canPrintf   logPrintf
  #endif

  canPrintf("ch            : ");
  switch(ch)
  {
    case _DEF_CAN1:
      canPrintf("_DEF_CAN1\n");
      break;
    case _DEF_CAN2:
      canPrintf("_DEF_CAN2\n");
      break;
  }

  canPrintf("is_open       : ");
  if (p_can->is_open)
    canPrintf("true\n");
  else
    canPrintf("false\n");

  canPrintf("baud          : ");
  switch(p_can->baud)
  {
    case CAN_100K:
      canPrintf("100K\n");
      break;
    case CAN_125K:
      canPrintf("125K\n");
      break;
    case CAN_250K:
      canPrintf("250K\n");
      break;
    case CAN_500K:
      canPrintf("500K\n");
      break;
    case CAN_1M:
      canPrintf("1M\n");
      break;
    default:
      break;
  }

  canPrintf("baud data     : ");
  switch(p_can->baud_data)
  {
    case CAN_100K:
      canPrintf("100K\n");
      break;
    case CAN_125K:
      canPrintf("125K\n");
      break;
    case CAN_250K:
      canPrintf("250K\n");
      break;
    case CAN_500K:
      canPrintf("500K\n");
      break;
    case CAN_1M:
      canPrintf("1M\n");
      break;

    case CAN_2M:
      canPrintf("2M\n");
      break;
    case CAN_4M:
      canPrintf("4M\n");
      break;
    case CAN_5M:
      canPrintf("5M\n");
      break;
  }

  canPrintf("mode          : ");
  switch(p_can->mode)
  {
    case CAN_NORMAL:
      canPrintf("NORMAL\n");
      break;
    case CAN_MONITOR:
      canPrintf("MONITOR\n");
      break;
    case CAN_LOOPBACK:
      canPrintf("LOOPBACK\n");
      break;
  }

  canPrintf("frame         : ");
  switch(p_can->frame)
  {
    case CAN_CLASSIC:
      canPrintf("CAN_CLASSIC\n");
      break;
    case CAN_FD_NO_BRS:
      canPrintf("CAN_FD_NO_BRS\n");
      break;
    case CAN_FD_BRS:
      canPrintf("CAN_FD_BRS\n");
      break;
  }
}

void canErrorCallback(uint8_t ch)
{
  err_int_cnt++;

  canErrUpdate(ch);
}

void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *hcan)
{
  uint8_t ch = _DEF_CAN1;

  err_int_cnt++;

  if (hcan->ErrorCode > 0)
  {
    can_tbl[ch].err_code |= CAN_ERR_ETC;
  }

  canErrUpdate(ch);
}

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
  for (int i=0; i<CAN_MAX_CH; i++)
  {
    if (hcan == can_tbl[i].h_can)
    {
      canRxFifoCallback(i, hcan);
    }
  }
}

void HAL_CAN_MspInit(CAN_HandleTypeDef* canHandle)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  if(canHandle->Instance==CAN1)
  {
    __HAL_RCC_CAN1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitStruct.Pin = GPIO_PIN_11|GPIO_PIN_12;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF9_CAN1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    HAL_NVIC_SetPriority(CAN1_TX_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(CAN1_TX_IRQn);
    HAL_NVIC_SetPriority(CAN1_RX0_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(CAN1_RX0_IRQn);
    HAL_NVIC_SetPriority(CAN1_SCE_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(CAN1_SCE_IRQn);
  }
}

void HAL_CAN_MspDeInit(CAN_HandleTypeDef* canHandle)
{
  if(canHandle->Instance==CAN1)
  {
    __HAL_RCC_CAN1_CLK_DISABLE();

    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_11|GPIO_PIN_12);

    HAL_NVIC_DisableIRQ(CAN1_TX_IRQn);
    HAL_NVIC_DisableIRQ(CAN1_RX0_IRQn);
    HAL_NVIC_DisableIRQ(CAN1_SCE_IRQn);
  }
}

#ifdef _USE_HW_CLI
void cliCan(cli_args_t *args)
{
  bool ret = false;


   canLock();

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    for (int i=0; i<CAN_MAX_CH; i++)
    {
      if (can_tbl[i].is_open == true)
      {
        canInfoPrint(i);
        cliPrintf("is_open       : %d\n", can_tbl[i].is_open);

        cliPrintf("q_rx_full_cnt : %d\n", can_tbl[i].q_rx_full_cnt);
        cliPrintf("q_tx_full_cnt : %d\n", can_tbl[i].q_tx_full_cnt);
        cliPrintf("fifo_full_cnt : %d\n", can_tbl[i].fifo_full_cnt);
        cliPrintf("fifo_lost_cnt : %d\n", can_tbl[i].fifo_lost_cnt);
        cliPrintf("rx error cnt  : %d\n", canGetRxErrCount(i));
        cliPrintf("tx error cnt  : %d\n", canGetTxErrCount(i));
        cliPrintf("recovery cnt  : %d\n", can_tbl[i].recovery_cnt);
        cliPrintf("err code      : 0x%X\n", can_tbl[i].err_code);
        canErrPrint(i);
        cliPrintf("\n");
      }
      else
      {
        cliPrintf("%d not open\n", i);
        cliPrintf("\n");
      }
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "open"))
  {
    cliPrintf("ch    : 0~%d\n\n", CAN_MAX_CH - 1);
    cliPrintf("mode  : CAN_NORMAL\n");
    cliPrintf("        CAN_MONITOR\n");
    cliPrintf("        CAN_LOOPBACK\n\n");
    cliPrintf("frame : CAN_CLASSIC\n");
    cliPrintf("        CAN_FD_NO_BRS\n");
    cliPrintf("        CAN_FD_BRS\n\n");
    cliPrintf("baud  : CAN_100K\n");
    cliPrintf("        CAN_125K\n");
    cliPrintf("        CAN_250K\n");
    cliPrintf("        CAN_500K\n");
    cliPrintf("        CAN_1M\n");
    cliPrintf("        CAN_2M\n");
    cliPrintf("        CAN_4M\n");
    cliPrintf("        CAN_5M\n");
    ret = true;
  }

  if (args->argc == 3 && args->isStr(0, "open") && args->isStr(1, "test"))
  {
    bool can_ret;
    uint8_t ch;

    ch = constrain(args->getData(2), 0, CAN_MAX_CH - 1);

    can_ret = canOpen(ch,
                      CAN_NORMAL,
                      CAN_CLASSIC,
                      CAN_500K,
                      CAN_500K);
    cliPrintf("canOpen() : %s\n", can_ret ? "True":"False");
    canInfoPrint(ch);
    ret = true;
  }

  if (args->argc == 6 && args->isStr(0, "open"))
  {
    uint8_t ch;
    CanMode_t mode = CAN_NORMAL;
    CanFrame_t frame = CAN_CLASSIC;
    CanBaud_t baud = CAN_500K;
    CanBaud_t baud_data = CAN_500K;
    const char *mode_str[]  = {"CLAN_NORMAL", "CAN_MONITOR", "CAN_LOOPBACK"};
    const char *frame_str[] = {"CAN_CLASSIC", "CAN_FD_NO_BRS", "CAN_FD_BRS"};
    const char *baud_str[]  = {"CAN_100K", "CAN_125K", "CAN_250K", "CAN_500K", "CAN_1M", "CAN_2M", "CAN_4M", "CAN_5M"};

    ch = constrain(args->getData(1), 0, CAN_MAX_CH - 1);

    for (int i=0; i<3; i++)
    {
      if (args->isStr(2, mode_str[i]))
      {
        mode = i;
        break;
      }
    }
    for (int i=0; i<3; i++)
    {
      if (args->isStr(3, frame_str[i]))
      {
        frame = i;
        break;
      }
    }
    for (int i=0; i<8; i++)
    {
      if (args->isStr(4, baud_str[i]))
      {
        baud = i;
        break;
      }
    }
    for (int i=0; i<8; i++)
    {
      if (args->isStr(5, baud_str[i]))
      {
        baud_data = i;
        break;
      }
    }

    bool can_ret;

    can_ret = canOpen(ch, mode, frame, baud, baud_data);
    cliPrintf("canOpen() : %s\n", can_ret ? "True":"False");
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "read_test"))
  {
    uint32_t index = 0;
    uint8_t ch;

    ch = constrain(args->getData(1), 0, CAN_MAX_CH - 1);


    while(cliKeepLoop())
    {
      if (canMsgAvailable(ch))
      {
        can_msg_t msg;

        canMsgRead(ch, &msg);

        index %= 1000;
        cliPrintf("ch %d %03d(R) <- id ",ch, index++);
        if (msg.frame != CAN_CLASSIC)
        {
          cliPrintf("fd ");
        }
        else
        {
          cliPrintf("   ");
        }
        if (msg.id_type == CAN_STD)
        {
          cliPrintf("std ");
        }
        else
        {
          cliPrintf("ext ");
        }
        cliPrintf(": 0x%08X, L:%02d, ", msg.id, msg.length);
        for (int i=0; i<msg.length; i++)
        {
          cliPrintf("0x%02X ", msg.data[i]);
        }
        cliPrintf("\n");
      }
    }
    ret = true;
  }

  if (args->argc == 3 && args->isStr(0, "send_test"))
  {
    uint32_t pre_time;
    uint32_t index = 0;
    uint32_t err_code;
    uint8_t ch;
    CanFrame_t frame;
    uint32_t rx_err_cnt = 0;
    uint32_t tx_err_cnt = 0;

    ch = constrain(args->getData(1), 0, CAN_MAX_CH - 1);

    if (args->isStr(2, "can"))
      frame = CAN_CLASSIC;
    else
      frame = CAN_FD_BRS;

    err_code = can_tbl[_DEF_CAN1].err_code;

    pre_time = millis();
    while(cliKeepLoop())
    {
      can_msg_t msg;

      if (millis()-pre_time >= 500)
      {
        pre_time = millis();

        msg.frame   = frame;
        msg.id_type = CAN_EXT;
        msg.dlc     = CAN_DLC_2;
        msg.id      = 0x314;
        msg.length  = 2;
        msg.data[0] = 1;
        msg.data[1] = 2;
        if (canMsgWrite(ch, &msg, 10) == true)
        {
          index %= 1000;
          cliPrintf("ch %d %03d(T) -> id ", ch, index++);
          if (msg.frame != CAN_CLASSIC)
          {
            cliPrintf("fd ");
          }
          else
          {
            cliPrintf("   ");
          }

          if (msg.id_type == CAN_STD)
          {
            cliPrintf("std ");
          }
          else
          {
            cliPrintf("ext ");
          }
          cliPrintf(": 0x%08X, L:%02d, ", msg.id, msg.length);
          for (int i=0; i<msg.length; i++)
          {
            cliPrintf("0x%02X ", msg.data[i]);
          }
          cliPrintf("\n");
        }

        if (rx_err_cnt != canGetRxErrCount(ch) || tx_err_cnt != canGetTxErrCount(ch))
        {
          cliPrintf("ch %d ErrCnt : %d, %d\n", ch, canGetRxErrCount(ch), canGetTxErrCount(ch));
          rx_err_cnt = canGetRxErrCount(ch);
          tx_err_cnt = canGetTxErrCount(ch);
        }

        if (err_int_cnt > 0)
        {
          cliPrintf("ch %d Cnt : %d\n", ch, err_int_cnt);
          err_int_cnt = 0;
        }
      }

      if (can_tbl[ch].err_code != err_code)
      {
        cliPrintf("ch %d ErrCode : 0x%X\n", ch, can_tbl[ch].err_code);
        canErrPrint(ch);
        err_code = can_tbl[ch].err_code;
      }

      if (canUpdate())
      {
        cliPrintf("ch %d BusOff Recovery\n", ch);
      }


      if (canMsgAvailable(ch))
      {
        canMsgRead(ch, &msg);

        index %= 1000;
        cliPrintf("ch %d %03d(R) <- id ", ch, index++);
        if (msg.frame != CAN_CLASSIC)
        {
          cliPrintf("fd ");
        }
        else
        {
          cliPrintf("   ");
        }
        if (msg.id_type == CAN_STD)
        {
          cliPrintf("std ");
        }
        else
        {
          cliPrintf("ext ");
        }
        cliPrintf(": 0x%08X, L:%02d, ", msg.id, msg.length);
        for (int i=0; i<msg.length; i++)
        {
          cliPrintf("0x%02X ", msg.data[i]);
        }
        cliPrintf("\n");
      }
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "bamocar_status"))
  {
    uint8_t ch = _DEF_CAN1;
    uint32_t pre_time;
    bool found = false;
    can_msg_t msg;

    if (canIsOpen(ch) != true)
    {
      if (canOpen(ch, CAN_NORMAL, CAN_CLASSIC, CAN_500K, CAN_500K) != true)
      {
        cliPrintf("canOpen() : False\n");
        canUnLock();
        return;
      }
    }

    if (canConfigFilter(ch, 0, CAN_STD, CAN_ID_MASK, CAN_BAMOCAR_RSP_ID, 0x7FF) != true)
    {
      cliPrintf("canConfigFilter() : False\n");
      canUnLock();
      return;
    }

    canMsgInit(&msg, CAN_CLASSIC, CAN_STD, CAN_DLC_3);
    msg.id      = CAN_BAMOCAR_CMD_ID;
    msg.data[0] = CAN_BAMOCAR_STATUS_REQ;
    msg.data[1] = CAN_BAMOCAR_STATUS_REG;
    msg.data[2] = 0x00;

    cliPrintf("TX -> id std: 0x%03X, L:%02d, 0x%02X 0x%02X 0x%02X\n",
              (unsigned int)msg.id,
              msg.length,
              msg.data[0],
              msg.data[1],
              msg.data[2]);

    if (canMsgWrite(ch, &msg, 10) != true)
    {
      cliPrintf("canMsgWrite() : False\n");
      canUnLock();
      return;
    }

    pre_time = millis();
    while (millis() - pre_time < CAN_BAMOCAR_STATUS_TIMEOUT)
    {
      canUpdate();

      if (canMsgAvailable(ch))
      {
        can_msg_t rx_msg;

        canMsgRead(ch, &rx_msg);

        cliPrintf("RX <- id ");
        if (rx_msg.id_type == CAN_STD)
        {
          cliPrintf("std ");
        }
        else
        {
          cliPrintf("ext ");
        }
        cliPrintf(": 0x%08X, L:%02d, ", (unsigned int)rx_msg.id, rx_msg.length);
        for (int i=0; i<rx_msg.length; i++)
        {
          cliPrintf("0x%02X ", rx_msg.data[i]);
        }
        cliPrintf("\n");

        if (rx_msg.id == CAN_BAMOCAR_RSP_ID && rx_msg.length >= 3 && rx_msg.data[0] == CAN_BAMOCAR_STATUS_REG)
        {
          uint16_t status;

          status = rx_msg.data[1] | (rx_msg.data[2] << 8);
          cliPrintf("bamocar status : 0x%04X\n", (unsigned int)status);
          found = true;
          break;
        }
      }
    }

    if (found != true)
    {
      cliPrintf("bamocar status timeout\n");
    }

    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "recovery"))
  {
    uint8_t ch;

    ch = constrain(args->getData(1), 0, CAN_MAX_CH - 1);

    canRecovery(ch);
    ret = true;
  }

  canUnLock();

  if (ret == false)
  {
    cliPrintf("can info\n");
    cliPrintf("can open\n");
    cliPrintf("can open ch[0~%d] mode frame baud fd_baud\n", CAN_MAX_CH-1);
    cliPrintf("can open test ch[0~%d]\n", CAN_MAX_CH-1);
    cliPrintf("can read_test ch[0~%d]\n", CAN_MAX_CH-1);
    cliPrintf("can send_test ch[0~%d] can:fd\n", CAN_MAX_CH-1);
    cliPrintf("can bamocar_status\n");
    cliPrintf("can recovery ch[0~%d]\n", CAN_MAX_CH-1);
  }
}
#endif

#endif
