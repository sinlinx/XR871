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

#include "driver/chip/hal_codec.h"
#include "../hal_inc.h"
#include "sys/io.h"
#include "string.h"
#include "pm/pm.h"

typedef struct {
	/*I2C info*/
	uint8_t *name;
	uint8_t i2cId;
	uint8_t devAddr;
	uint8_t RegLength;
	uint8_t RegValLength;
	/*dir*/
	uint8_t playBack;
	uint8_t record;

	struct codec_ops *ops;
	struct codec_dai_ops *dai_ops;
	struct codec_ctl_ops *ctl_ops;
	hw_write write;
	hw_read read;
	uint32_t playDev;
	uint32_t recordDev;
	uint32_t sampleRate;
	HAL_BoardCfg boardCfg;
	DAI_FmtParam *fmtParam;
	CODEC_InitParam *initParam;
	SPK_Param spk_cfg;
	HAL_Mutex Lock;
} CODEC_Priv;

struct CODECS {
	char name[10];
	CODEC *dev;
};

static DAI_FmtParam gFmtParam = {
	DAIFMT_CBS_CFS,
	DAIFMT_I2S,
	DAIFMT_NB_NF,
	32, /*16,32,64,128,256*/
	16,
	24576000,
	24576000,
	MCLK1
};

#define SPEAKER_DOUBLE_USED	1
#define D_SPEAKER_VOL	0x10
#define S_SPEAKER_VOL	0x10
#define HEADPHONE_VOL	0x2b
#define MAINMIC_GAIN	0x3
#define HDSETMIC_GAIN	0x4
#define ADC_DIGITAL_GAIN	0xb0b0
#define AGC_USED	0
#define DRC_USED	0

static CODEC_InitParam gInitParam = {
	SPEAKER_DOUBLE_USED,
	D_SPEAKER_VOL,
	S_SPEAKER_VOL,
	HEADPHONE_VOL,
	MAINMIC_GAIN,
	HDSETMIC_GAIN,
};

volatile uint32_t g_init_volume_value = 14;
volatile uint32_t g_init_mute_status = 0;
static CODEC_Priv gCodecPriv;
extern CODEC AC101;
extern CODEC ES8323;

static const struct CODECS codecs[] = {
	{"AC101", &AC101},
//        {"ES8323", &ES8323},
	{"", NULL},
};

int32_t snd_soc_read(uint32_t reg)
{
	int16_t ret = 0;
	uint8_t val[5];
	CODEC_Priv *priv = &gCodecPriv;
	int8_t regValLength = 0;

	if (priv->RegValLength == CODEC_I2C_REGVAL_LENGTH16)
		regValLength = 2;
	else if (priv->RegValLength == CODEC_I2C_REGVAL_LENGTH8)
		regValLength = 1;
	else
		return -1;

	if (priv->RegLength != CODEC_I2C_REG_LENGTH8)
		return -1;


	ret = priv->read(priv->i2cId,priv->devAddr,reg,val,regValLength);
	if (ret != regValLength)
		return -1;

	uint16_t *reg_val = (uint16_t *)val;
	if (regValLength == CODEC_I2C_REGVAL_LENGTH16)
		return ((((*reg_val)&0xFF) << 8) |(((*reg_val)&0xFF00)>>8));
	else if(regValLength == CODEC_I2C_REGVAL_LENGTH8)
		return ((*reg_val)&0xFF);
	else
		return -1;

}

int32_t snd_soc_write(uint32_t reg, uint32_t reg_val)
{
	uint32_t ret;
	CODEC_Priv *priv = &gCodecPriv;
	uint8_t val[5];

	int8_t regValLength = 0;

	if (priv->RegLength != CODEC_I2C_REG_LENGTH8)
		return -1;

	if (priv->RegValLength == CODEC_I2C_REGVAL_LENGTH16) {
		regValLength = 2;
		val[0] = (reg_val & 0xFF00) >> 8;
		val[1] = reg_val & 0xFF;
	}else if (priv->RegValLength == CODEC_I2C_REGVAL_LENGTH8) {
		regValLength = 1;
		val[0] = reg_val & 0xFF;
	} else
		return -1;

	ret = priv->write(priv->i2cId,priv->devAddr, reg, val,regValLength);
	if (ret != regValLength)
		return -1;
	else
		return 0;
}

int32_t snd_soc_update_bits(uint32_t reg, uint32_t mask, uint32_t value)
{
	bool change;
	uint32_t old, new;
	int ret;
	ret = snd_soc_read(reg);
	if (ret < 0)
		return ret;
	old = ret;
	new = (old & ~mask) | (value & mask);
	change = old != new;
	if (change)
		ret = snd_soc_write(reg, new);
	if (ret < 0)
		return ret;

	return change;
}

//#define PA_ON_LOW

#ifdef PA_ON_LOW
#define PA_ON	GPIO_PIN_LOW
#define PA_OFF	GPIO_PIN_HIGH
#else
#define PA_ON	GPIO_PIN_HIGH
#define PA_OFF	GPIO_PIN_LOW
#endif

HAL_Status HAL_CODEC_Trigger(AUDIO_Device dev, uint32_t on)
{
	if (dev != AUDIO_DEVICE_HEADPHONE && dev != AUDIO_DEVICE_SPEAKER)
		return HAL_ERROR;
	CODEC_Priv *priv = &gCodecPriv;
	GPIO_PinMuxParam *pinmux = NULL;

	HAL_MutexLock(&priv->Lock, OS_WAIT_FOREVER);
	if (priv->spk_cfg.pinmux != NULL && dev == AUDIO_DEVICE_SPEAKER) {
		pinmux = priv->spk_cfg.pinmux;
		pinmux = pinmux;

		if (on) {
			HAL_GPIO_WritePin(pinmux->port, pinmux->pin, PA_ON);
		} else {
			HAL_GPIO_WritePin(pinmux->port, pinmux->pin, PA_OFF);
		}
	} else {//headphone set in codec file

		if (priv->ctl_ops->setTrigger)
			priv->ctl_ops->setTrigger(dev, on);
	}
	HAL_MutexUnlock(&priv->Lock);

	return HAL_OK;
}

HAL_Status HAL_CODEC_Mute(AUDIO_Device dev, uint32_t mute)
{
	if (dev > AUDIO_DEVICE_MAINMIC || dev < AUDIO_DEVICE_HEADPHONE)
		return HAL_ERROR;

	CODEC_Priv *priv = &gCodecPriv;
	GPIO_PinMuxParam *pinmux = NULL;

	HAL_MutexLock(&priv->Lock, OS_WAIT_FOREVER);
	pinmux = priv->spk_cfg.pinmux;

	if (dev == AUDIO_DEVICE_SPEAKER && priv->spk_cfg.pinmux != NULL) {
		if (mute == 1)
			HAL_GPIO_WritePin(pinmux->port, pinmux->pin, PA_OFF);
		else if (mute == 0)
			HAL_GPIO_WritePin(pinmux->port, pinmux->pin, PA_ON);
		else
			CODEC_ERROR("Codec mute err..\n");
	}

	if (dev == AUDIO_DEVICE_HEADPHONE) {

	}
	HAL_MutexUnlock(&priv->Lock);

	return HAL_OK;
}


HAL_Status HAL_CODEC_ROUTE_Set(AUDIO_Device dev)
{
	if (dev > AUDIO_DEVICE_MAINMIC || dev < AUDIO_DEVICE_HEADPHONE)
		return HAL_ERROR;

	CODEC_Priv *priv = &gCodecPriv;
	GPIO_PinMuxParam *pinmux = NULL;

	HAL_MutexLock(&priv->Lock, OS_WAIT_FOREVER);
	if (priv->spk_cfg.pinmux != NULL && dev == AUDIO_DEVICE_HEADPHONE) {

		pinmux = priv->spk_cfg.pinmux;
		HAL_GPIO_WritePin(pinmux->port, pinmux->pin, PA_OFF);
	}
	if (priv->ctl_ops->setRoute)
		priv->ctl_ops->setRoute(dev);

	if (priv->spk_cfg.pinmux != NULL && dev == AUDIO_DEVICE_SPEAKER) {

		pinmux = priv->spk_cfg.pinmux;
		HAL_GPIO_WritePin(pinmux->port, pinmux->pin, PA_ON);
	}
	HAL_MutexUnlock(&priv->Lock);

	return HAL_OK;
}

HAL_Status HAL_CODEC_VOLUME_LEVEL_Set(AUDIO_Device dev,int volume)
{
	CODEC_Priv *priv = &gCodecPriv;
	if (volume > VOLUME_MAX_LEVEL)
		return HAL_INVALID;

	HAL_MutexLock(&priv->Lock, OS_WAIT_FOREVER);
	if (priv->ctl_ops->setVolume)
		priv->ctl_ops->setVolume(dev, volume);
	HAL_MutexUnlock(&priv->Lock);

	return HAL_OK;
}

HAL_Status HAL_CODEC_INIT_VOLUME_Set(AUDIO_Device dev,int volume)
{
	CODEC_Priv *priv = &gCodecPriv;

	if (volume > VOLUME_MAX_LEVEL)
		return HAL_INVALID;

	HAL_MutexLock(&priv->Lock, OS_WAIT_FOREVER);
	g_init_volume_value = volume;
	HAL_MutexUnlock(&priv->Lock);

	return HAL_OK;
}

HAL_Status HAL_CODEC_MUTE_STATUS_Init(int status)
{
	CODEC_Priv *priv = &gCodecPriv;

	HAL_MutexLock(&priv->Lock, OS_WAIT_FOREVER);
	g_init_mute_status = status;
	HAL_MutexUnlock(&priv->Lock);

	return HAL_OK;
}

uint32_t HAL_CODEC_MUTE_STATUS_Get()
{
	return g_init_mute_status;
}

HAL_Status HAL_CODEC_Open(DATA_Param *param)
{
	if (!param)
		return HAL_INVALID;

	CODEC_DEBUG("CODEC Open..\n");

	CODEC_Priv *priv = &gCodecPriv;

	HAL_MutexLock(&priv->Lock, OS_WAIT_FOREVER);
	if (!param->fmtParam)
		priv->fmtParam = &gFmtParam;
	else
		priv->fmtParam = param->fmtParam;

	if (param->sampleRate)
		priv->sampleRate = param->sampleRate;
	else
		priv->sampleRate = 48000;

	switch (param->audioDev) {
		case AUDIO_DEVICE_HEADPHONE:
		case AUDIO_DEVICE_SPEAKER:
			priv->playBack = 1;
			break;
		case AUDIO_DEVICE_HEADPHONEMIC:
		case AUDIO_DEVICE_MAINMIC:
			priv->record = 1;
			break;
		default:
			HAL_MutexUnlock(&priv->Lock);
			return HAL_INVALID;
	}

	if (priv->dai_ops->startup)
		priv->dai_ops->startup(param->audioDev);
	if (priv->dai_ops->setPll)
		priv->dai_ops->setPll(priv->fmtParam);
	if (priv->dai_ops->setClkdiv)
		priv->dai_ops->setClkdiv(priv->fmtParam,priv->sampleRate);
	if (priv->dai_ops->setFormat)
		priv->dai_ops->setFormat(priv->fmtParam);
	if (priv->ctl_ops->setRoute)
		priv->ctl_ops->setRoute(param->audioDev);
	if (priv->ctl_ops->setVolume && (param->audioDev < AUDIO_DEVICE_HEADPHONEMIC))
		priv->ctl_ops->setVolume(param->audioDev, g_init_volume_value);
	HAL_MutexUnlock(&priv->Lock);

	return HAL_OK;
}

HAL_Status HAL_CODEC_Close(uint32_t dir)
{
	CODEC_Priv *priv = &gCodecPriv;

	CODEC_DEBUG("CODEC Close..\n");

	HAL_MutexLock(&priv->Lock, OS_WAIT_FOREVER);
	if (dir == 0) {
		priv->playBack = 0;
	} else {
		priv->record = 0;
	}
	if (priv->dai_ops->shutDown)
		priv->dai_ops->shutDown(priv->playBack, priv->record);
	HAL_MutexUnlock(&priv->Lock);

	return HAL_OK;
}

#ifdef CONFIG_PM
static int codec_suspend(struct soc_device *dev, enum suspend_state_t state)
{
	CODEC_Priv *priv = &gCodecPriv;

	switch (state) {
		case PM_MODE_SLEEP:
		case PM_MODE_STANDBY:
		case PM_MODE_HIBERNATION:
		case PM_MODE_POWEROFF:

			HAL_MutexLock(&priv->Lock, OS_WAIT_FOREVER);

			if (priv->ops->setInitParam)
				priv->ops->setInitParam(HAL_CODEC_DEINIT,NULL);
			if (priv->ops->jackDetect)
				priv->ops->jackDetect(HAL_CODEC_DEINIT,NULL);
			if (priv->ops->setSysClk)
				priv->ops->setSysClk(HAL_CODEC_DEINIT,NULL);
			if (priv->ops->setPower)
				priv->ops->setPower(HAL_CODEC_DEINIT,NULL);
			priv->boardCfg(0, HAL_BR_PINMUX_DEINIT, NULL);

			HAL_MutexUnlock(&priv->Lock);

			break;
		default:
			break;
	}

	return 0;
}

static int codec_resume(struct soc_device *dev, enum suspend_state_t state)
{
	CODEC_Priv *priv = &gCodecPriv;
	switch (state) {
		case PM_MODE_SLEEP:
		case PM_MODE_STANDBY:
		case PM_MODE_HIBERNATION:

			HAL_MutexLock(&priv->Lock, OS_WAIT_FOREVER);

			if (priv->ops->setPower)
				priv->ops->setPower(HAL_CODEC_INIT,NULL);
			if (priv->ops->setSysClk)
				priv->ops->setSysClk(HAL_CODEC_INIT,NULL);
			if (priv->ops->setInitParam)
				priv->ops->setInitParam(HAL_CODEC_INIT,(void *)priv->initParam);
			if (priv->ops->jackDetect)
				priv->ops->jackDetect(HAL_CODEC_INIT,NULL);
			priv->boardCfg(0, HAL_BR_PINMUX_INIT, NULL);

			HAL_MutexUnlock(&priv->Lock);
			break;

		default:
			break;
	}

	return 0;
}

static struct soc_device_driver codec_drv = {
	.name = "CODEC",
	.suspend = codec_suspend,
	.resume = codec_resume,
};

static struct soc_device codec_dev = {
	.name = "CODEC",
	.driver = &codec_drv,
};

#define CODEC_DEV (&codec_dev)
#else
#define CODEC_DEV NULL
#endif

HAL_Status HAL_CODEC_Init(CODEC_Param *param)
{
	if (!param)
		return HAL_INVALID;

	CODEC_DEBUG("CODEC INIT..\n");

	CODEC_Priv *priv = &gCodecPriv;
	memset(priv,0,sizeof(*priv));
	priv->read= param->read;
	priv->write = param->write;
	priv->i2cId = param->i2cId;

	if (!param->param)
		priv->initParam = &gInitParam;
	else
		priv->initParam = param->param;

	int i = 0;
	CODECP codec = NULL;
	for (i = 0; strlen(codecs[i].name); i++) {
		if (strncmp(param->name, codecs[i].name, strlen(codecs[i].name)) == 0) {
			codec = codecs[i].dev;
		}
	}

	if (!codec)
		return HAL_ERROR;

	HAL_MutexInit(&priv->Lock);
	priv->ops = codec->ops;
	priv->dai_ops = codec->dai_ops;
	priv->ctl_ops = codec->ctl_ops;
	priv->name = codec->name;
	priv->devAddr = codec->devAddr;
	priv->RegValLength = codec->RegValLength;
	priv->RegLength = codec->RegLength;

	HAL_MutexLock(&priv->Lock, OS_WAIT_FOREVER);
	if (priv->ops->setPower)
		priv->ops->setPower(HAL_CODEC_INIT,NULL);
	if (priv->ops->setSysClk)
		priv->ops->setSysClk(HAL_CODEC_INIT,NULL);

	if (priv->ops->setInitParam)
		priv->ops->setInitParam(HAL_CODEC_INIT,(void *)priv->initParam);

	if (priv->ops->jackDetect)
		priv->ops->jackDetect(HAL_CODEC_INIT,NULL);
#ifdef CONFIG_PM
	pm_register_ops(CODEC_DEV);
#endif
	HAL_MutexUnlock(&priv->Lock);

	if (param->boardCfg != NULL) {

		priv->boardCfg = param->boardCfg;
		/* config pinmux */
		priv->boardCfg(0, HAL_BR_PINMUX_INIT, NULL);
		priv->boardCfg(0, HAL_BR_PINMUX_GETINFO, &(priv->spk_cfg));
	}
	return HAL_OK;
}


HAL_Status HAL_CODEC_DeInit()
{
	CODEC_Priv *priv = &gCodecPriv;

	HAL_MutexLock(&priv->Lock, OS_WAIT_FOREVER);
	if (priv->ops->setPower)
		priv->ops->setPower(HAL_CODEC_DEINIT,NULL);

	if (priv->ops->setSysClk)
		priv->ops->setSysClk(HAL_CODEC_DEINIT,NULL);

	if (priv->ops->setInitParam)
		priv->ops->setInitParam(HAL_CODEC_DEINIT,NULL);

	if (priv->ops->jackDetect)
		priv->ops->jackDetect(HAL_CODEC_DEINIT,NULL);
	HAL_MutexUnlock(&priv->Lock);

	HAL_MutexDeinit(&priv->Lock);
	return HAL_OK;
}


