/*
 * ili9488_reg.h
 *
 *  Created on: Jun 25, 2026
 *      Author: young
 */

#ifndef SRC_COMMON_HW_INCLUDE_LCD_ILI9488_REG_H_
#define SRC_COMMON_HW_INCLUDE_LCD_ILI9488_REG_H_

#define ILI9488_NOP                         0x00U
#define ILI9488_SWRESET                     0x01U
#define ILI9488_RDDID                       0x04U
#define ILI9488_RDDST                       0x09U
#define ILI9488_SLPIN                       0x10U
#define ILI9488_SLPOUT                      0x11U
#define ILI9488_NORON                       0x13U
#define ILI9488_INVOFF                      0x20U
#define ILI9488_INVON                       0x21U
#define ILI9488_DISPOFF                     0x28U
#define ILI9488_DISPON                      0x29U
#define ILI9488_CASET                       0x2AU
#define ILI9488_PASET                       0x2BU
#define ILI9488_RAMWR                       0x2CU
#define ILI9488_RAMRD                       0x2EU
#define ILI9488_MADCTL                      0x36U
#define ILI9488_PIXFMT                      0x3AU
#define ILI9488_FRMCTR1                     0xB1U
#define ILI9488_INVCTR                      0xB4U
#define ILI9488_DFUNCTR                     0xB6U
#define ILI9488_ENTMOD                      0xB7U
#define ILI9488_PWCTR1                      0xC0U
#define ILI9488_PWCTR2                      0xC1U
#define ILI9488_VMCTR1                      0xC5U
#define ILI9488_PGAMCTRL                    0xE0U
#define ILI9488_NGAMCTRL                    0xE1U
#define ILI9488_ADJCTL3                     0xF7U

#define ILI9488_MADCTL_MY                   0x80U
#define ILI9488_MADCTL_MX                   0x40U
#define ILI9488_MADCTL_MV                   0x20U
#define ILI9488_MADCTL_ML                   0x10U
#define ILI9488_MADCTL_BGR                  0x08U
#define ILI9488_MADCTL_MH                   0x04U

#define ILI9488_PIXFMT_16BIT                0x55U
#define ILI9488_PIXFMT_18BIT                0x66U

#endif /* SRC_COMMON_HW_INCLUDE_LCD_ILI9488_REG_H_ */
