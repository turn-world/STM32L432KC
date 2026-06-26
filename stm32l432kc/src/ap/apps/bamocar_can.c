/*
 * bamocar_can.c
 *
 * Bamocar CAN protocol helper.
 * APPS decides the requested torque; this file only packs/sends Bamocar
 * register frames over the existing CAN driver.
 */

#include "bamocar_can.h"

void bamocarCanMakeReg16(uint32_t rx_id, uint8_t reg_id, int16_t value, bamocar_can_frame_t *p_frame)
{
  uint16_t raw_value = (uint16_t)value;

  p_frame->id = rx_id;
  p_frame->dlc = 3;
  p_frame->data[0] = reg_id;
  p_frame->data[1] = (uint8_t)(raw_value & 0xFFU);
  p_frame->data[2] = (uint8_t)((raw_value >> 8) & 0xFFU);
  p_frame->data[3] = 0;
  p_frame->data[4] = 0;
  p_frame->data[5] = 0;
  p_frame->data[6] = 0;
  p_frame->data[7] = 0;
}

void bamocarCanMakeTorqueCmd(uint32_t rx_id, int16_t torque_cmd, bamocar_can_frame_t *p_frame)
{
  bamocarCanMakeReg16(rx_id, BAMOCAR_REG_TORQUE_CMD, torque_cmd, p_frame);
}

void bamocarCanMakeReadRequest(uint32_t rx_id, uint8_t reg_id, uint8_t interval_ms, bamocar_can_frame_t *p_frame)
{
  p_frame->id = rx_id;
  p_frame->dlc = 3;
  p_frame->data[0] = BAMOCAR_REG_READ_REQUEST;
  p_frame->data[1] = reg_id;
  p_frame->data[2] = interval_ms;
  p_frame->data[3] = 0;
  p_frame->data[4] = 0;
  p_frame->data[5] = 0;
  p_frame->data[6] = 0;
  p_frame->data[7] = 0;
}

bool bamocarCanSendTorqueCmd(uint8_t can_ch, int16_t torque_cmd)
{
  bamocar_can_frame_t frame;
  can_msg_t msg;

  if (canIsOpen(can_ch) != true)
  {
    return false;
  }

  bamocarCanMakeTorqueCmd(BAMOCAR_CAN_DEFAULT_RX_ID, torque_cmd, &frame);

  canMsgInit(&msg, CAN_CLASSIC, CAN_STD, CAN_DLC_3);
  msg.id = frame.id;
  msg.data[0] = frame.data[0];
  msg.data[1] = frame.data[1];
  msg.data[2] = frame.data[2];

  /* Usually returns immediately; waits at most 1 ms if all mailboxes are busy. */
  return canMsgWrite(can_ch, &msg, 1U);
}
