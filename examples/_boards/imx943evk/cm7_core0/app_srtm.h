/*
 * Copyright 2023 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _APP_SRTM_H_
#define _APP_SRTM_H_

#include "rpmsg_lite.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/
/* IRQ handler priority definition, bigger number stands for lower priority */

/* Task priority definition, bigger number stands for higher priority */
#define APP_SRTM_MONITOR_TASK_PRIO    (4U)
#define APP_SRTM_DISPATCHER_TASK_PRIO (3U)
#define APP_LPI2C_IRQ_PRIO            (5U)
#define APP_SAI_TX_DMA_IRQ_PRIO       (5U)
#define APP_SAI_RX_DMA_IRQ_PRIO       (5U)
#define APP_SAI_IRQ_PRIO              (5U)
#define APP_PDM_DMA_IRQ_PRIO          (5U)
#define APP_M2M_DMA_IRQ_PRIO          (5U)
#define APP_IRQSTEER_DMA_IRQ_PRIO     (5U)

/* Define the timeout ms to polling the A Core link up status */
#define APP_LINKUP_TIMER_PERIOD_MS (10U)

#define RPMSG_LITE_SRTM_SHMEM_BASE (VDEV0_VRING_BASE)
#define RPMSG_LITE_SRTM_LINK_ID    (RL_PLATFORM_IMX943_M7_A55_SRTM_LINK_ID)
#define RPMSG_LITE_MU (RPMSG_LITE_M70_A55_MU)
#define RPMSG_LITE_MU_IRQ (RPMSG_LITE_M70_A55_MU_IRQn)

#define APP_SRTM_AUDIO_CHANNEL_NAME "rpmsg-audio-channel"
#define APP_SRTM_PDM_CHANNEL_NAME   "rpmsg-micfil-channel"
#define APP_SRTM_I2C_CHANNEL_NAME   "rpmsg-i2c-channel"

#define PEER_CORE_ID (1U)

#define APP_SRTM_SAI           (SAI1)
#define APP_SRTM_SAI_IRQn      SAI1_IRQn

/* I2C service */
#define LPI2C1_BAUDRATE              (400000)
#define LPI2C2_BAUDRATE              (400000)

#define I2C_SWITCH_NONE 1

/* IRQSTEER, the EDMA1 interrupts are connected through IRQSTEER for Cortex-M7 NVIC. */
#define APP_SRTM_IRQSTEER_DMA  (IRQSTEERM7_CH3_IRQn)

/* SAI codec service */
#define APP_SAI_DMA            (EDMA1)
#define APP_SAI_TX_DMA_CHANNEL (24U)
#define APP_SAI_TX_DMA_MUX     (kDma1RequestMuxSai1Tx)
#define APP_SAI_RX_DMA_CHANNEL (25U)
#define APP_SAI_RX_DMA_MUX     (kDma1RequestMuxSai1Rx)
/* The frequency of the audio pll 1/2 are the fixed value. */
#define APP_AUDIO_PLL1_FREQ (393216000U)
#define APP_AUDIO_PLL2_FREQ (361267200U)
/* The MCLK of the SAI is 12288000Hz by default which can be changed when playback the music. */
#define APP_SAI_CLK_FREQ (12288000U)

/* PDM service */
#define APP_SRTM_PDM           (PDM)
#define APP_SRTM_PDM_DMA       (EDMA1)
#define APP_PDM_RX_DMA_CHANNEL (kDma1RequestMuxPdm)
#define APP_PDM_QUALITY_MODE        (kPDM_QualityModeHigh)
#define APP_PDM_CICOVERSAMPLE_RATE  (0U)
#define APP_PDM_CHANNEL_GAIN        (kPDM_DfOutputGain4)
#define APP_PDM_CHANNEL_CUTOFF_FREQ (kPDM_DcRemoverCutOff152Hz)

#define APP_DMA_IRQN(channel) (IRQn_Type)((uint32_t)EDMA1_CH0_IRQn + channel)

#ifndef SRTM_AUDIO_SERVICE_USED
#define SRTM_AUDIO_SERVICE_USED 0
#endif

typedef void (*app_rpmsg_monitor_t)(struct rpmsg_lite_instance *rpmsgHandle, bool ready, void *param);
typedef void (*app_irq_handler_t)(IRQn_Type irq, void *param);

extern int32_t RPMsg_MU11_B_IRQHandler(void);
/*******************************************************************************
 * API
 ******************************************************************************/

#if defined(__cplusplus)
extern "C" {
#endif

typedef enum
{
    APP_SRTM_StateRun = 0x0U,
    APP_SRTM_StateLinkedUp,
    APP_SRTM_StateReboot,
    APP_SRTM_StateShutdown,
} app_srtm_state_t;

/* Initialize SRTM contexts */
void APP_SRTM_Init(void);

/* Create RPMsg channel and start SRTM communication */
void APP_SRTM_StartCommunication(void);

/* Set RPMsg channel init/deinit monitor */
void APP_SRTM_SetRpmsgMonitor(app_rpmsg_monitor_t monitor, void *param);

#if defined(__cplusplus)
}
#endif

#endif /* _APP_SRTM_H_ */
