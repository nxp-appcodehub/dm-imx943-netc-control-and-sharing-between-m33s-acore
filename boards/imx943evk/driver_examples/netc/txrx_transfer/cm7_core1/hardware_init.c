/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*${header:start}*/
#include "sm_platform.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "board.h"
#include "app.h"
#include "hal_clock.h"
#include "hal_power.h"
/*${header:end}*/

/*${macro:start}*/
#define EXAMPLE_PORT_NUM (EXAMPLE_EP_NUM + EXAMPLE_SWT_MAX_PORT_NUM)
/*${macro:end}*/

/*${variable:start}*/
/* PHY operation. */
static netc_mdio_handle_t s_emdio_handle;
static phy_rtl8211f_resource_t s_phy_rtl8211f_resource;
static phy_dp8384x_resource_t s_phy_dp8384x_resource;
static uint8_t s_phy_addr[EXAMPLE_PORT_NUM] = EXAMPLE_PHY_ADDR;
static phy_handle_t s_phy_handle[EXAMPLE_PORT_NUM];
/*${variable:end}*/

/*${function:start}*/
void BOARD_InitHardware(void)
{
    hal_pwr_st_e st = hal_power_state_off;

    /* clang-format off */
    /* busNetcMixClk 133MHz */
    hal_clk_t hal_busmixClk = {
        .clk_id = hal_clock_busnetcmix,
        .pclk_id = hal_clock_syspll1dfs1div2, /* 400 MHz */
        .clk_round_opt = hal_clk_round_auto,
        .rate = 133333333UL,
    };
    /* enetClk 666 MHz */
    hal_clk_t hal_enetClk = {
        .clk_id = hal_clock_enet,
        .pclk_id = hal_clock_syspll1dfs2, /* 666 MHz */
        .clk_round_opt = hal_clk_round_auto,
        .rate = 666000000UL,
    };
    /* enetRefClk 250MHz */
    hal_clk_t hal_enetrefClk = {
        .clk_id = hal_clock_enetref,
        .pclk_id = hal_clock_syspll1dfs0, /* 1 GHz */
        .clk_round_opt = hal_clk_round_auto,
        .rate = 250000000UL,
    };
    /* enetTimer1Clk 100MHz */
    hal_clk_t hal_enettimer1Clk = {
        .clk_id = hal_clock_enettimer1,
        .pclk_id = hal_clock_syspll1dfs0div2, /* 500 MHz */
        .clk_round_opt = hal_clk_round_auto,
        .rate = 100000000UL,
    };

    /* mac0Clk(netc_switch_port0) 250MHz */
    hal_clk_t hal_mac0Clk = {
        .clk_id = hal_clock_mac0,
        .pclk_id = hal_clock_syspll1dfs0, /* syspll1 dfs0 = 1 GHz */
        .clk_round_opt = hal_clk_round_auto,
        .rate = 250000000UL,
    };

    /* mac1Clk(netc_switch_port1) 250MHz */
    hal_clk_t hal_mac1Clk = {
        .clk_id = hal_clock_mac1,
        .pclk_id = hal_clock_syspll1dfs0, /* syspll1 dfs0 = 1 GHz */
        .clk_round_opt = hal_clk_round_auto,
        .rate = 250000000UL,
    };

    /* mac2Clk(netc_switch_port2) 250MHz */
    hal_clk_t hal_mac2Clk = {
        .clk_id = hal_clock_mac2,
        .pclk_id = hal_clock_syspll1dfs0, /* syspll1 dfs0 = 1 GHz */
        .clk_round_opt = hal_clk_round_auto,
        .rate = 250000000UL,
    };

    /* mac3Clk(enetc0) 250MHz */
    hal_clk_t hal_mac3Clk = {
        .clk_id = hal_clock_mac3,
        .pclk_id = hal_clock_syspll1dfs0, /* syspll1 dfs0 = 1 GHz */
        .clk_round_opt = hal_clk_round_auto,
        .rate = 250000000UL,
    };

    /* mac4Clk(enetc1) 250MHz */
    hal_clk_t hal_mac4Clk = {
        .clk_id = hal_clock_mac4,
        .pclk_id = hal_clock_syspll1dfs0, /* syspll1 dfs0 = 1 GHz */
        .clk_round_opt = hal_clk_round_auto,
        .rate = 250000000UL,
    };

    /* mac5Clk(enetc2) 250MHz */
    hal_clk_t hal_mac5Clk = {
        .clk_id = hal_clock_mac5,
        .pclk_id = hal_clock_syspll1dfs0, /* syspll1 dfs0 = 1 GHz */
        .clk_round_opt = hal_clk_round_auto,
        .rate = 250000000UL,
    };

    hal_pwr_s_t pwrst = {
        .did = HAL_POWER_PLATFORM_MIX_SLICE_IDX_NETC,
        .st = hal_power_state_on,
    };
    /* clang-format on */

    SM_Platform_Init();

    BOARD_InitDebugConsolePins();
    BOARD_InitBootPins();
    BOARD_BootClockRUN();
    BOARD_InitDebugConsole();

    HAL_PowerSetState(&pwrst);
    st = HAL_PowerGetState(&pwrst);
    assert(st == hal_power_state_on);

    HAL_ClockSetParent(&hal_busmixClk);
    HAL_ClockSetRate(&hal_busmixClk);
    HAL_ClockEnable(&hal_busmixClk);

    HAL_ClockSetParent(&hal_enetClk);
    HAL_ClockSetRate(&hal_enetClk);
    HAL_ClockEnable(&hal_enetClk);

    HAL_ClockSetParent(&hal_enetrefClk);
    HAL_ClockSetRate(&hal_enetrefClk);
    HAL_ClockEnable(&hal_enetrefClk);

    HAL_ClockSetParent(&hal_enettimer1Clk);
    HAL_ClockSetRate(&hal_enettimer1Clk);
    HAL_ClockEnable(&hal_enettimer1Clk);

    HAL_ClockSetParent(&hal_mac0Clk);
    HAL_ClockSetRate(&hal_mac0Clk);
    HAL_ClockEnable(&hal_mac0Clk);

    HAL_ClockSetParent(&hal_mac1Clk);
    HAL_ClockSetRate(&hal_mac1Clk);
    HAL_ClockEnable(&hal_mac1Clk);

    HAL_ClockSetParent(&hal_mac2Clk);
    HAL_ClockSetRate(&hal_mac2Clk);
    HAL_ClockEnable(&hal_mac2Clk);

    HAL_ClockSetParent(&hal_mac3Clk);
    HAL_ClockSetRate(&hal_mac3Clk);
    HAL_ClockEnable(&hal_mac3Clk);

    HAL_ClockSetParent(&hal_mac4Clk);
    HAL_ClockSetRate(&hal_mac4Clk);
    HAL_ClockEnable(&hal_mac4Clk);

    HAL_ClockSetParent(&hal_mac5Clk);
    HAL_ClockSetRate(&hal_mac5Clk);
    HAL_ClockEnable(&hal_mac5Clk);

    /* Select ETH signals to use */
    BOARD_EXPANDER_SetPinAsOutput(BOARD_PCA6416_I2C6_S3_ID, ETH2_SEL);
    BOARD_EXPANDER_SetPinAsOutput(BOARD_PCA6416_I2C6_S3_ID, ETH3_SEL);
    BOARD_EXPANDER_SetPinAsOutput(BOARD_PCA6416_I2C6_S3_ID, ETH4_SEL);

    BOARD_EXPANDER_SetPinToHigh(BOARD_PCA6416_I2C6_S3_ID, ETH2_SEL);
    BOARD_EXPANDER_SetPinToHigh(BOARD_PCA6416_I2C6_S3_ID, ETH3_SEL);
    BOARD_EXPANDER_SetPinToHigh(BOARD_PCA6416_I2C6_S3_ID, ETH4_SEL);

    /* PHY reset */
    BOARD_EXPANDER_SetPinAsOutput(BOARD_PCA6416_I2C3_S5_21_ID, ETH2_RST_B);
    BOARD_EXPANDER_SetPinAsOutput(BOARD_PCA6416_I2C3_S5_21_ID, ETH3_RST_B);
    BOARD_EXPANDER_SetPinAsOutput(BOARD_PCA6416_I2C3_S5_21_ID, ETH4_RST_B);

    BOARD_EXPANDER_SetPinToLow(BOARD_PCA6416_I2C3_S5_21_ID, ETH2_RST_B);
    SDK_DelayAtLeastUs(20000, SDK_DEVICE_MAXIMUM_CPU_CLOCK_FREQUENCY);
    BOARD_EXPANDER_SetPinToHigh(BOARD_PCA6416_I2C3_S5_21_ID, ETH2_RST_B);
    SDK_DelayAtLeastUs(100000, SDK_DEVICE_MAXIMUM_CPU_CLOCK_FREQUENCY);

    BOARD_EXPANDER_SetPinToLow(BOARD_PCA6416_I2C3_S5_21_ID, ETH3_RST_B);
    SDK_DelayAtLeastUs(20000, SDK_DEVICE_MAXIMUM_CPU_CLOCK_FREQUENCY);
    BOARD_EXPANDER_SetPinToHigh(BOARD_PCA6416_I2C3_S5_21_ID, ETH3_RST_B);
    SDK_DelayAtLeastUs(100000, SDK_DEVICE_MAXIMUM_CPU_CLOCK_FREQUENCY);

    BOARD_EXPANDER_SetPinToLow(BOARD_PCA6416_I2C3_S5_21_ID, ETH4_RST_B);
    SDK_DelayAtLeastUs(20000, SDK_DEVICE_MAXIMUM_CPU_CLOCK_FREQUENCY);
    BOARD_EXPANDER_SetPinToHigh(BOARD_PCA6416_I2C3_S5_21_ID, ETH4_RST_B);
    SDK_DelayAtLeastUs(100000, SDK_DEVICE_MAXIMUM_CPU_CLOCK_FREQUENCY);

    /*
     * PCS(Physical Coding Sublayer) protocols on link0-5,
     * xxxx xxxx xxxx xxx1: 1G SGMII
     * xxxx xxxx xxxx xx1x: OC-SGMII(i.e.: OverClock 2.5 G SGMII)
     */
    BLK_CTRL_NETCMIX->CFG_LINK_PCS_PROT_0 |= BLK_CTRL_NETCMIX_CFG_LINK_PCS_PROT_0_CFG_LINK_PCS_PROT_0(2U); /* OverClock 2.5 G SGMII */
    BLK_CTRL_NETCMIX->CFG_LINK_PCS_PROT_1 |= BLK_CTRL_NETCMIX_CFG_LINK_PCS_PROT_1_CFG_LINK_PCS_PROT_1(2U); /* OverClock 2.5 G SGMII */
    BLK_CTRL_NETCMIX->CFG_LINK_PCS_PROT_2 |= BLK_CTRL_NETCMIX_CFG_LINK_PCS_PROT_2_CFG_LINK_PCS_PROT_2(1U); /* 1G SGMII */
    BLK_CTRL_NETCMIX->CFG_LINK_PCS_PROT_3 |= BLK_CTRL_NETCMIX_CFG_LINK_PCS_PROT_3_CFG_LINK_PCS_PROT_3(1U); /* 1G SGMII */
    BLK_CTRL_NETCMIX->CFG_LINK_PCS_PROT_4 |= BLK_CTRL_NETCMIX_CFG_LINK_PCS_PROT_4_CFG_LINK_PCS_PROT_4(1U); /* 1G SGMII */
    BLK_CTRL_NETCMIX->CFG_LINK_PCS_PROT_5 |= BLK_CTRL_NETCMIX_CFG_LINK_PCS_PROT_5_CFG_LINK_PCS_PROT_5(1U); /* 1G SGMII */

    /*
     * MII protocol for port0~5
     * 0b0000..MII
     * 0b0001..RMII
     * 0b0010..RGMII
     * 0b0011..SGMII
     * 0b0100~0b1111..Reserved
     */
    BLK_CTRL_NETCMIX->NETC_LINK_CFG0 |= BLK_CTRL_NETCMIX_NETC_LINK_CFG0_MII_PROT(0x0U); /* MII */
    BLK_CTRL_NETCMIX->NETC_LINK_CFG1 |= BLK_CTRL_NETCMIX_NETC_LINK_CFG1_MII_PROT(0x0U); /* MII */
    BLK_CTRL_NETCMIX->NETC_LINK_CFG2 |= BLK_CTRL_NETCMIX_NETC_LINK_CFG2_MII_PROT(0x2U); /* RGMII */
    BLK_CTRL_NETCMIX->NETC_LINK_CFG3 |= BLK_CTRL_NETCMIX_NETC_LINK_CFG3_MII_PROT(0x2U); /* RGMII */
    BLK_CTRL_NETCMIX->NETC_LINK_CFG4 |= BLK_CTRL_NETCMIX_NETC_LINK_CFG4_MII_PROT(0x2U); /* RGMII */
    BLK_CTRL_NETCMIX->NETC_LINK_CFG5 |= BLK_CTRL_NETCMIX_NETC_LINK_CFG5_MII_PROT(0x2U); /* RGMII */

    /*
     * ETH2 selection: MAC2(switch port2) or MAC3(enetc1)
     * 0b - MAC2 selected
     * 1b - MAC3 selected
     */
    BLK_CTRL_NETCMIX->EXT_PIN_CONTROL &= ~BLK_CTRL_NETCMIX_EXT_PIN_CONTROL_mac2_mac3_sel(1U);

    /* Unlock the IERB. It will warm reset whole NETC. */
    NETC_PRIV->NETCRR &= ~NETC_PRIV_NETCRR_LOCK_MASK;
    while ((NETC_PRIV->NETCRR & NETC_PRIV_NETCRR_LOCK_MASK) != 0U)
    {
    }

    /* Lock the IERB. */
    NETC_PRIV->NETCRR |= NETC_PRIV_NETCRR_LOCK_MASK;
    while ((NETC_PRIV->NETCSR & NETC_PRIV_NETCSR_STATE_MASK) != 0U)
    {
    }
}

status_t APP_MDIO_Init(void)
{
    status_t result = kStatus_Success;

    netc_mdio_config_t mdioConfig = {
        .isPreambleDisable = false,
        .isNegativeDriven  = false,
        .srcClockHz        = HAL_ClockGetRate(hal_clock_enet),
    };

    mdioConfig.mdio.type = kNETC_EMdio;
    result               = NETC_MDIOInit(&s_emdio_handle, &mdioConfig);

    return result;
}

static status_t APP_EMDIOWrite(uint8_t phyAddr, uint8_t regAddr, uint16_t data)
{
    return NETC_MDIOWrite(&s_emdio_handle, phyAddr, regAddr, data);
}

static status_t APP_EMDIORead(uint8_t phyAddr, uint8_t regAddr, uint16_t *pData)
{
    return NETC_MDIORead(&s_emdio_handle, phyAddr, regAddr, pData);
}

status_t APP_PHY_Init(void)
{
    status_t result            = kStatus_Success;

    /* phyrtl8211f */
    phy_config_t phy8211Config = {
        .autoNeg   = true,
        .speed     = kPHY_Speed1000M,
        .duplex    = kPHY_FullDuplex,
        .enableEEE = false,
        .ops       = &phyrtl8211f_ops,
    };

    s_phy_rtl8211f_resource.write = APP_EMDIOWrite;
    s_phy_rtl8211f_resource.read  = APP_EMDIORead;
    phy8211Config.resource = &s_phy_rtl8211f_resource;

    /* phydp8384x */
    phy_config_t phy8384xConfig = {
        .autoNeg   = true,
        .speed     = kPHY_Speed100M,
        .duplex    = kPHY_FullDuplex,
        .enableEEE = false,
        .ops       = &phydp8384x_ops,
    };

    s_phy_dp8384x_resource.write = APP_EMDIOWrite;
    s_phy_dp8384x_resource.read  = APP_EMDIORead;
    phy8384xConfig.resource = &s_phy_dp8384x_resource.write;

    for (int i = 0; i < EXAMPLE_PORT_NUM; i++) {
	phy_config_t *phyConfig;

        switch (i)
        {
            case EXAMPLE_EP0_PORT:
            case EXAMPLE_EP1_PORT:
            case EXAMPLE_SWT_PORT2:
                phyConfig = &phy8211Config;
                break;
            case EXAMPLE_SWT_PORT0:
            case EXAMPLE_SWT_PORT1:
                phyConfig = &phy8384xConfig;
                break;
            default:
                assert(false);
                break;
        }

        phyConfig->phyAddr = s_phy_addr[i];
        result = PHY_Init(&s_phy_handle[i], phyConfig);
        if (result != kStatus_Success)
        {
            return result;
        }
        result = PHY_EnableLoopback(&s_phy_handle[i], kPHY_LocalLoop, phyConfig->speed, true);
        if (result != kStatus_Success)
        {
            return result;
        }
    }

    return result;
}

status_t APP_PHY_GetLinkStatus(uint32_t port, bool *link)
{
    return PHY_GetLinkStatus(&s_phy_handle[port], link);
}

status_t APP_PHY_GetLinkModeSpeedDuplex(uint32_t port, netc_hw_mii_mode_t *mode, netc_hw_mii_speed_t *speed, netc_hw_mii_duplex_t *duplex)
{
    switch (port)
    {
        case EXAMPLE_EP0_PORT:
        case EXAMPLE_EP1_PORT:
        case EXAMPLE_SWT_PORT2:
            *mode = kNETC_RgmiiMode;
            break;
        case EXAMPLE_SWT_PORT0:
        case EXAMPLE_SWT_PORT1:
            *mode = kNETC_MiiMode;
            break;
        default:
            assert(false);
            break;
    }

    return PHY_GetLinkSpeedDuplex(&s_phy_handle[port], (phy_speed_t *)speed, (phy_duplex_t *)duplex);
}
/*${function:end}*/
