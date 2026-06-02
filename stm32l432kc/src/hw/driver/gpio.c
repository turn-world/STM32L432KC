/*
 * gpio.c
 *
 *  Created on: Aug 7, 2025
 *      Author: young
 */


#include "gpio.h"
#include "cli.h"

typedef struct
{
	GPIO_TypeDef  *port;
	uint32_t		pin;
	uint8_t			mode;
	GPIO_PinState	on_state;
	GPIO_PinState	off_state;
	bool			init_value;

} button_tbl_t;

button_tbl_t gpio_tbl[GPIO_MAX_CH] =
{
    {GPIOA, GPIO_PIN_3, _DEF_INPUT_PULLUP, GPIO_PIN_RESET, GPIO_PIN_SET  , true  }, // PUL
    {GPIOA, GPIO_PIN_4, _DEF_INPUT_PULLUP, GPIO_PIN_RESET, GPIO_PIN_SET  , true  }, // DIR
    {GPIOA, GPIO_PIN_5, _DEF_INPUT_PULLUP, GPIO_PIN_RESET, GPIO_PIN_SET  , true  }, // ENL
    {GPIOA, GPIO_PIN_9, _DEF_OUTPUT_OPEN_DRAIN, GPIO_PIN_RESET, GPIO_PIN_SET  , true  }, // test
};

#ifdef _USE_HW_CLI
static void cliGpio(cli_args_t *args);
#endif

bool gpioInit(void)
{
	bool ret = true;



	for(int i = 0; i < GPIO_MAX_CH; i++)
	{
		gpioPinMode(i,gpio_tbl[i].mode);
		gpioPinWrite(i,gpio_tbl[i].init_value);
	}
#ifdef _USE_HW_CLI
	cliAdd("gpio",cliGpio);
#endif
	return ret;
}

bool gpioPinMode(uint8_t ch, uint8_t mode)
{
	bool ret = true;
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	if(ch >= GPIO_MAX_CH)
	{
		return false;
	}

	switch(mode)
	{
	case _DEF_INPUT:
		GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
		GPIO_InitStruct.Pull = GPIO_NOPULL;
		break;
	case _DEF_INPUT_PULLUP:
		GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
		GPIO_InitStruct.Pull = GPIO_PULLUP;
		break;
	case _DEF_INPUT_PULLDOWN:
		GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
		GPIO_InitStruct.Pull = GPIO_PULLDOWN;
		break;
	case _DEF_OUTPUT:
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
		GPIO_InitStruct.Pull = GPIO_NOPULL;
		break;
	case _DEF_OUTPUT_PULLUP:
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
		GPIO_InitStruct.Pull = GPIO_PULLUP;
		break;
	case _DEF_OUTPUT_PULLDOWN:
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
		GPIO_InitStruct.Pull = GPIO_PULLDOWN;
		break;
	case _DEF_OUTPUT_OPEN_DRAIN:
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
		GPIO_InitStruct.Pull = GPIO_NOPULL;
		break;
	default:
		return false;

	}

	GPIO_InitStruct.Pin = gpio_tbl[ch].pin;
	HAL_GPIO_Init(gpio_tbl[ch].port, &GPIO_InitStruct);

	return ret;
}

void gpioPinWrite(uint8_t ch, bool value)
{
	if(ch >= GPIO_MAX_CH)
	{
		return;
	}

	if(value == true)
	{
		HAL_GPIO_WritePin(gpio_tbl[ch].port, gpio_tbl[ch].pin, gpio_tbl[ch].on_state);
	}else
	{
		HAL_GPIO_WritePin(gpio_tbl[ch].port, gpio_tbl[ch].pin, gpio_tbl[ch].off_state);
	}
}

bool gpioPinRead(uint8_t ch)
{
	bool ret = false;
	if(ch >= GPIO_MAX_CH)
	{
		return false;
	}

	if(HAL_GPIO_ReadPin(gpio_tbl[ch].port, gpio_tbl[ch].pin) == gpio_tbl[ch].on_state)
	{
		ret = true;
	}
	return ret;
}
void gpioPinToggle(uint8_t ch)
{
	if(ch >= GPIO_MAX_CH)
	{
		return;
	}

	HAL_GPIO_TogglePin(gpio_tbl[ch].port, gpio_tbl[ch].pin);
}

void gpioPinToggleCount(uint8_t ch, uint32_t count, uint32_t delay_ms)
{
	if(ch >= GPIO_MAX_CH)
	{
		return;
	}

	for(uint32_t i = 0; i < count; i++)
	{
		gpioPinToggle(ch);

		if(delay_ms > 0)
		{
			delay(delay_ms);
		}
	}
}

void gpioPinPulseCount(uint8_t ch, uint32_t count, uint32_t delay_ms)
{
	if(ch >= GPIO_MAX_CH)
	{
		return;
	}

	for(uint32_t i = 0; i < count; i++)
	{
		gpioPinWrite(ch, true);

		if(delay_ms > 0)
		{
			delay(delay_ms);
		}

		gpioPinWrite(ch, false);

		if(delay_ms > 0)
		{
			delay(delay_ms);
		}
	}
}

#ifdef _USE_HW_CLI
static void cliGpio(cli_args_t *args)
{
	bool ret = false;

	if(args->argc == 1 && args->isStr(0,"show") == true)
	{
		while(cliKeepLoop())
		{
			for(int i = 0; i < GPIO_MAX_CH; i++)
			{
				cliPrintf("%d", gpioPinRead(i));
			}
			cliPrintf("\n");
			delay(100);
		}

		ret = true;
	}

	if(args->argc == 2 && args->isStr(0,"read") == true)
	{
		uint8_t ch;

		ch = (uint8_t)args->getData(1);
		while(cliKeepLoop())
		{
			cliPrintf("gpio read %d : %d",ch, gpioPinRead(ch));
		}

		ret = true;
	}

	if(args->argc == 3 && args->isStr(0,"write") == true)
	{
		uint8_t ch;
		uint8_t value;


		ch 		= (uint8_t)args->getData(1);
		value 	= (uint8_t)args->getData(2);

		gpioPinWrite(ch,value);
		cliPrintf("gpio write %d : %d\n", ch, value);

		ret = true;
	}

	 if(args->argc == 3 && args->isStr(0, "mode") == true)
	  {
	    uint8_t ch;
	    uint8_t mode;
	    bool result = false;

	    ch = (uint8_t)args->getData(1);

	    if(args->isStr(2, "input") == true)
	    {
	      mode = _DEF_INPUT;
	      result = gpioPinMode(ch, mode);
	    }
	    else if(args->isStr(2, "pullup") == true)
	    {
	      mode = _DEF_INPUT_PULLUP;
	      result = gpioPinMode(ch, mode);
	    }
	    else if(args->isStr(2, "pulldown") == true)
	    {
	      mode = _DEF_INPUT_PULLDOWN;
	      result = gpioPinMode(ch, mode);
	    }
	    else if(args->isStr(2, "output") == true)
	    {
	      mode = _DEF_OUTPUT;
	      result = gpioPinMode(ch, mode);
	    }
	    else if(args->isStr(2, "od") == true)
	    {
	      mode = _DEF_OUTPUT_OPEN_DRAIN;
	      result = gpioPinMode(ch, mode);
	    }

	    if(result == true)
	    {
	      cliPrintf("gpio mode %d ok\n", ch);
	    }
	    else
	    {
	      cliPrintf("gpio mode %d fail\n", ch);
	    }

	    ret = true;
	  }

	if(args->argc >= 2 && args->argc <= 4 && args->isStr(0, "toggle") == true)
	{
		uint8_t ch;
		uint32_t count = 100;
		uint32_t delay_ms = 1;

		ch = (uint8_t)args->getData(1);

		if(args->argc >= 3)
		{
			count = (uint32_t)args->getData(2);
		}

		if(args->argc >= 4)
		{
			delay_ms = (uint32_t)args->getData(3);
		}

		if(ch < GPIO_MAX_CH)
		{
			gpioPinToggleCount(ch, count, delay_ms);
			cliPrintf("gpio toggle %d count %d delay %dms\n", ch, (int)count, (int)delay_ms);
		}
		else
		{
			cliPrintf("gpio toggle %d fail\n", ch);
		}

		ret = true;
	}

	if(args->argc >= 2 && args->argc <= 4 && args->isStr(0, "pulse") == true)
	{
		uint8_t ch;
		uint32_t count = 100;
		uint32_t delay_ms = 1;

		ch = (uint8_t)args->getData(1);

		if(args->argc >= 3)
		{
			count = (uint32_t)args->getData(2);
		}

		if(args->argc >= 4)
		{
			delay_ms = (uint32_t)args->getData(3);
		}

		if(ch < GPIO_MAX_CH)
		{
			gpioPinPulseCount(ch, count, delay_ms);
			cliPrintf("gpio pulse %d count %d delay %dms\n", ch, (int)count, (int)delay_ms);
		}
		else
		{
			cliPrintf("gpio pulse %d fail\n", ch);
		}

		ret = true;
	}

	if(ret != true)
	{
		cliPrintf("gpio show\n");
		cliPrintf("gpio read ch[0~%d]\n", GPIO_MAX_CH-1);
		cliPrintf("gpio write ch[0~%d] 0:1\n", GPIO_MAX_CH-1);
		cliPrintf("gpio mode ch[0~%d] input/pullup/pulldown/output/od\n", GPIO_MAX_CH-1);
		cliPrintf("gpio toggle ch[0~%d] count delay_ms\n", GPIO_MAX_CH-1);
		cliPrintf("gpio pulse ch[0~%d] count delay_ms\n", GPIO_MAX_CH-1);
	}
}
#endif
