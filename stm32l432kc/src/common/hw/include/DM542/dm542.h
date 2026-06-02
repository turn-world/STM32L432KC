/*
 * dm542.h
 *
 *  Created on: May 13, 2026
 *      Author: young
 */

#ifndef SRC_COMMON_HW_INCLUDE_DM542_DM542_H_
#define SRC_COMMON_HW_INCLUDE_DM542_DM542_H_

#include "hw_def.h"

#ifdef _USE_DM542

typedef struct
{
  uint32_t      pulse_per_rev;   // 예: 400, 800, 1600...
  float         lead_mm;         // 리드스크류 1회전당 이동거리 mm
  bool          dir_inv;
  bool          ena_inv;
} dm542_t;

bool dm542Init(void);                                                         // Initialize DM542 GPIO and CLI

void dm542Enable(dm542_t *p_driver);                                          // Enable motor driver output
void dm542Disable(dm542_t *p_driver);                                         // Disable motor driver output

void dm542SetDir(dm542_t *p_driver, bool dir);                                // Set motor rotation direction
void dm542Step(dm542_t *p_driver);                                            // Generate one step pulse

void dm542MoveStep(dm542_t *p_driver, int32_t step, uint32_t pulse_delay_us);  // Move by step count
void dm542MoveMm(dm542_t *p_driver, float mm, uint32_t pulse_delay_us);        // Move by distance in mm

#endif
#endif /* SRC_COMMON_HW_INCLUDE_DM542_DM542_H_ */
