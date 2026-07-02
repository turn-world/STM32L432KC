/*
 * ili9488.c
 *
 *  Created on: Jun 25, 2026
 *      Author: young
 */

#include "spi.h"
#include "gpio.h"
#include "lcd/ili9488.h"
#include "lcd/ili9488_reg.h"

#ifdef _USE_HW_ILI9488
#ifdef _USE_HW_SPI
#ifdef _USE_HW_GPIO

#define ILI9488_SPI_CH                       _DEF_SPI2
#define ILI9488_CS_CH                        _DEF_GPIO2
#define ILI9488_DC_CH                        _DEF_GPIO3
#define ILI9488_RST_CH                       _DEF_GPIO4

#define ILI9488_TX_CHUNK_PIXELS              32U
#define ILI9488_TX_CHUNK_BYTES               (ILI9488_TX_CHUNK_PIXELS * 3U)

#define MADCTL_MY                            ILI9488_MADCTL_MY
#define MADCTL_MX                            ILI9488_MADCTL_MX
#define MADCTL_MV                            ILI9488_MADCTL_MV
#define MADCTL_ML                            ILI9488_MADCTL_ML
#define MADCTL_RGB                           0x00U
#define MADCTL_BGR                           ILI9488_MADCTL_BGR
#define MADCTL_MH                            ILI9488_MADCTL_MH


static uint8_t spi_ch = ILI9488_SPI_CH;

static const int32_t _width  = HW_LCD_WIDTH;
static const int32_t _height = HW_LCD_HEIGHT;
static void (*frameCallBack)(void) = NULL;
volatile static bool is_write_frame = false;


static void writecommand(uint8_t c);
static void writedata(uint8_t d);
static void writedata_buf(const uint8_t *p_data, uint32_t length);
static void ili9488GpioInit(void);
static void ili9488InitRegs(void);
static void ili9488FillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
static void ili9488WriteColor(uint16_t color);
static void ili9488WriteColorRepeat(uint16_t color, uint32_t length);


static void TransferDoneISR(void)
{
  if (is_write_frame == true)
  {
    is_write_frame = false;
    gpioPinWrite(ILI9488_CS_CH, _DEF_HIGH);

    if (frameCallBack != NULL)
    {
      frameCallBack();
    }
  }
}

bool ili9488Init(void)
{
  return ili9488Reset();
}

bool ili9488InitDriver(lcd_driver_t *p_driver)
{
  if (p_driver == NULL)
  {
    return false;
  }

  p_driver->init        = ili9488Init;
  p_driver->reset       = ili9488Reset;
  p_driver->setWindow   = ili9488SetWindow;
  p_driver->getWidth    = ili9488GetWidth;
  p_driver->getHeight   = ili9488GetHeight;
  p_driver->setCallBack = ili9488SetCallBack;
  p_driver->sendBuffer  = ili9488SendBuffer;

  return true;
}

bool ili9488Reset(void)
{
  ili9488GpioInit();

  spiBegin(spi_ch);
  spiSetBitWidth(spi_ch, 8);
  spiSetDataMode(spi_ch, SPI_MODE0);
  spiAttachTxInterrupt(spi_ch, TransferDoneISR);

  gpioPinWrite(ILI9488_DC_CH,  _DEF_HIGH);
  gpioPinWrite(ILI9488_CS_CH,  _DEF_HIGH);
  gpioPinWrite(ILI9488_RST_CH, _DEF_HIGH);
  delay(5);

  gpioPinWrite(ILI9488_RST_CH, _DEF_LOW);
  delay(20);
  gpioPinWrite(ILI9488_RST_CH, _DEF_HIGH);
  delay(120);

  ili9488InitRegs();
  ili9488SetRotation(0);
  ili9488FillRect(0, 0, HW_LCD_WIDTH, HW_LCD_HEIGHT, black);

  return true;
}

uint16_t ili9488GetWidth(void)
{
  return HW_LCD_WIDTH;
}

uint16_t ili9488GetHeight(void)
{
  return HW_LCD_HEIGHT;
}

static void writecommand(uint8_t c)
{
  spiSetBitWidth(spi_ch, 8);

  gpioPinWrite(ILI9488_DC_CH, _DEF_LOW);
  gpioPinWrite(ILI9488_CS_CH, _DEF_LOW);

  spiTransfer8(spi_ch, c);

  gpioPinWrite(ILI9488_CS_CH, _DEF_HIGH);
}

static void writedata(uint8_t d)
{
  spiSetBitWidth(spi_ch, 8);

  gpioPinWrite(ILI9488_DC_CH, _DEF_HIGH);
  gpioPinWrite(ILI9488_CS_CH, _DEF_LOW);

  spiTransfer8(spi_ch, d);

  gpioPinWrite(ILI9488_CS_CH, _DEF_HIGH);
}

static void writedata_buf(const uint8_t *p_data, uint32_t length)
{
  if ((p_data == NULL) || (length == 0U))
  {
    return;
  }

  spiSetBitWidth(spi_ch, 8);

  gpioPinWrite(ILI9488_DC_CH, _DEF_HIGH);
  gpioPinWrite(ILI9488_CS_CH, _DEF_LOW);

  while (length > 0U)
  {
    spiTransfer8(spi_ch, *p_data);
    p_data++;
    length--;
  }

  gpioPinWrite(ILI9488_CS_CH, _DEF_HIGH);
}

static void ili9488GpioInit(void)
{
  gpioPinMode(ILI9488_CS_CH, _DEF_OUTPUT);
  gpioPinMode(ILI9488_DC_CH, _DEF_OUTPUT);
  gpioPinMode(ILI9488_RST_CH, _DEF_OUTPUT);
}

static void ili9488InitRegs(void)
{
  static const uint8_t pgamma[] =
  {
    0x00, 0x03, 0x09, 0x08, 0x16,
    0x0A, 0x3F, 0x78, 0x4C, 0x09,
    0x0A, 0x08, 0x16, 0x1A, 0x0F
  };
  static const uint8_t ngamma[] =
  {
    0x00, 0x16, 0x19, 0x03, 0x0F,
    0x05, 0x32, 0x45, 0x46, 0x04,
    0x0E, 0x0D, 0x35, 0x37, 0x0F
  };

  writecommand(ILI9488_SWRESET);
  delay(10);

  writecommand(ILI9488_SLPOUT);
  delay(120);

  writecommand(ILI9488_PGAMCTRL);
  writedata_buf(pgamma, sizeof(pgamma));

  writecommand(ILI9488_NGAMCTRL);
  writedata_buf(ngamma, sizeof(ngamma));

  writecommand(ILI9488_PWCTR1);
  writedata(0x17);
  writedata(0x15);

  writecommand(ILI9488_PWCTR2);
  writedata(0x41);

  writecommand(ILI9488_VMCTR1);
  writedata(0x00);
  writedata(0x12);
  writedata(0x80);

  writecommand(ILI9488_FRMCTR1);
  writedata(0xA0);

  writecommand(ILI9488_DFUNCTR);
  writedata(0x02);
  writedata(0x02);
  writedata(0x3B);

  writecommand(ILI9488_ENTMOD);
  writedata(0xC6);

  writecommand(ILI9488_ADJCTL3);
  writedata(0xA9);
  writedata(0x51);
  writedata(0x2C);
  writedata(0x82);

  writecommand(ILI9488_PIXFMT);
  writedata(ILI9488_PIXFMT_18BIT);

  writecommand(ILI9488_NORON);
  delay(10);

  writecommand(ILI9488_DISPON);
  delay(120);
}

void ili9488SetRotation(uint8_t mode)
{
  writecommand(ILI9488_MADCTL);

  switch (mode & 0x03U)
  {
    case 0:
      writedata(MADCTL_MX | MADCTL_BGR);
      break;

    case 1:
      writedata(MADCTL_MV | MADCTL_BGR);
      break;

    case 2:
      writedata(MADCTL_MY | MADCTL_BGR);
      break;

    default:
      writedata(MADCTL_MX | MADCTL_MY | MADCTL_MV | MADCTL_BGR);
      break;
  }
}

void ili9488SetWindow(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
  x0 = constrain(x0, 0, HW_LCD_WIDTH - 1);
  x1 = constrain(x1, 0, HW_LCD_WIDTH - 1);
  y0 = constrain(y0, 0, HW_LCD_HEIGHT - 1);
  y1 = constrain(y1, 0, HW_LCD_HEIGHT - 1);

  if (x0 > x1)
  {
    int32_t tmp = x0;
    x0 = x1;
    x1 = tmp;
  }

  if (y0 > y1)
  {
    int32_t tmp = y0;
    y0 = y1;
    y1 = tmp;
  }

  spiSetBitWidth(spi_ch, 8);

  writecommand(ILI9488_CASET);
  writedata((uint8_t)(x0 >> 8));
  writedata((uint8_t)x0);
  writedata((uint8_t)(x1 >> 8));
  writedata((uint8_t)x1);

  writecommand(ILI9488_PASET);
  writedata((uint8_t)(y0 >> 8));
  writedata((uint8_t)y0);
  writedata((uint8_t)(y1 >> 8));
  writedata((uint8_t)y1);

  writecommand(ILI9488_RAMWR);
}

static void ili9488FillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color)
{
  if ((x >= _width) || (y >= _height))
  {
    return;
  }

  if (x < 0)
  {
    w += x;
    x = 0;
  }

  if (y < 0)
  {
    h += y;
    y = 0;
  }

  if ((x + w) > _width)
  {
    w = _width - x;
  }

  if ((y + h) > _height)
  {
    h = _height - y;
  }

  if ((w < 1) || (h < 1))
  {
    return;
  }

  ili9488SetWindow(x, y, x + w - 1, y + h - 1);

  gpioPinWrite(ILI9488_DC_CH, _DEF_HIGH);
  gpioPinWrite(ILI9488_CS_CH, _DEF_LOW);

  ili9488WriteColorRepeat((uint16_t)color, (uint32_t)w * (uint32_t)h);

  gpioPinWrite(ILI9488_CS_CH, _DEF_HIGH);
}

static void ili9488WriteColor(uint16_t color)
{
  uint8_t r = (uint8_t)((color >> 11) & 0x1FU);
  uint8_t g = (uint8_t)((color >> 5) & 0x3FU);
  uint8_t b = (uint8_t)(color & 0x1FU);

  spiTransfer8(spi_ch, (uint8_t)((r << 3) | (r >> 2)));
  spiTransfer8(spi_ch, (uint8_t)((g << 2) | (g >> 4)));
  spiTransfer8(spi_ch, (uint8_t)((b << 3) | (b >> 2)));
}

static void ili9488WriteColorRepeat(uint16_t color, uint32_t length)
{
  uint8_t tx_buf[ILI9488_TX_CHUNK_BYTES];
  uint8_t r = (uint8_t)((color >> 11) & 0x1FU);
  uint8_t g = (uint8_t)((color >> 5) & 0x3FU);
  uint8_t b = (uint8_t)(color & 0x1FU);
  uint8_t r8 = (uint8_t)((r << 3) | (r >> 2));
  uint8_t g8 = (uint8_t)((g << 2) | (g >> 4));
  uint8_t b8 = (uint8_t)((b << 3) | (b >> 2));

  spiSetBitWidth(spi_ch, 8);

  while (length > 0U)
  {
    uint32_t chunk_pixels = length;
    uint32_t tx_len = 0;

    if (chunk_pixels > ILI9488_TX_CHUNK_PIXELS)
    {
      chunk_pixels = ILI9488_TX_CHUNK_PIXELS;
    }

    for (uint32_t i = 0; i < chunk_pixels; i++)
    {
      tx_buf[tx_len++] = r8;
      tx_buf[tx_len++] = g8;
      tx_buf[tx_len++] = b8;
    }

    for (uint32_t i = 0; i < tx_len; i++)
    {
      spiTransfer8(spi_ch, tx_buf[i]);
    }

    length -= chunk_pixels;
  }
}

bool ili9488SendBuffer(uint8_t *p_data, uint32_t length, uint32_t timeout_ms)
{
  const uint16_t *p_color = (const uint16_t *)p_data;

  (void)timeout_ms;

  if ((p_data == NULL) || (length == 0U))
  {
    return false;
  }

  is_write_frame = true;

  gpioPinWrite(ILI9488_DC_CH, _DEF_HIGH);
  gpioPinWrite(ILI9488_CS_CH, _DEF_LOW);

  spiSetBitWidth(spi_ch, 8);

  while (length > 0U)
  {
    ili9488WriteColor(*p_color);
    p_color++;
    length--;
  }

  TransferDoneISR();

  return true;
}

bool ili9488SetCallBack(void (*p_func)(void))
{
  frameCallBack = p_func;

  return true;
}

#endif
#endif
#endif
