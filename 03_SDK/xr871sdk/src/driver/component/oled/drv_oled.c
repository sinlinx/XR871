/*
 * Copyright (C) 2017 XRADIO TECHNOLOGY CO., LTD. All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *    1. Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *    2. Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the
 *       distribution.
 *    3. Neither the name of XRADIO TECHNOLOGY CO., LTD. nor the names of
 *       its contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "stdio.h"
#include "string.h"
#include "kernel/os/os_timer.h"
#include "kernel/os/os_time.h"
#include "kernel/os/os_mutex.h"

#include "oled_char_lib.h"
#include "driver/chip/hal_def.h"
#include "driver/component/oled/drv_oled.h"
#include "ssd1306.h"

#define OLED_DBG 0
#define LOG(flags, fmt, arg...)	\
	do {								\
		if (flags) 						\
			printf(fmt, ##arg);		\
	} while (0)

#define DRV_OLDE_DBG(fmt, arg...)	\
			//LOG(OLED_DBG, "[OLED] "fmt, ##arg)

#define OLED_MAX_TRANSFER_DATA_LEN 128

SSD1306_t oled_t;

OS_Mutex_t OLED_WR_LOCK;

void oled_wrcmd (uint8_t cmd)
{
	oled_t.SSD1306_Write(cmd, SSD1306_CMD);
}

Component_Status oled_wrdata(const uint8_t *data, uint32_t len)
{
	int i=0;
	if (len <= OLED_MAX_TRANSFER_DATA_LEN)
		for (i = 0; i < len; i++)
			oled_t.SSD1306_Write(*(data++), SSD1306_DATA);
	return COMP_OK;
}

Component_Status oled_setpos(unsigned column, unsigned page)
{
    oled_wrcmd(0xb0 + page);
    oled_wrcmd(((column&0xf0)>>4)|0x10);
    oled_wrcmd((column&0x0f)|0x00);
    return COMP_OK;
}

Component_Status oled_write(unsigned column, unsigned page , const uint8_t *data, uint32_t len)
{
	oled_setpos(column, page);
	oled_wrdata(data, len);
	return COMP_OK;
}
Component_Status DRV_Oled_Pnxm_Bmp(unsigned column, unsigned page, unsigned width, unsigned height, const uint8_t *bmp)
{
	OS_MutexLock(&OLED_WR_LOCK, 1000000);
    int pages = height / 8;
	if ((height % 8) > 0)
		pages += 1;
    if (pages > 8) {
		COMPONENT_WARN("oled show bmp error\n");
		return COMP_ERROR;
    }
    // draw 1st page
    page = 7 - page;
    if (pages >= 1) {
		oled_write(column, page, bmp, width);
    }
    // draw 2nd page
    if (pages >= 2) {
		oled_write(column, page - 1,&bmp[width], width);
    }
    // draw 3rd page
    if (pages >= 3) {
		oled_write(column, page - 2, &bmp[width * 2], width);
    }
    // draw 4th page
    if (pages >= 4) {
		oled_write(column, page - 3, &bmp[width * 3], width);
    }
	// draw 5th page
	if (pages >= 5) {
		oled_write(column, page - 4, &bmp[width *4], width);
	}
	// draw 6th page
	if (pages >= 6) {
		oled_write(column, page - 5, &bmp[width* 5], width);
	}
	// draw 7th page
	if (pages >= 7) {
		oled_write(column, page - 6, &bmp[width * 6], width);
	}
	// draw 8th page
	if (pages == 8) {
		oled_write(column, page - 7, &bmp[width * 7], width);
	}
	OS_MutexUnlock(&OLED_WR_LOCK);
    return COMP_OK;
}

void Oled_Reset_Io_Init()
{
	GPIO_InitParam io_Param;
	io_Param.driving = GPIO_DRIVING_LEVEL_3;
	io_Param.mode = GPIOx_Pn_F1_OUTPUT;
	io_Param.pull = GPIO_PULL_DOWN;
	HAL_GPIO_Init(oled_t.SSD1306_reset_Port, oled_t.SSD1306_reset_Pin, &io_Param);
}

void DRV_Oled_Power_Off()
{
	HAL_GPIO_WritePin(oled_t.SSD1306_reset_Port, oled_t.SSD1306_reset_Pin, GPIO_PIN_LOW);
}

void DRV_Oled_Power_On()
{
	HAL_GPIO_WritePin(oled_t.SSD1306_reset_Port, oled_t.SSD1306_reset_Pin, GPIO_PIN_HIGH);
}
void DRV_Oled_Reset()
{
	HAL_GPIO_WritePin(oled_t.SSD1306_reset_Port, oled_t.SSD1306_reset_Pin, GPIO_PIN_LOW);
	OS_MSleep(1);
	HAL_GPIO_WritePin(oled_t.SSD1306_reset_Port, oled_t.SSD1306_reset_Pin, GPIO_PIN_HIGH);
}

Component_Status  DRV_Oled_Showchar_1608(uint8_t x, uint8_t y, uint8_t chr)
{
	OS_MutexLock(&OLED_WR_LOCK, 1000000);
	const uint8_t *temp;
	chr -= ' ';
	if (x > 128 || y > 7) {
		COMPONENT_WARN("oled show char error\n");
		return COMP_ERROR;
	}

	y = 7 - y;
	temp = &ascii_1608[chr][0];
	oled_write(x, y, temp, 8);
	oled_write(x, y - 1, &temp[8], 8);
	OS_MutexUnlock(&OLED_WR_LOCK);
	return COMP_OK;
}

Component_Status DRV_Oled_Show_Str_1608(unsigned column, unsigned page, const char *str)
{
	if (column > 128 || page > 7) {
		COMPONENT_WARN("oled show str error\n");
		return COMP_ERROR;
	}
    const char *p = str;
    while (*p != '\0') {
		DRV_Oled_Showchar_1608(column, page, *(p++));
		column += 8;
		if (column > 128 || (128 - column) < 8) {
			page += 2;
			column = 0;
		}
	}
    return COMP_OK;
}

int DRV_Oled_P8xnstr(unsigned column, unsigned page, const uint8_t* str, unsigned len)
{
	OS_MutexLock(&OLED_WR_LOCK, 1000000);
	oled_write(column, page, str, len);
	OS_MutexUnlock(&OLED_WR_LOCK);
    return 0;
}

void DRV_Oled_OnOff(int onoff)
{
    if (onoff) {
        oled_wrcmd(0x8d);// set charge pump for OLED driver block
        oled_wrcmd(0x14);
        oled_wrcmd(0xaf);
    } else {
        oled_wrcmd(0x8d);// disable charge pump
        oled_wrcmd(0x10);
        oled_wrcmd(0xae);
    }
}

void DRV_Oled_Clear_Screen()
{
	DRV_OLDE_DBG("oled_clear_screen\n");
	uint8_t data[128] = {0};
	int i = 0;
	for (i = 0; i <= 7; i++)
    	DRV_Oled_P8xnstr(0, i, data, 128);
}

void DRV_Oled_Set_Brightness(uint8_t brightness)
{
	SSD1306_set_brightness(brightness);
}

Component_Status  DRV_Oled_Init(Oled_Config *cfg)
{
	if(OS_MutexCreate(&OLED_WR_LOCK) != OS_OK)
		COMPONENT_WARN("OS_MutexCreate error\n");

	oled_t.SSD1306_SPI_ID = cfg->Oled_SPI_ID;
	oled_t.SSD1306_SPI_MCLK = cfg->Oled_SPI_MCLK;
	oled_t.SSD1306_SPI_CS = cfg->Oled_SPI_CS;

	oled_t.SSD1306_dsPin = cfg->Oled_dsPin;
	oled_t.SSD1306_dsPort = cfg->Oled_dsPort;
	oled_t.SSD1306_reset_Port = cfg->Oled_reset_Port;
	oled_t.SSD1306_reset_Pin = cfg->Oled_reset_Pin;

	HAL_Status ret = SSD1306_SPI_Init(&oled_t);
	if (ret != HAL_OK)
		return COMP_ERROR;

	Oled_Reset_Io_Init();
	DRV_Oled_Reset();
	SSD1306_Init();
	DRV_Oled_Clear_Screen();

	COMPONENT_TRACK("end\n");
	return COMP_OK;
}

Component_Status DRV_Oled_DeInit()
{
	OS_MutexDelete(&OLED_WR_LOCK);
	if(SSD1306_SPI_DeInit() != HAL_OK) {
		COMPONENT_WARN("SSD1306 SPI Deinit error\n");
		return COMP_ERROR;
	}
	COMPONENT_TRACK("end\n");

	return COMP_OK;
}

