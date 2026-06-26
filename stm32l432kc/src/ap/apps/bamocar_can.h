/*
 * bamocar_can.h
 *
 * Minimal helpers for UniTek BAMOCAR D3 CAN command frames.
 * This module keeps Bamocar register IDs, CAN IDs, and byte packing out of
 * APPS control logic.
 */

#ifndef SRC_AP_APPS_BAMOCAR_CAN_H_
#define SRC_AP_APPS_BAMOCAR_CAN_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"
#include "can.h"

#define BAMOCAR_CAN_DEFAULT_RX_ID      0x201U
#define BAMOCAR_CAN_DEFAULT_TX_ID      0x181U
#define BAMOCAR_REG_SPEED_CMD          0x31U
#define BAMOCAR_REG_TORQUE_CMD         0x90U
#define BAMOCAR_REG_STATUS             0x40U
#define BAMOCAR_REG_READ_REQUEST       0x3DU
#define BAMOCAR_TORQUE_CMD_MAX         32767

typedef struct
{
  uint32_t id;
  uint8_t dlc;
  uint8_t data[8];
} bamocar_can_frame_t;

/* Build a 16-bit Bamocar register write frame. */
void bamocarCanMakeReg16(uint32_t rx_id, uint8_t reg_id, int16_t value,
                         bamocar_can_frame_t *p_frame);

/* Build a torque-command register frame. */
void bamocarCanMakeTorqueCmd(uint32_t rx_id, int16_t torque_cmd,
                             bamocar_can_frame_t *p_frame);

/* Build a periodic register-read request frame. */
void bamocarCanMakeReadRequest(uint32_t rx_id, uint8_t reg_id,
                               uint8_t interval_ms,
                               bamocar_can_frame_t *p_frame);

/* Send a torque command immediately through the selected CAN channel. */
bool bamocarCanSendTorqueCmd(uint8_t can_ch, int16_t torque_cmd);

#ifdef __cplusplus
}
#endif

#endif /* SRC_AP_APPS_BAMOCAR_CAN_H_ */
