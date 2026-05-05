/*
 * cdc.c
 *
 *  Created on: May 6, 2026
 *      Author: TEMP
 */


#ifdef _USE_HW_CDC


static bool is_init = false;

bool cdcInit(void)
{
  bool ret = true;

  is_init = true;

  return ret;
}

// 한번이라도 초기화 했는지 확인용
bool cdcIsInit(void)
{
	return is_init;
}
