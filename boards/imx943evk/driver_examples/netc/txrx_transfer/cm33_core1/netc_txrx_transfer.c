/*
 * Copyright 2021-2023,2025 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "FreeRTOS.h"
#include "task.h"
//#include "fsl_shell.h"
//#include "shell.h"
#include "fsl_debug_console.h"
#include "fsl_netc_timer.h"
#include "fsl_clock.h"
#include  "fsl_ccm.h"
#include "app.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "rsc_table.h"
#include "app_srtm.h"
#include "fsl_netc_msg.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define EXAMPLE_EP_BD_ALIGN       128U
#define EXAMPLE_TX_INTR_MSG_DATA  1U
#define EXAMPLE_RX_INTR_MSG_DATA  2U
#define SI_COM_INTR_MSG_DATA      3U
#define EXAMPLE_TX_MSIX_ENTRY_IDX 0U
#define EXAMPLE_RX_MSIX_ENTRY_IDX 1U
#define SI_COM_MSIX_ENTRY_IDX     2U
#define EXAMPLE_FRAME_FID         1U
#define NETC_VSI_NUM_USED       (1U)
#ifndef PHY_STABILITY_DELAY_US
#define PHY_STABILITY_DELAY_US (500000U)
#endif 
#define ENISI 1
#define ENVLANFWD 1
#define ENIPF 1
#define ENPOLICER 1
#define VLAN_FILTER_ID  (1U)
#define ENSGCL 1
#define EN_1588_TIMER 1
#define ENVLANQOSMAP 1
#define ENQBV 1
#define ENBP 0
#if ENVLANFWD
uint32_t vfEntryID  = 0U;
uint32_t fdbEntryID = 0U;
netc_tb_vf_config_t vfEntryCfg;
netc_tb_fdb_config_t fdbEntryCfg;
#endif

#define NETC_MSGINTR_IRQ MSGINTR1_IRQn
#define NETC_MSGINTR_PRIORITY 6U

#define SI_MSG_THREAD_STACKSIZE 2000
#define SI_MSG_THREAD_PRIO 3

uint32_t rpEID = 1;
#if ENPOLICER
netc_tb_rp_config_t policerEntry;
#endif

#if !(defined(FSL_FEATURE_NETC_HAS_NO_SWITCH) && FSL_FEATURE_NETC_HAS_NO_SWITCH)
/* ENETC pseudo port for management */
#ifndef EXAMPLE_SWT_SI
#define EXAMPLE_SWT_SI kNETC_ENETC1PSI0
#endif
/* Switch pseudo port */
#ifndef EXAMPLE_SWT_PSEUDO_PORT
#define EXAMPLE_SWT_PSEUDO_PORT 0x4U
#endif
#endif
/*******************************************************************************
 * Prototypes
 ******************************************************************************/
/* Rx buffer memeory type. */
typedef uint8_t rx_buffer_t[EXAMPLE_EP_RXBUFF_SIZE_ALIGN];

/*******************************************************************************
 * Variables
 ******************************************************************************/
/* EP resource. */
static ep_handle_t g_ep_handle;
static netc_hw_si_idx_t g_siIndex[EXAMPLE_EP_NUM] = EXAMPLE_EP_SI;

#if !(defined(FSL_FEATURE_NETC_HAS_NO_SWITCH) && FSL_FEATURE_NETC_HAS_NO_SWITCH)
/* SWT resource. */
static swt_handle_t g_swt_handle;
static swt_config_t g_swt_config;
static swt_transfer_config_t swtTxRxConfig;
#endif


#if (NETC_VSI_NUM_USED > 0)
AT_NONCACHEABLE_SECTION_ALIGN(static uint8_t psiMsgBuff1[1024], 32);
#endif

/* Buffer descriptor resource. */
AT_NONCACHEABLE_SECTION_ALIGN(static netc_rx_bd_t g_rxBuffDescrip[EXAMPLE_EP_RXBD_NUM],
                              EXAMPLE_EP_BD_ALIGN);
AT_NONCACHEABLE_SECTION_ALIGN(static rx_buffer_t g_rxDataBuff[EXAMPLE_EP_RXBD_NUM],
                              EXAMPLE_EP_BUFF_SIZE_ALIGN);
AT_NONCACHEABLE_SECTION_ALIGN(static uint8_t g_txFrame[EXAMPLE_EP_TEST_FRAME_SIZE], EXAMPLE_EP_BUFF_SIZE_ALIGN);
#if !(defined(FSL_FEATURE_NETC_HAS_NO_SWITCH) && FSL_FEATURE_NETC_HAS_NO_SWITCH)
AT_NONCACHEABLE_SECTION_ALIGN(static netc_tx_bd_t g_mgmtTxBuffDescrip[EXAMPLE_EP_TXBD_NUM], EXAMPLE_EP_BD_ALIGN);
AT_NONCACHEABLE_SECTION_ALIGN(static netc_cmd_bd_t g_cmdBuffDescrip[EXAMPLE_EP_TXBD_NUM], EXAMPLE_EP_BD_ALIGN);
#endif
AT_NONCACHEABLE_SECTION(static uint8_t g_rxFrame[EXAMPLE_EP_RXBUFF_SIZE_ALIGN]);
static uint64_t rxBuffAddrArray[EXAMPLE_EP_RXBD_NUM];
#if !(defined(FSL_FEATURE_NETC_HAS_NO_SWITCH) && FSL_FEATURE_NETC_HAS_NO_SWITCH)
static netc_tx_frame_info_t g_mgmtTxDirty[EXAMPLE_EP_TXBD_NUM];
static netc_tx_frame_info_t mgmtTxFrameInfo;
#endif

static netc_tx_frame_info_t txFrameInfo;
static uint8_t txFrameNum, rxFrameNum;
static volatile bool txOver;

/* Timer resource. */
netc_timer_handle_t g_timer_handle;

/* MAC address. */
static uint8_t g_macAddr[6] = {0x54, 0x27, 0x8d, 0x00, 0x00, 0x00};

netc_buffer_struct_t g_txBuff      = {.buffer = &g_txFrame, .length = sizeof(g_txFrame)};
netc_frame_struct_t gtxFrame      = {.buffArray = &g_txBuff, .length = 1};

static bool last_link_up = true;
/*******************************************************************************
 * Code
 ******************************************************************************/

static status_t EP_getlinkstatus(ep_handle_t *handle, uint8_t *link)
{
    *link = (last_link_up ? 1U : 0U);

    return kStatus_Success;
}

static status_t EP_getlinkspeed(ep_handle_t *handle, netc_hw_mii_speed_t *speed, netc_hw_mii_duplex_t *duplex)
{

    *speed  = kNETC_MiiSpeed2500M;
    *duplex = kNETC_MiiFullDuplex;

    return kStatus_Success;
}


/*! @brief Build Frame for single ring transmit. */
static void APP_BuildBroadCastFrame(void)
{
    uint32_t length = EXAMPLE_EP_TEST_FRAME_SIZE - 14U;
    uint32_t count;

    for (count = 0; count < 6U; count++)
    {
        g_txFrame[count] = 0xFFU;
    }
    memcpy(&g_txFrame[6], &g_macAddr[0], 6U);
    g_txFrame[12] = (length >> 8U) & 0xFFU;
    g_txFrame[13] = length & 0xFFU;

    for (count = 0; count < length; count++)
    {
        g_txFrame[count + 14U] = count % 0xFFU;
    }
}
#define MAX_INPUT_LEN 256
#define MAX_TOKENS 40
#define MAX_GATES 256
#define MAX_SCHEDULE_ENTRIES 768

typedef struct {
    int id;
    bool is_open;
    unsigned long duration;
} GateState;

typedef struct {
    uint32_t gate_mask;  // Bitmask of open gates (e.g., 0x1 = gate0 open)
    uint32_t duration_ns; // Duration in nanoseconds
    netc_tb_tgs_gate_type_t state;
} TasScheduleEntry;


#if ENISI
static status_t APP_SWT_PSPFAddISIEntry(netc_tb_isi_config_t *isiEntryCfg, uint32_t *isiEntryID, uint32_t *iSEID, uint8_t *smacAddr, uint32_t vlan_id);
static status_t APP_SWT_PSPFAddISTableEntry(netc_tb_is_config_t *isEntryCfg, uint32_t *iSEID, uint32_t *entries_ids, netc_tb_is_forward_action_t fwd_action, uint32_t *sgiEID, uint32_t *isqEID);
static status_t APP_SWT_PSPFAddISFTableEntry(netc_tb_isf_config_t *isfEntryCfg, uint32_t *iSEID, uint32_t *entries_ids,  uint32_t *isfEntryID, uint8_t *pcp, uint32_t *rpEID, uint32_t *sgiEID);
static status_t APP_SWT_PSPFAddISCTableEntry(uint32_t *entries_ids);

uint32_t defaultisEID = 470;
netc_tb_isi_config_t isiEntryCfg;

uint32_t isiEntryID, isfEntryID;
uint32_t entries_ids[][2] = {[kNETC_SWITCH0Port0] = {1,2},
                             [kNETC_SWITCH0Port1] = {3,4},
                            [kNETC_SWITCH0Port2] = {5,6},
                             [kNETC_SWITCH0Port3] = {7,8},
                             [kNETC_SWITCH0Port4] = {9,10}};
uint32_t iSEID[][1] = {[kNETC_SWITCH0Port0] = 1,
                       [kNETC_SWITCH0Port1] = 2,
                       [kNETC_SWITCH0Port2] = 3,
                       [kNETC_SWITCH0Port3] = 4,
                       [kNETC_SWITCH0Port4] = 5};


netc_tb_is_config_t isEntryCfg;
netc_tb_isf_config_t isfEntryCfg;
#endif

#if ENIPF
static status_t APP_SWT_AddIngressFilterTableEntry(netc_tb_ipf_config_t *ipfEntryCfg, uint32_t *entryID, netc_tb_ipf_forward_action_t action, uint8_t *smacAddr, uint8_t port_id);

#endif

#if ENVLANFWD
static status_t APP_SWT_AddVlanFilterEntry(netc_tb_vf_config_t *vfEntryCfg, uint32_t *vfEntryID, uint32_t vid, uint8_t forwarding_action, uint8_t pbitmap);
static status_t APP_SWT_AddFDBEntry(netc_tb_fdb_config_t *fdbEntryCfg, uint32_t *fdbEntryID, uint8_t *macAddr, uint8_t *port, uint32_t *etEID);
#endif

enum pol_rate_params {CIR, CBS, EIR, EBS};
#if ENPOLICER
/* 3.725 bits per second * 10000000U = 34Mbps */
uint32_t rate_params[] =  {[CIR] = 10000000U, [CBS] = 2000U, [EIR] = 0x0U, [EBS] = 0x0U};
static status_t APP_SWT_RxPSFPAddRPTableEntry(netc_tb_rp_config_t *rpEntryCfg, uint32_t *rpEID, uint32_t *rate_params);

#endif
    uint8_t smacAddr[][6] = {[kNETC_SWITCH0Port0] = {0x00, 0x10, 0x99, 0x00, 0x00, 0x01},
                             [kNETC_SWITCH0Port1] = {0x00, 0x10, 0x99, 0x00, 0x00, 0x02},
                             [kNETC_SWITCH0Port2] = {0x00, 0x10, 0x99, 0x00, 0x00, 0x03},
                             [kNETC_SWITCH0Port3] = {0x00, 0x10, 0x99, 0x00, 0x00, 0x04}
                             /*[kNETC_SWITCH0Port4] = {0x00, 0x10, 0x94, 0x00, 0x00, 0x05}*/};
    
    netc_swt_port_bitmap_t port_bitmap[][1] = {[kNETC_SWITCH0Port0] = {kNETC_SWTPort0Bit},
                                               [kNETC_SWITCH0Port1] = {kNETC_SWTPort1Bit},
                                               [kNETC_SWITCH0Port2] = {kNETC_SWTPort2Bit},
                                               [kNETC_SWITCH0Port3] = {kNETC_SWTPort3Bit},
                                            /* [kNETC_SWITCH0Port4] = {kNETC_SWTPort4Bit}*/};
#if ENIPF
    bool enipf[] = {true, true, true, true, true};
    netc_tb_ipf_config_t ipfEntryCfg;
    uint32_t entryID[5] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
    uint64_t matchCount;
#endif

#if ENSGCL
    uint32_t sgclEID[][1] = {[kNETC_SWITCH0Port0] = 1,
                             [kNETC_SWITCH0Port1] = 4,
                             [kNETC_SWITCH0Port2] = 7,
                             [kNETC_SWITCH0Port3] = 0xa,
                             [kNETC_SWITCH0Port4] = 0xd};

    netc_tb_sgcl_gcl_t sgclEntryCfg;
    netc_tb_sgi_config_t sgiEntryCfg;
    static status_t APP_SWT_PSPFAddSGCLTableEntry(netc_tb_sgcl_gcl_t *sgclEntryCfg, uint32_t *sgclEID, GateState *gates, int num_gates);
    static status_t APP_SWT_PSPFAddandUpdateSGITableEntry(netc_tb_sgi_config_t *sgiEntryCfg, uint32_t *sgiEID, uint32_t *sgclEID);
#endif

#if ENQBV
status_t SWT_TxPortTGSEnable(swt_handle_t *handle, netc_hw_port_idx_t portIdx, bool enable);
    netc_tb_tgs_gcl_t tgclEntryCfg;
    netc_tb_tgs_entry_id_t  port_map[][1] = {[kNETC_SWITCH0Port0] = kNETC_TGSSwtPort0,
                                            [kNETC_SWITCH0Port1] = kNETC_TGSSwtPort1,
                                            [kNETC_SWITCH0Port2] = kNETC_TGSSwtPort2,
                                            [kNETC_SWITCH0Port3] = kNETC_TGSSwtPort3,
                                            [kNETC_SWITCH0Port4] = kNETC_TGSSwtPort4};
static status_t  APP_SWT_ConfigQBVGCL(netc_tb_tgs_gcl_t *tgclEntryCfg, netc_tb_tgs_entry_id_t *port_map, TasScheduleEntry *entries, uint32_t num_entries, uint64_t base_time);
#endif
        
uint32_t sgiEID[][1] = {[kNETC_SWITCH0Port0] = 0,
                        [kNETC_SWITCH0Port1] = 1,
                        [kNETC_SWITCH0Port2] = 2,
                        [kNETC_SWITCH0Port3] = 3,
                        [kNETC_SWITCH0Port4] = 4};

uint32_t isqEID[][1] = {[kNETC_SWITCH0Port0] = 0,
                        [kNETC_SWITCH0Port1] = 1,
                        [kNETC_SWITCH0Port2] = 2,
                        [kNETC_SWITCH0Port3] = 3,
                        [kNETC_SWITCH0Port4] = 4};

uint8_t pcp[][1] = {[kNETC_SWITCH0Port0] = 2,
                       [kNETC_SWITCH0Port1] = 3,
                       [kNETC_SWITCH0Port2] = 4,
                       [kNETC_SWITCH0Port3] = 5,
                       [kNETC_SWITCH0Port4] = 6};

netc_tb_iseqg_config_t iseqg;
uint32_t etEID[][1] = {};
netc_tb_rp_stse_t swtRpStatis;

typedef struct {
    char* tokens[MAX_TOKENS];
    int count;
} CommandTokens;

// Command descriptions for help
const char* help_text = 
"\rAvailable commands:\r\n"
"  fdb add <mac> dev <port bitmap>                      - Add FDB entry\r\n"
"  ingr port filt add smac <mac> sport <portid> action  - Add ingress port filter\r\n"
"  vlan add <vlan_id> port_bitmap <bitmap> <action>     - Add VLAN with port bitmap\r\n"
"  stream add smac <mac> vid <vlan> index <is_eid>      - Add stream\r\n"
"  stream filter add stream_index <is_eid> prio <pcp> [policer <policer_id> | stream_gate_id <id>]\r\n"      
"                                                       - Add stream filter\r\n"
"  stream table stats stream_index <index>              - Display statistics for a given stream index\r\n"
"  policer add pol_id <policer_id> cir <cir_val> cbs <cbs_val> eir <eir_val> ebs <ebs_val>\r\n"     
"                                                       - Add policer\r\n"
"  add gate instance <id> stream_index <idx> cycle time <time> num_gates <n> gcl_id <sgclEID> gates states <states>\r\n"
"                                                       - Add gate instance (e.g., 0:open:300000 1:closed:200000)\r\n"
"  tas add port <port> maxSDU <sduTC0,..sduTC7> base time <ns> num_sched_entries <n> schedules <entries> masSDU <sduTC0,..sduTC8>\r\n"
"          - Add TAS schedule (e.g., 0x1:300000:S = gate 0 open for 300000ns in state Schedule)\r\n"
"  management <dest_port>                               - Loop redirected traffic to management port back on the dest_port. \r\n"
"                                                         Traffic is redirected by IPF table entry defined by the user. \r\n"
"                                                         For more details type management. \r\n"
"  help                                                 - Show this help\r\n"
"  clear                                                - Clear screen\r\n";

void clear_screen() {
    // ANSI escape sequence to clear screen and move cursor to top-left
    PRINTF("\033[2J\033[H");
        PRINTF("\r\n"
       "+--------------------------------------------+\r\n"
       "|   NETC Switch Network Configuration Console|\r\n"
       "|                   v1.1                     |\r\n"
       "+--------------------------------------------+\r\n"
       "\r\n"
       "  Supported Features:\r\n"
       "  * Ingress port filtering\r\n"
       "  * Ingress Stream Identification\r\n"
       "  * Ingress Stream Filtering\r\n"
       "  * Rate Policer and Stream gating with 1588 Timer\r\n"
       "  * VLAN filtering\r\n"
       "  * FDB lookup\r\n"
       "  * Time aware shaper (Qbv)\r\n"
       "  * Time aware shaper with Hold/Release (Preemption)\r\n"
       "  * Traffic reflector using management port (from PSI to front panel or from PSI to VSI)\r\n" 
       "\r\n"
       "  Type 'help' for available commands\r\n"
       "\r\n");
}

void parse_command(char* input, CommandTokens* tokens) {
    char* token = strtok(input, " \t\n");
    tokens->count = 0;
    
    while (token != NULL && tokens->count < MAX_TOKENS) {
        tokens->tokens[tokens->count++] = token;
        token = strtok(NULL, " \t\n");
    }
}

int validate_mac(const char* mac, uint8_t *dst) {
    // Simple MAC address validation (00:11:22:33:44:55 format)
    int i, ch = 0, pow = 16, pos = 0;
    for (i = 0; mac[i]; i++) {
        if (i % 3 == 2) {
            if (mac[i] != ':') return 0;
            ch = 0;
            pow = 16;
        } else {
            if (!isxdigit(mac[i])) return 0;
            if (i % 3 == 0) {
              ch =  (mac[i] <= 0x39) ? (mac[i] - 0x30): (mac[i] <= 0x46) ? (mac[i] - 0x37): (mac[i] - 0x57);
              ch = ch * pow;
            } else {
              ch = ch + ((mac[i] <= 0x39) ? (mac[i] - 0x30): (mac[i] <= 0x46) ? (mac[i] - 0x37): (mac[i] - 0x57));
              dst[pos++] = ch;
            }
        }
    }
    return (i == 17); // 6 groups of 2 hex digits + 5 colons
}

void handle_fdb_add(CommandTokens* tokens) {
    status_t result  = kStatus_Success;
    uint8_t smacAddr[6];
     
    if (tokens->count != 5 || strcmp(tokens->tokens[1], "add") != 0 || strcmp(tokens->tokens[3], "dev") != 0) {
        PRINTF("\nError: Usage: fdb add <mac> dev <port bitmap>\r\n");
        return;
    }
    
    if (!validate_mac(tokens->tokens[2], smacAddr)) {
        PRINTF("\nError: Invalid MAC address format (use 00:11:22:33:44:55)\r\n");
        return;
    }
    
    char* end;    
    uint8_t pbitmap = strtoul(tokens->tokens[4], &end, 10);
    if (*end != '\0' || pbitmap < 1 || pbitmap > 0xF) {
        PRINTF("\nError: Port bitmap must be between 1 and 0xF\r\n");
        return;
    }
    
    result = APP_SWT_AddFDBEntry(&fdbEntryCfg, &fdbEntryID, smacAddr, &pbitmap, etEID[0]);
    if (result != kStatus_Success)
    {
      PRINTF("Could not add MAC FDB entry. Error: %x\r\n", result);
      return;
    }
    PRINTF("\r\nAdded FDB entry - MAC: %s, Port: %s\r\n", tokens->tokens[2], tokens->tokens[4]);
}

void handle_ingr_port_filt_add(CommandTokens* tokens) {
    status_t result  = kStatus_Success;
    uint32_t entryID;
    uint8_t smacAddr[6];
    uint16_t act;
#if ENIPF
    if (tokens->count < 8 || strcmp(tokens->tokens[1], "port") != 0 || 
        strcmp(tokens->tokens[2], "filt") != 0 || strcmp(tokens->tokens[3], "add") != 0) {
        PRINTF("\nError: Usage: ingr port filt add smac <mac> sport <portid> action <act:kNETC_IPFForwardDiscard=0,kNETC_IPFForwardPermit=1,kNETC_IPFRedirectToMgmtPort=2,kNETC_IPFCopyToMgmtPort=3>\r\n");
        return;
    }
    
    if (strcmp(tokens->tokens[4], "smac") != 0 || !validate_mac(tokens->tokens[5], smacAddr) || 
        strcmp(tokens->tokens[6], "sport") != 0 || strcmp(tokens->tokens[8], "action") != 0) {
          PRINTF("\nError: Invalid format. Expected: smac <mac> sport <portid> action <act:kNETC_IPFForwardDiscard=0,kNETC_IPFForwardPermit=1,kNETC_IPFRedirectToMgmtPort=2,kNETC_IPFCopyToMgmtPort=3>\r\n");
        return;
    }

    char* end;
    uint16_t port = strtol(tokens->tokens[7], &end, 10);
    if (*end != '\0' ||  port > 3) {
        PRINTF("\nError: Port ID must be between 0 and 3\r\n");
        return;
    }
    
    act = strtol(tokens->tokens[9], &end, 10);
    if (*end != '\0' ||  act > 3) {
        PRINTF("\nError: Act must be between 0 and 3\r\n");
        return;
    }
    
    result = APP_SWT_AddIngressFilterTableEntry(&ipfEntryCfg, &entryID, act, smacAddr, port);
    if (result != kStatus_Success)
    {
      PRINTF("Could not add ingress port filter entry. Error %x\r\n", result);
      return;
    }

    PRINTF("\r\nAdded ingress port filter - MAC: %s, Port: %s action %d\r\n", tokens->tokens[5], tokens->tokens[7], act);
#endif
}

void handle_vlan_add(CommandTokens* tokens) {
    status_t result  = kStatus_Success;

    if (tokens->count < 3 || strcmp(tokens->tokens[1], "add") != 0) {
        PRINTF("\nError: Usage: vlan add <vlan_id> <port_bitmap> <action>\r\n");
        return;
    }
    
    char* end;
    long vlan_id = strtol(tokens->tokens[2], &end, 10);
    if (*end != '\0' || vlan_id < 1 || vlan_id > 4095) {
        PRINTF("\nError: VLAN ID must be between 1 and 4095\r\n");
        return;
    }

    long mfo = strtol(tokens->tokens[4], &end, 10);
    if (*end != '\0' || mfo < 1 || mfo > 3) {
      PRINTF("\nError: MAC forwarding action must be: 1 - kNETC_NoFDBLookUp, 2 - kNETC_FDBLookUpWithFlood, 3 - kNETC_FDBLookUpWithDiscard\r\n");
        return;
    }
    
    long pbitmap = strtoul(tokens->tokens[3], &end, 10);
    if (*end != '\0' || pbitmap < 1 || pbitmap > 0xF) {
        PRINTF("\nError: Port bitmap must be between 1 and 0xF\r\n");
        return;
    }
    
    result = APP_SWT_AddVlanFilterEntry(&vfEntryCfg, &vfEntryID, vlan_id, mfo, pbitmap);
    if (result != kStatus_Success)
    {
      PRINTF("Could not add VLAN / FDB entry. Error: %x\r\n", result);
        return;
    }
 
    PRINTF("\r\nAdded VLAN %ld with port bitmap %x and action %d\r\n", vlan_id, pbitmap, mfo);
}

void handle_stream_add(CommandTokens* tokens) {
    status_t result  = kStatus_Success;
    uint8_t smacAddr[6];
#if ENISI    
    if (tokens->count < 8 || strcmp(tokens->tokens[1], "add") != 0 || 
        strcmp(tokens->tokens[2], "smac") != 0 || strcmp(tokens->tokens[4], "vid") != 0) {
        PRINTF("\r\nError: Usage: stream add smac <mac> vid <vlan> index <is_eid>\r\n");
        return;
    }
    
    if (!validate_mac(tokens->tokens[3], smacAddr)) {
        PRINTF("\r\nError: Invalid MAC address format\r\n");
        return;
    }

    char* end;
    long vlan_id = strtol(tokens->tokens[5], &end, 10);
    if (*end != '\0' || vlan_id < 1 || vlan_id > 4095) {
        PRINTF("\r\nError: VLAN ID must be between 1 and 4095\r\n");
        return;
    }
  
    uint32_t iSEID = strtol(tokens->tokens[7], &end, 10);
    if (*end != '\0' || iSEID < 1 || iSEID > 1568) {
        PRINTF("\r\nError: Stream id must be between 1 and 1568\r\n");
        return;
    }


    result = APP_SWT_PSPFAddISIEntry(&isiEntryCfg, &isiEntryID, &iSEID, smacAddr, vlan_id);
    if (result != kStatus_Success)
    {
      PRINTF("Could not add ingress stream identification entry. Error: %x\r\n", result);
            return;
    }
 
 
#if 1
    /* here may be a potential bug. Treat with attention. Ingress stream identification entires = 1568 while ingress stream table entries = 480 */
    uint32_t counter_id = iSEID;
    /* add stream counter table entries for port i. Two entries are added for port i: one entry for stream table and one for stream filter table*/
    result = SWT_RxPSFPAddISCTableEntry(&g_swt_handle, counter_id);
    if (result != kStatus_Success)
    {
            PRINTF("Could not add ingress stream counter table entry\r\n");
            return;
    }
#endif


    result = APP_SWT_PSPFAddISTableEntry(&isEntryCfg, &iSEID, &counter_id, kNETC_ISBridgeForward, sgiEID[0], isqEID[0]);
    if (result != kStatus_Success)
    {
        PRINTF("Could not add ingress stream  table entries. Error: %x\r\n", result);
        return;
    }
    PRINTF("\r\nAdded stream - MAC: %s, VLAN: %ld  stream id: %u\r\n", tokens->tokens[3], vlan_id, iSEID);
    PRINTF("\r\nINFO: Ingress stream table entry added with stream id %u\r\n", iSEID);
#endif
}

void handle_stream_table_stats(CommandTokens* tokens) {
    status_t result  = kStatus_Success;
    netc_tb_isc_stse_t isCount[1] = {0};
    char* end;
#if ENISI
    if (tokens->count < 5 || strcmp(tokens->tokens[1], "table") != 0 || 
        strcmp(tokens->tokens[2], "stats") != 0) {
        PRINTF("\nError: Usage: stream table stats stream_index <index>\r\n");
        return;
    }

    uint32_t iSEID = strtol(tokens->tokens[4], &end, 10);
    if (*end != '\0' || iSEID < 1 || iSEID > 1568) {
        PRINTF("\nError: Stream id must be between 1 and 1568\r\n");
        return;
    }
    

   if ((kStatus_Success != SWT_RxPSFPGetISCStatistic(&g_swt_handle, iSEID, &isCount[0])))
    {
      PRINTF("\nError: Could not get statistics for stream table / stream filter table stream index %d\r\n", iSEID);
      return;
    } else {
      PRINTF("\r\n Ingress stream table stream match count: %lu\r\n", isCount[0].rxCount);
      PRINTF("\r\nNote: If stream matches stream filter table, stream table stats are no longer available\r\n");
    }
   
#endif   
    
}
void handle_stream_filter_add(CommandTokens* tokens) {
    status_t result  = kStatus_Success;
    char* end;
    uint32_t rpEID = 0, sgiEID;
    uint32_t *p_sgi = NULL;
    uint8_t pcp;
#if ENISI    
    if (tokens->count < 5 || strcmp(tokens->tokens[1], "filter") != 0 || 
        strcmp(tokens->tokens[2], "add") != 0) {
        PRINTF("\nError: Usage: stream filter add stream_index <is_eid> prio <pcp> [policer <policer_id> | stream_gate_id <id>]\r\n");
        return;
    }
    

    uint32_t iSEID = strtol(tokens->tokens[4], &end, 10);
    if (*end != '\0' || iSEID < 1 || iSEID > 1568) {
        PRINTF("\nError: Stream id must be between 1 and 1568\r\n");
        return;
    }
    
    uint32_t counter_id = iSEID;
    pcp = strtoul(tokens->tokens[6], &end, 10);
    if (*end != '\0' || pcp > 7) {
      PRINTF("\nError: PCP  must be between 0 and 7\r\n");
      return;
    }
    
    if (tokens->count == 9 && strcmp(tokens->tokens[7], "policer") == 0) {
      rpEID = strtoul(tokens->tokens[8], &end, 10);
      if (*end != '\0' || rpEID < 1 || rpEID > 32) {
        PRINTF("\nWarn: Rate policer id must be between 1 and 32. Policer will be disabled\r\n");
        return;
      }
    } else if (tokens->count == 9 && strcmp(tokens->tokens[7], "stream_gate_id") == 0) {
         sgiEID = strtoul(tokens->tokens[8], &end, 10);
         p_sgi = &sgiEID;
      if (*end != '\0' || sgiEID > 32) {
        PRINTF("\r\nWarn: Stream gate instance id must be between 1 and 32. Stream gate instance will be disabled\r\n");
        return;
      }
    } else if (tokens->count == 7 && strcmp(tokens->tokens[5], "prio") == 0) {
          pcp = strtoul(tokens->tokens[6], &end, 10);
          if (*end != '\0' || pcp > 7) {
              PRINTF("\nError: PCP  must be between 0 and 7\r\n");
              return;
          }
    } else {
            PRINTF("\r\n Invalid command.\r\n");
            return;
    }
    
    result = APP_SWT_PSPFAddISFTableEntry(&isfEntryCfg, &iSEID, &counter_id, &isfEntryID, &pcp, &rpEID, p_sgi);
    if (result != kStatus_Success)
    {
            PRINTF("Could not add ingress stream  filter table entry. Error %x\r\n", result);
            return;
    }
    if (tokens->count == 9)
      PRINTF("\r\nAdded stream filter - stream index: %s, PCP: %s, Policer ID / Gate instance ID: %s\r\n", 
           tokens->tokens[4], tokens->tokens[6], tokens->tokens[8]);
    else
      PRINTF("\r\nAdded stream filter - stream index: %s, PCP: %s\r\n", 
           tokens->tokens[4], tokens->tokens[6]);
#endif
}

void handle_policer_add(CommandTokens* tokens) {
    status_t result  = kStatus_Success;
    netc_tb_rp_config_t policerEntry;
    char* end;
    uint32_t rpEID = 0, cir, cbs, eir, ebs;
    uint32_t rate_params[] = {[CIR] = 0x0, [CBS] = 0x0, [EIR] = 0x0, [EBS] = 0x0};

    if (tokens->count < 3 || strcmp(tokens->tokens[1], "add") != 0) {
        PRINTF("\r\nError: Usage: policer add pol_id <policer_id> cir <cir_val> cbs <cbs_val> eir <eir_val> ebs <ebs_val>\r\n");
        return;
    }
    
    rpEID = strtol(tokens->tokens[3], &end, 10);
    if (*end != '\0' || rpEID < 1 || rpEID > 32) {
      PRINTF("\r\nWarn: Rate policer id must be between 1 and 32. Policer will not be created\r\n");
      return;
     }
    
    rate_params[CIR] = strtol(tokens->tokens[5], &end, 10);
    if (*end != '\0' || rpEID < 1 || rpEID > 32) {
      PRINTF("\r\nWarn: Rate policer CIR must be an integer\r\n");
      return;
    }
    
    rate_params[CBS] = strtol(tokens->tokens[7], &end, 10);
    if (*end != '\0' || rpEID < 1 || rpEID > 32) {
      PRINTF("\r\nWarn: Rate policer CBS must be an integer\r\n");
      return;
    }
    
    rate_params[EIR] = strtol(tokens->tokens[9], &end, 10);
    if (*end != '\0' || rpEID < 1 || rpEID > 32) {
      PRINTF("\r\nWarn: Rate policer EIR must be an integer\r\n");
      return;
    }
    
    rate_params[EBS] = strtol(tokens->tokens[11], &end, 10);
    if (*end != '\0' || rpEID < 1 || rpEID > 32) {
      PRINTF("\r\nWarn: Rate policer EIR must be an integer\r\n");
      return;
    }
#if ENPOLICER
    /*for now we add a policer with a rate of 34mbps. Params are hardcoded inside function */
    result = APP_SWT_RxPSFPAddRPTableEntry(&policerEntry, &rpEID, rate_params);
    if (result != kStatus_Success)
     {
       PRINTF("\r\nCould not add rate policer. Error: %x\r\n", result);
      return;
     }
#endif
    PRINTF("\r\nAdded policer with ID: %s cir %s cbs %s eir %s ebs %s.\r\n",
           tokens->tokens[3], tokens->tokens[5], tokens->tokens[7], tokens->tokens[9], tokens->tokens[11]);
}

bool parse_gate_state(const char* str, GateState* gate) {
    const char *p = str;
    char *endptr;
    
    // Parse gate ID
    gate->id = strtol(p, &endptr, 10);
    if (endptr == p || *endptr != ':') return false;
    p = endptr + 1;
    
    // Parse state (open/closed)
    if (strncmp(p, "open:", 5) == 0) {
        gate->is_open = true;
        p += 5;
    } 
    else if (strncmp(p, "closed:", 7) == 0) {
        gate->is_open = false;
        p += 7;
    } 
    else {
        return false;
    }
    
    // Parse duration
    gate->duration = strtoul(p, &endptr, 10);
    if (endptr == p) return false;
    
    return true;
}

void handle_gate_instance_add(CommandTokens* tokens) {
      status_t result  = kStatus_Success;
      unsigned long cycle_time, sum = 0;
      bool found = false;
      int stream_index, num_gates;
      char* end;
      
      uint32_t sgclEID, sgiEID;
    // Expected format: add gate instance 0 stream_index 1 cycle time 1300000 num_gates 4 gcl_id <sgclEID> gates  states 0:open:300000 1:open:300000 2:open:300000 3:closed:300000
    
    if (tokens->count < 15 || 
        strcmp(tokens->tokens[0], "add") != 0 ||
        strcmp(tokens->tokens[1], "gate") != 0 ||
        strcmp(tokens->tokens[2], "instance") != 0 ||
        strcmp(tokens->tokens[4], "stream_index") != 0 ||
        strcmp(tokens->tokens[6], "cycle") != 0 ||
        strcmp(tokens->tokens[7], "time") != 0 ||
        strcmp(tokens->tokens[9], "num_gates") != 0 ||
        strcmp(tokens->tokens[11], "gcl_id") != 0 ||
        strcmp(tokens->tokens[14], "states") != 0) {
        PRINTF("\r\nError: Invalid gate instance command format\r\n");
        PRINTF("\r\nUsage: add gate instance <id> stream_index <idx> cycle time <time> num_gates <n> gcl_id <sgclEID> gates states <states>\r\n");
        PRINTF("\r\nExample: add gate instance 0 stream_index 1 cycle time 1300000 num_gates 4 gcl_id <sgclEID> gates states 0:open:300000 1:open:300000 2:open:300000 3:closed:300000\r\n");
        return;
    }

    stream_index = atoi(tokens->tokens[5]);
    cycle_time = strtoul(tokens->tokens[8], NULL, 10);
    num_gates = atoi(tokens->tokens[10]);

    // Verify num_gates matches the number of state tokens
    int expected_state_tokens = tokens->count - 15;
    if (expected_state_tokens != num_gates) {
        PRINTF("\r\nError: num_gates (%d) doesn't match provided states (%d)\r\n", num_gates, expected_state_tokens);
        return;
    }

    if (num_gates > MAX_GATES) {
        PRINTF("\r\nError: Too many gates (max %d)\r\n", MAX_GATES);
        return;
    }

    GateState gates[MAX_GATES];
    bool parse_error = false;

    // Parse each gate state
    for (int i = 0; i < num_gates; i++) {
        if (!parse_gate_state(tokens->tokens[15 + i], &gates[i])) {
            PRINTF("\r\nError: Invalid gate state format at position %d: %s\r\n", i, tokens->tokens[15 + i]);
            PRINTF("\r\nExpected format: id:open|closed:duration (e.g., 0:open:300000)\r\n");
            parse_error = true;
            break;
        }
        
        // Verify gate ID matches position
        if (gates[i].id != i) {
            PRINTF("\r\nError: Gate ID %d doesn't match position %d\r\n", gates[i].id, i);
            parse_error = true;
            break;
        }
        sum += gates[i].duration;
    }
    
    if (sum != cycle_time) {
      PRINTF("\r\nError: Cycle time is not equal with the sum of gates states time\r\n");
      parse_error = true;
    }
    
    if (parse_error) {
        return;
    }

    sgclEID = strtoul(tokens->tokens[12], &end, 10);
    if (*end != '\0' || sgclEID < 1 || sgclEID > 255) {
      PRINTF("\r\nWarn: sgclEID is not valid\r\n");
      return;
    }
    
    sgiEID = strtoul(tokens->tokens[3], &end, 10);
    if (*end != '\0' || sgiEID > 255) {
      PRINTF("\r\nWarn: sgiEID is not valid\r\n");
      return;
    }
#if ENPOLICER
    result = APP_SWT_PSPFAddSGCLTableEntry(&sgclEntryCfg, &sgclEID, &gates[0], num_gates);
    if (result != kStatus_Success)
    {
      PRINTF("\r\nCould not add ingress stream  gate control list entry. Error: %x\r\n", result);
        return;
    }

    result = APP_SWT_PSPFAddandUpdateSGITableEntry(&sgiEntryCfg, &sgiEID, &sgclEID);
    if (result != kStatus_Success)
    {
      PRINTF("\r\nCould not add /update stream gate instance table entry. Error: %x\r\n", result);
      return;
    }
#endif
    // Print parsed information
    PRINTF("\r\nAdded gate instance %d\r\n", sgiEID);
    PRINTF("  Stream index: %d\r\n", stream_index);
    PRINTF("  Cycle time: %lu ns\r\n", cycle_time);
    PRINTF("  Number of gates: %d\r\n", num_gates);
    PRINTF("  Gate states:\r\n");
    
    for (int i = 0; i < num_gates; i++) {
        PRINTF("    Gate state %d: %s for %lu ns\r\n", 
               gates[i].id, gates[i].is_open ? "open" : "closed", gates[i].duration);
    }
}

bool parse_hex_number(const char* str, uint32_t* result) {
    *result = 0;
    if (strncmp(str, "0x", 2) != 0) return false;
    
    for (int i = 2; str[i] != '\0'; i++) {
        char c = tolower(str[i]);
        if (c == ':') break;
        
        *result <<= 4;
        if (c >= '0' && c <= '9') *result |= (c - '0');
        else if (c >= 'a' && c <= 'f') *result |= (c - 'a' + 10);
        else return false;
    }
    return true;
}

bool parse_schedule_entry(const char* str, TasScheduleEntry* entry) {
    const char* colon = strchr(str, ':');
    if (!colon) return false;
    
    // Parse hex gate mask (before colon)
    if (!parse_hex_number(str, &entry->gate_mask)) {
        return false;
    }
    
    const char* colon1 = strchr(colon + 1, ':');
    if (!colon1) return false;
    
    // Parse duration (after colon)
    entry->duration_ns = 0;
    const char* p = colon + 1;
    while (*p >= '0' && *p <= '9') {
        entry->duration_ns = entry->duration_ns * 10 + (*p - '0');
        p++;
    }
    
    switch (*(colon1 + 1)) {
      case 'S':entry->state = kNETC_SetGateStates; break;
      case 'H':entry->state = kNETC_SetAndHoldMac; break;
      case 'R':entry->state = kNETC_SetAndReleaseMac; break;
      default: return false;
    
    }
    return (*(colon1 + 2) == '\0'); // Ensure we parsed the entire string
}

void handle_tas_add(CommandTokens* tokens) {
    status_t result  = kStatus_Success;
    uint32_t  sum = 0;
    bool found = false;
    char* end;
    uint8_t port = 0;
    uint16_t maxsdu[8] = {0};
    netc_port_tc_sdu_config_t config = {0};
    // Verify command structure
    if (tokens->count < 19 || 
        strcmp(tokens->tokens[0], "tas") != 0 ||
        strcmp(tokens->tokens[1], "add") != 0 ||
        strcmp(tokens->tokens[2], "port") != 0 ||
        strcmp(tokens->tokens[13], "base") != 0 ||
        strcmp(tokens->tokens[14], "time") != 0 ||
        strcmp(tokens->tokens[18], "schedules") != 0) {
        PRINTF("\r\nError: Invalid TAS command format\r\n");
        PRINTF("\r\nUsage: tas add port <port_id> maxsdu <sduTC0..sduTC7> base time <ns> num_sched_entries <n> schedules <entries>\r\n");
        PRINTF("\r\nExample: tas add port 0 maxsdu 1500..1500 cycle time 1200000 num_sched_entries 4 schedules 0x1:300000:H 0x2:300000:R 0x8:300000:H 0x10:300000\r\n");
        return;
    }

    port = strtoul(tokens->tokens[3], &end, 10);
    if (*end != '\0' || port > 3) {
      PRINTF("\r\nWarn: port id is not valid\r\n");
      return;
    }
    
    // Parse base time
    uint64_t base_time = 0;
    const char* p = tokens->tokens[15];
    while (*p >= '0' && *p <= '9') {
        base_time = base_time * 10 + (*p - '0');
        p++;
    }
    if (*p != '\0') {
        PRINTF("\r\nError: Invalid base time format\r\n");
        return;
    }

    // Parse number of schedule entries
    uint32_t num_entries = 0;
    p = tokens->tokens[17];
    while (*p >= '0' && *p <= '9') {
        num_entries = num_entries * 10 + (*p - '0');
        p++;
    }
    if (*p != '\0') {
        PRINTF("\r\nError: Invalid num_sched_entries format\r\n");
        return;
    }

    // Verify number of entries matches provided schedules
    if (tokens->count - 19 != num_entries) {
        PRINTF("\r\nError: num_sched_entries (%u) doesn't match provided schedules (%u)\r\n",
               num_entries, tokens->count - 19);
        return;
    }
      
    // Parse each schedule entry
    TasScheduleEntry entries[MAX_SCHEDULE_ENTRIES]; // Define appropriate max
    for (uint32_t i = 0; i < num_entries; i++) {
        if (!parse_schedule_entry(tokens->tokens[19 + i], &entries[i])) {
            PRINTF("\r\nError: Invalid schedule entry format at position %u: %s\r\n",
                   i, tokens->tokens[19 + i]);
            PRINTF("\r\nExpected format: 0x<hex_mask>:<duration_ns> (e.g., 0x1:300000)\r\n");
            return;
        }
        sum += entries[i].duration_ns;

    }
#if 0
    if (base_time != sum) {
        PRINTF("\r\nError: Cycle time is not equal with the sum of gates schedules time\r\n");
        return;
    }
#endif
#if ENQBV
        
    for (uint8_t prio = 0; prio < 8; prio++) {
      maxsdu[prio] = strtoul(tokens->tokens[5 + prio], &end, 10);
      if (*end != '\0' || maxsdu[prio] > 9600) {
        PRINTF("\r\nWarn: maxsdu value is not valid\r\n");
        return;
      }
      config.enTxMaxSduCheck = true;
      config.maxSduSized = maxsdu[prio];
      result |= SWT_TxSDUConfigPort(&g_swt_handle, port, prio, &config);
    }
    if (result != kStatus_Success)
    {
      PRINTF("\r\nCould not configure sdu. Error: %x r\n", result);
      return;
    }
    
    result = SWT_TxPortTGSEnable(&g_swt_handle, port, true);
    if (result != kStatus_Success)
    {
      PRINTF("\r\nCould not enable time gating. Error: %x r\n", result);
      return;
    }

    result = APP_SWT_ConfigQBVGCL(&tgclEntryCfg, &port, entries, num_entries, base_time);
    if (result != kStatus_Success)
    {
      PRINTF("\r\nCould not configure QBV GCL. Error: %x r\n", result);
      return;
    }
    
 for (uint32_t i = 0; i < num_entries; i++) {         
        /*enable preemption on preemptible queues */
      for (uint8_t prio = 0; prio < 8; prio++) {
        if ((1 << prio) & entries[i].gate_mask) {
          bool enPreemption;
          enPreemption = (entries[i].state == kNETC_SetAndReleaseMac) ? true : false;

          NETC_PORT_Type *base = g_swt_handle.hw.ports[port].port;
          uint32_t  temp = base->PFPCR & (~((uint32_t)1U << (uint8_t)prio));
          base->PFPCR   = temp | ((uint32_t)enPreemption << (uint8_t)prio);
        }
      }
 }
#endif
    // Print confirmation with parsed information
    PRINTF("\r\nAdded TAS schedule\r\n");
    PRINTF("  Base time: %llu ns\r\n", base_time);
    PRINTF("  Schedule entries (%u):\r\n", num_entries);
    PRINTF("  max sdu: ");
    for (int prio = 0; prio < 8; prio++) {
      PRINTF("%u ", maxsdu[prio]);
    }
    PRINTF("\r\n");
    for (uint32_t i = 0; i < num_entries; i++) {
      PRINTF("    [%02u] Gates: 0x%X, Duration: %u ns, State: %s\r\n",
               i, entries[i].gate_mask, entries[i].duration_ns,
               (entries[i].state == kNETC_SetAndReleaseMac) ? "R" : 
               (entries[i].state == kNETC_SetAndHoldMac) ? "H" : "S");
    }
}

static void netc_si_msg_thread(void *arg)
{
    status_t result               = kStatus_Success;
    netc_psi_rx_msg_t msgInfo;

    while (1)
    {
        result = EP_PsiRxMsg(&g_ep_handle, kNETC_Vsi1, &msgInfo);
        if (result == kStatus_Success)
        {
            EP_PsiHandleRxMsg(&g_ep_handle, 1, &msgInfo);
        }

#if (NETC_VSI_NUM_USED > 1)
        result = EP_PsiRxMsg(ethernetif->ep_handle, kNETC_Vsi2, &msgInfo);
        if (result == kStatus_Success)
        {
            EP_PsiHandleRxMsg(ethernetif->ep_handle, 2, &msgInfo);
        }
#endif
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void reflector(void *pvParameter)
{
  status_t result                  = kStatus_Success;
  uint32_t length;
  uint8_t port = *((uint8_t *)pvParameter);
  netc_swt_tag_port_no_ts_t tag = {
  .comTag = {
      .tpid = NETC_SWITCH_DEFAULT_ETHER_TYPE,
      .subType = kNETC_TagToPortNoTs,
      .type = kNETC_TagToPort,
      .qv = 1,
      .ipv = 0,
      .dr = 0,
      .swtId = 1,
      .port = port
  }
  };

  /* for traffic sent to switch management port, send it to the available VSI*/
  if (port == EXAMPLE_SWT_PSEUDO_PORT) {
    tag.comTag.swtId = 0;
    tag.comTag.port = 1;
  }

  while (1){
        result = SWT_GetRxFrameSize(&g_swt_handle, &length);
        if (length) {
          result = SWT_ReceiveFrameCopy(&g_swt_handle, g_txFrame, length, NULL);
          if (result == kStatus_Success) {
              memcpy(&g_txFrame[12], &tag, sizeof(tag));
              gtxFrame.buffArray->length = length;
              result = SWT_SendFrame(&g_swt_handle, &gtxFrame, NULL, NULL);
              SWT_ReclaimTxDescriptor(&g_swt_handle, 0);
              if (mgmtTxFrameInfo.status != kNETC_EPTxSuccess || result)
              {
                  PRINTF("\r\nTransmit frame has error %x!\r\n", result);
              }
           }
      }
      vTaskDelay(pdMS_TO_TICKS(1));
  } 
}


void reset_rgmii() {
int port_id = EXAMPLE_SWT_PORT2;

  APP_PHY_Init(&port_id);

}
void handle_management_port(CommandTokens* tokens) {
   uint32_t length;
   status_t result                  = kStatus_Success;
   char* end;
   uint8_t seconds, dest_port;
   static TaskHandle_t xHandle = NULL;
   
    if (tokens->count < 2) {
        PRINTF("\r\nError: Invalid command format\r\n");
        PRINTF("\r\nUsage: management <dest_port> \r\n");
        PRINTF("\r\nExample: management 1. Dest port can be in range 0-2 for front panel ports.\r\nFor sending traffic from PSI to VSI, set dest_port to 3.\r\n");
        return;
    }

    dest_port = strtoul(tokens->tokens[1], &end, 10);
    if (*end != '\0' || dest_port > 3) {
      PRINTF("\r\nWarn: Destination port is greater than 3\r\n");
      return;
    }
    
       PRINTF("\r\nIn CPU loop reflecting packets received on management port back to port %u", dest_port);
#if 1
    if (xHandle != NULL)
       vTaskDelete( xHandle );

    if (xTaskCreate(reflector, "Reflector", 1024U,  &dest_port, 2, &xHandle) != pdPASS)
    {
        PRINTF("\r\nFailed to create Reflector task\r\n");
        return;
    }
#endif
#if 0
  uint64_t start, stop;
  NETC_TimerGetCurrentTime(&g_timer_handle, &start);
  do {
  result = EP_GetRxFrameSize(&g_ep_handle, 0, &length);
// } while (result == kStatus_NETC_RxFrameEmpty);
  if (result == kStatus_NETC_RxHRNotZeroFrame) {
       netc_swt_tag_port_no_ts_t tag = {
        .comTag = {
            .tpid = NETC_SWITCH_DEFAULT_ETHER_TYPE,
            .subType = kNETC_TagToPortNoTs,
            .type = kNETC_TagToPort,
            .qv = 1,
            .ipv = 0,
            .dr = 0,
            .swtId = 1,
            .port = dest_port
        }
      };
        
       result = EP_ReceiveFrameCopy(&g_ep_handle, 0, g_txFrame, length, NULL);
       memcpy(&g_txFrame[12], &tag, sizeof(tag));
       gtxFrame.buffArray->length = length;
       result = SWT_SendFrame(&g_swt_handle, &gtxFrame, NULL, NULL);
       SWT_ReclaimTxDescriptor(&g_swt_handle, 0);
       if (mgmtTxFrameInfo.status != kNETC_EPTxSuccess || result)
        {
            PRINTF("\r\nTransmit frame has error %x!\r\n", result);
            return;
        }
  }
   NETC_TimerGetCurrentTime(&g_timer_handle, &stop);                           
  } while ((stop - start) <= (seconds * 1000000000ULL));
#endif
}
void process_command(CommandTokens* tokens) {
    if (tokens->count == 0) return;
    
    if (strcmp(tokens->tokens[0], "fdb") == 0) {
        handle_fdb_add(tokens);
    } 
    else if (strcmp(tokens->tokens[0], "ingr") == 0) {
        handle_ingr_port_filt_add(tokens);
    }
    else if (strcmp(tokens->tokens[0], "vlan") == 0) {
        handle_vlan_add(tokens);
    }
    else if (strcmp(tokens->tokens[0], "stream") == 0) {
        if (tokens->count > 1 && strcmp(tokens->tokens[1], "filter") == 0) {
            handle_stream_filter_add(tokens);
        } else if (strcmp(tokens->tokens[1], "add") == 0) {
            handle_stream_add(tokens);
        } else
            handle_stream_table_stats(tokens);
    }
    else if (strcmp(tokens->tokens[0], "policer") == 0) {
        handle_policer_add(tokens);
    }
    else if (strcmp(tokens->tokens[0], "add") == 0) {
       if (tokens->count > 1 && strcmp(tokens->tokens[1], "gate") == 0) {
        handle_gate_instance_add(tokens);
       }
    } else if (strcmp(tokens->tokens[0], "tas") == 0) {
      if (tokens->count > 1 && strcmp(tokens->tokens[1], "add") == 0) {
        handle_tas_add(tokens);
       }
    } else if (strcmp(tokens->tokens[0], "management") == 0) {
        handle_management_port(tokens);
    }
    else if (strcmp(tokens->tokens[0], "help") == 0) {
        PRINTF("%s", help_text);

    }
    else if (strcmp(tokens->tokens[0], "clear") == 0) {
        clear_screen();
    }
    else if (strcmp(tokens->tokens[0], "reset_rgmii") == 0) {
        reset_rgmii();
    }
    else {
        PRINTF("\r\nError: Unknown command. Type 'help' for available commands.\r\n");
    }
}

void print_prompt() {
    PRINTF("\r\n> ");
}

void console(void *pvParameters) {
    char input[MAX_INPUT_LEN];
    int pos = 0;
    char c;
    
    clear_screen();
    print_prompt();
    
    while (1) {   
        c = GETCHAR();
        
        if (c == '\r') {  // Handle Enter key
            if (pos > 0) {
                input[pos] = '\0';
                
                CommandTokens tokens;
                parse_command(input, &tokens);
                process_command(&tokens);
                
                pos = 0;
            }
            print_prompt();
        } 
        else if (c == '\b' || c == 127) {  // Handle backspace/delete
            if (pos > 0) {
                pos--;
                PRINTF("\b \b");  // Erase the character
            }
        }
        else if (c >= 32 && c <= 126 && pos < MAX_INPUT_LEN - 1) {  // Printable characters
            input[pos++] = c;
            PUTCHAR(c);  // Echo the character
        }
    }

    
    return;
}

#if defined(FSL_FEATURE_NETC_HAS_SWITCH_TAG) && FSL_FEATURE_NETC_HAS_SWITCH_TAG
/*! @brief Build Frame for single ring transmit. */
static void APP_BuildBroadCastFrameSwtTag(uint8_t port)
{
    netc_swt_tag_port_no_ts_t tag = {
        .comTag = {
            .tpid = NETC_SWITCH_DEFAULT_ETHER_TYPE,
            .subType = kNETC_TagToPortNoTs,
            .type = kNETC_TagToPort,
            .qv = 1,
            .ipv = 0,
            .dr = 0,
            .swtId = 1,
            .port = port
        }
    };
    uint32_t headerSize = 14U + sizeof(tag);
    uint32_t length = EXAMPLE_EP_TEST_FRAME_SIZE - headerSize;
    uint32_t count;

    for (count = 0; count < 6U; count++)
    {
        g_txFrame[count] = 0xFFU;
    }
    memcpy(&g_txFrame[6], &g_macAddr[0], 6U);
    memcpy(&g_txFrame[12], &tag, sizeof(tag));

    g_txFrame[12 + sizeof(tag)] = (length >> 8U) & 0xFFU;
    g_txFrame[13 + sizeof(tag)] = length & 0xFFU;

    for (count = 0; count < length; count++)
    {
        g_txFrame[count + headerSize] = count % 0xFFU;
    }
}
#endif

static status_t APP_ReclaimCallback(ep_handle_t *handle, uint8_t ring, netc_tx_frame_info_t *frameInfo, void *userData)
{
    txFrameInfo = *frameInfo;
    return kStatus_Success;
}

#if !(defined(FSL_FEATURE_NETC_HAS_NO_SWITCH) && FSL_FEATURE_NETC_HAS_NO_SWITCH)
static status_t APP_SwtReclaimCallback(swt_handle_t *handle, netc_tx_frame_info_t *frameInfo, void *userData)
{
    mgmtTxFrameInfo = *frameInfo;
    return kStatus_Success;
}
#endif

void msgintrCallback(MSGINTR_Type *base, uint8_t channel, uint32_t pendingIntr)
{
#if (NETC_VSI_NUM_USED > 0)
    uint32_t msg_recv_flags = kNETC_PsiRxMsgFromVsi1Flag;
#endif
    /* Transmit interrupt */
    if ((pendingIntr & (1U << EXAMPLE_TX_INTR_MSG_DATA)) != 0U)
    {
        EP_CleanTxIntrFlags(&g_ep_handle, 1, 0);
        txOver = true;
    }
    /* Receive interrupt */
    if ((pendingIntr & (1U << EXAMPLE_RX_INTR_MSG_DATA)) != 0U)
    {
        EP_CleanRxIntrFlags(&g_ep_handle, 1);
    }
    
#if (NETC_VSI_NUM_USED > 0)
    /* PSI Rx interrupt */
    if ((pendingIntr & (1U << SI_COM_INTR_MSG_DATA)) != 0U)
    {
        EP_PsiClearStatus(&g_ep_handle, msg_recv_flags);
    }
#endif
}

#if ENPOLICER

static status_t APP_SWT_RxPSFPAddRPTableEntry(netc_tb_rp_config_t *rpEntryCfg, uint32_t *rpEID, uint32_t *rate_params)
{
    status_t result  = kStatus_Success;
    uint32_t entryNum           = SWT_RxPSFPGetRPTableRemainEntryNum(&g_swt_handle);
    /* rate limit traffic at 30Mbps */
    memset(rpEntryCfg, 0U, sizeof(*rpEntryCfg));
    rpEntryCfg->entryID  = *rpEID;
    rpEntryCfg->fee.fen  = 1U;
    rpEntryCfg->cfge.cir =  rate_params[CIR];
    rpEntryCfg->cfge.cbs =  rate_params[CBS];
    rpEntryCfg->cfge.eir =  rate_params[EIR];
    rpEntryCfg->cfge.ebs =  rate_params[EBS];
    result              = SWT_RxPSFPAddRPTableEntry(&g_swt_handle, rpEntryCfg);
    if (kStatus_Success != result)
    {
        return kStatus_Fail;
    }
    if ((entryNum == 0U) || (entryNum != (SWT_RxPSFPGetRPTableRemainEntryNum(&g_swt_handle) + 1U)))
    {
        return kStatus_Fail;
    }
    
    return result;
}
#endif

#if ENISI
static status_t APP_SWT_PSPFAddISIEntry(netc_tb_isi_config_t *isiEntryCfg, uint32_t *isiEntryID, uint32_t *iSEID, uint8_t *smacAddr, uint32_t vlan_id)
{
    status_t result  = kStatus_Success;

    uint32_t entryNum = SWT_RxPSFPGetISITableRemainEntryNum(&g_swt_handle);

	
    /* Add Ingress Stream Identification entry with source MAC addr and VID info */
    memset(isiEntryCfg, 0, sizeof(netc_tb_isi_config_t));
    isiEntryCfg->keye.keyType = kNETC_KCRule0;
    memcpy(&isiEntryCfg->keye.framekey[0], &smacAddr[0], 6);
	/* in the key we use only the VID not the PCP because opcpp is not set in the profile */
    isiEntryCfg->keye.framekey[6] = 0x80;/*VALID,PCP */
    isiEntryCfg->keye.framekey[7] = vlan_id;/*VID*/
    isiEntryCfg->cfge.iSEID       = *iSEID;
    result                        = SWT_RxPSFPAddISITableEntry(&g_swt_handle, isiEntryCfg, isiEntryID);
    
	if (/*(*isiEntryID == 0U) ||*/ (result != kStatus_Success))
    {
        return kStatus_Fail;
    }
	
    if ((entryNum == 0U) || (entryNum != (SWT_RxPSFPGetISITableRemainEntryNum(&g_swt_handle) + 1U)))
    {
        return kStatus_Fail;
    }
	

    return result;
}

static status_t APP_SWT_PSPFAddISCTableEntry(uint32_t *entries_ids)
{
    status_t result  = kStatus_Success;
    uint32_t    entryNum = SWT_RxPSFPGetISCTableRemainEntryNum(&g_swt_handle);

    /* Counter for ingress stream table entry match */
    if (kStatus_Success != SWT_RxPSFPAddISCTableEntry(&g_swt_handle, entries_ids[0]))
    {
        return kStatus_Fail;
    }

     /* Counter for ingress stream filter table entry match */
    if (kStatus_Success != SWT_RxPSFPAddISCTableEntry(&g_swt_handle, entries_ids[1]))
    {
        return kStatus_Fail;
    }
 
    if ((entryNum == 0U) || (entryNum != (SWT_RxPSFPGetISCTableRemainEntryNum(&g_swt_handle) + 2U)))
    {
        return kStatus_Fail;
    }
	
	return result;
}

static status_t APP_SWT_PSPFAddISTableEntry(netc_tb_is_config_t *isEntryCfg, uint32_t *iSEID, uint32_t *entries_ids, netc_tb_is_forward_action_t fwd_action, uint32_t *sgiEID, uint32_t *isqEID)
{
    status_t result  = kStatus_Success;
	
    uint32_t entryNum = SWT_RxPSFPGetISTableRemainEntryNum(&g_swt_handle);
    /* Add Ingress Stream Table entry */
    memset(isEntryCfg, 0, sizeof(netc_tb_is_config_t));
    isEntryCfg->entryID = *iSEID;
    isEntryCfg->cfge.fa = fwd_action;//kNETC_ISBridgeForward;//kNETC_ISCopyToMgmtPortAndStream;
    /* Enable an additional ISF lookup */
    isEntryCfg->cfge.sfe = 1;
    /* Need specified a software defined Host Reason (8-15) when frame is redirected or copied to the switch management
     * port */
    isEntryCfg->cfge.hr = kNETC_SoftwareDefHR0;
    /* Disable Source Port Pruning */
    isEntryCfg->cfge.sppd        = 1;
	/*From RM: For FA = 011b, 101 (802.1Q bridge forwarding), this field identifies destination ports to which egress packet procesing is required.
	For now there is no packet processing on egress. Kept this as it was for fa = kNETC_ISCopyToMgmtPortAndStream */
    isEntryCfg->cfge.ePortBitmap = kNETC_SWTPort4Bit | kNETC_SWTPort3Bit | kNETC_SWTPort2Bit | kNETC_SWTPort1Bit | kNETC_SWTPort0Bit;
/*we disable stream gate instance on stream table for now. Use it on filter table */
#if ENSGCL1
	/* Set Stream Gate Instance Entry*/
    isEntryCfg->cfge.osgi   = 1;
    isEntryCfg->cfge.sgiEID = *sgiEID;	
#endif
    isEntryCfg->cfge.ifmEID      = 0xFFFFFFFF;
    isEntryCfg->cfge.etEID       = 0xFFFFFFFF;

#if ENSEQ
	/* IEEE802.1CB R-TAG  */
    isEntryCfg->cfge.isqa   = kNETC_ISPreformFRER;
    isEntryCfg->cfge.isqEID = *isqEID;
#endif
	
	
    /* Add default Ingress Stream counter entry for this Ingress Stream entry */
    isEntryCfg->cfge.iscEID = entries_ids[0];
    if (kStatus_Success != SWT_RxPSFPAddISTableEntry(&g_swt_handle, isEntryCfg))
    {
        return kStatus_Fail;
    }
    if ((entryNum == 0U) || (entryNum != (SWT_RxPSFPGetISTableRemainEntryNum(&g_swt_handle) + 1U)))
    {
        return kStatus_Fail;
    }
	
	return result;
}


static status_t APP_SWT_PSPFAddISFTableEntry(netc_tb_isf_config_t *isfEntryCfg, uint32_t *iSEID, uint32_t *entries_ids,  uint32_t *isfEntryID, uint8_t *pcp, uint32_t *rpEID, uint32_t *sgiEID)
{
	
    status_t result  = kStatus_Success;
    
    uint32_t entryNum = SWT_RxPSFPGetISFTableRemainEntryNum(&g_swt_handle);
    /* Add Ingress Stream Filter Table entry */
    memset(isfEntryCfg, 0, sizeof(netc_tb_isf_config_t));
    isfEntryCfg->keye.isEID = *iSEID;
    isfEntryCfg->keye.pcp   = *pcp;
    /* Overwrite the Ingress Stream counter entry when frame with IS_EID has EXAMPLE_FRAME_PCP_2 PCP value  */
    isfEntryCfg->cfge.iscEID = entries_ids[0];
#if ENPOLICER
    if (*rpEID) {
      isfEntryCfg->cfge.orp = 1;
      isfEntryCfg->cfge.rpEID = *rpEID;
    }
#endif
#if ENSGCL
    if (sgiEID) {
	/* Set Stream Gate Instance Entry*/
        isfEntryCfg->cfge.osgi   = 1;
        isfEntryCfg->cfge.sgiEID = *sgiEID;	
    }
#endif
    result                  = SWT_RxPSFPAddISFTableEntry(&g_swt_handle, isfEntryCfg, isfEntryID);
    if (/*(*isfEntryID == 0U) ||*/ (result != kStatus_Success))
    {
        return kStatus_Fail;
    }
    if ((entryNum == 0U) || (entryNum != (SWT_RxPSFPGetISFTableRemainEntryNum(&g_swt_handle) + 1U)))
    {
        return kStatus_Fail;
    }
    
    return result;
}
	
	
#endif

#if ENSGCL
#define NUM_GATE_LIST_ENTRIES (4U)
#define FRAME_SIZE (1000U)
static status_t APP_SWT_PSPFAddSGCLTableEntry(netc_tb_sgcl_gcl_t *sgclEntryCfg, uint32_t *sgclEID, GateState *gates, int num_gates)
{
    status_t result  = kStatus_Success;
    uint32_t sum = 0;

    uint32_t wordNum = SWT_RxPSFPGetSGCLTableRemainWordNum(&g_swt_handle);
    /* Add Stream Gate Control List Table entry */
    netc_sgcl_gate_entry_t *gate = SDK_Malloc(((gates) ? num_gates : NUM_GATE_LIST_ENTRIES) * sizeof(netc_sgcl_gate_entry_t), 4U);

    memset(sgclEntryCfg, 0U, sizeof(netc_tb_sgcl_gcl_t));
    sgclEntryCfg->gcList     = gate;
    sgclEntryCfg->entryID    = *sgclEID;
    /* when not running in console mode, use hardcoded values*/
    if (!gates) {
      sgclEntryCfg->cycleTime  = 300000 * NUM_GATE_LIST_ENTRIES;//NUM_GATE_LIST_ENTRIES * (FRAME_SIZE / 2) * 8U + 1000U;
      sgclEntryCfg->numEntries = NUM_GATE_LIST_ENTRIES;
      for (uint32_t i = 0U; i < NUM_GATE_LIST_ENTRIES; i++)
      {
     
          gate[i].timeInterval = 300000;//(FRAME_SIZE / 2) * 8U;
          //gate[i].iom          = 1000U;
          //gate[i].iomen        = 0U;
          gate[i].gtst = 0;
         /// gate[i].oipv = 1;
      }
      
      
       gate[2].timeInterval = 300000;
       gate[2].gtst = 1;
    } else {
      for (uint32_t i = 0U; i < num_gates; i++)
      {
        gate[i].timeInterval = gates[i].duration;
        gate[i].gtst = gates[i].is_open;
        sum += gates[i].duration;
      }
      sgclEntryCfg->cycleTime = sum;
      sgclEntryCfg->numEntries = num_gates;
    }

    if (kStatus_Success != SWT_RxPSFPAddSGCLTableEntry(&g_swt_handle, sgclEntryCfg))
    {
        return kStatus_Fail;
    }
    uint32_t remain = SWT_RxPSFPGetSGCLTableRemainWordNum(&g_swt_handle);
    if ((wordNum == 0U) ||
        (wordNum != (remain + 1U + (((gates) ? num_gates : NUM_GATE_LIST_ENTRIES) + 1) / 2U)))
    {
        PRINTF("SWT_RxPSFPGetSGCLTableRemainWordNum failed\r\n");
        return kStatus_Fail;
    }
	
	return result;
}

static status_t APP_SWT_PSPFAddandUpdateSGITableEntry(netc_tb_sgi_config_t *sgiEntryCfg, uint32_t *sgiEID, uint32_t *sgclEID)
{
    uint32_t result =    kStatus_Success;
    uint32_t entryNum = SWT_RxPSFPGetSGITableRemainEntryNum(&g_swt_handle);
    netc_tb_sgi_sgise_t sgiState;
    netc_tb_sgcl_sgclse_t sgclState;
#if EN_1588_TIMER
	uint64_t nanosecond;
#endif

    /* Add Stream Gate Instance Table entry with NULL Gate Control List */
    memset(sgiEntryCfg, 0, sizeof(netc_tb_sgi_config_t));
    sgiEntryCfg->entryID            = *sgiEID;
    sgiEntryCfg->acfge.adminSgclEID = 0xFFFFFFFFU;
    /* Set base time to current time + 5s */
    TMR0_BASE->TMR_DEF_CNT_L;
    TMR0_BASE->TMR_DEF_CNT_H;

#if !EN_1588_TIMER
    sgiEntryCfg->acfge.adminBaseTime[0]  = TMR0_BASE->TMR_DEF_CNT_L;
    sgiEntryCfg->acfge.adminBaseTime[1]  = TMR0_BASE->TMR_DEF_CNT_H + 1U;

#else
    NETC_TimerGetCurrentTime(&g_timer_handle, &nanosecond);
    sgiEntryCfg->acfge.adminBaseTime[0]  = nanosecond & 0xFFFFFFFF;
    sgiEntryCfg->acfge.adminBaseTime[1]  = (nanosecond >> 32U) + 1U;
#endif
    sgiEntryCfg->acfge.adminCycleTimeExt = 1000U;
    sgiEntryCfg->cfge.sduType            = kNETC_MPDU;
    /* Stream Gate Instance entry initial is close */
    sgiEntryCfg->icfge.gst = 0U;
    result                = SWT_RxPSFPAddSGITableEntry(&g_swt_handle, sgiEntryCfg);
    if (kStatus_Success != result)
    {
       PRINTF("SWT_RxPSFPAddSGITableEntry failed\r\n");
        return kStatus_Fail;
    }

    if ((entryNum == 0U) || (entryNum != (SWT_RxPSFPGetSGITableRemainEntryNum(&g_swt_handle) + 1U)))
    {
      PRINTF("SWT_RxPSFPGetSGITableRemainEntryNum failed\r\n");
        return kStatus_Fail;
    }
	
	/* Update Stream Gate Instance Table entry with Gate Control List */
    sgiEntryCfg->acfge.adminSgclEID = *sgclEID;
    result                         = SWT_RxPSFPUpdateSGITableEntry(&g_swt_handle, sgiEntryCfg);
    if (kStatus_Success != result)
    {
      PRINTF("SWT_RxPSFPUpdateSGITableEntry failed\r\n");
        return kStatus_Fail;
    }
	
	   /* Check the Stream Gate instane entry status */
    if (kStatus_Success != SWT_RxPSFPGetSGIState(&g_swt_handle, *sgiEID, &sgiState))
    {
      PRINTF("SWT_RxPSFPGetSGIState failed\r\n");
        return kStatus_Fail;
    }
    if (sgiState.state != kNETC_GSUseDefUntilAdminAct)
    {
       PRINTF("Invalid state\r\n");
        return kStatus_Fail;
    }
    /* Delay 5s to wait gate control list becomes operational */
    SDK_DelayAtLeastUs(4000000, SDK_DEVICE_MAXIMUM_CPU_CLOCK_FREQUENCY);
    if (kStatus_Success != SWT_RxPSFPGetSGIState(&g_swt_handle, *sgiEID, &sgiState))
    {
       PRINTF("SWT_RxPSFPGetSGIState failed\r\n");
        return kStatus_Fail;
    }
    /* Check the Stream Gate instane entry status */
    if (sgiState.state != kNETC_GSUseOperList)
    {
        PRINTF("sgiState.state != kNETC_GSUseOperList\r\n");
        return kStatus_Fail;
    }

	return result;
}
#endif

#if ENIPF
static void  APP_SWT_enIPF(bool *enipf, swt_config_t *swt_config)
{
  for (uint32_t i = 0U; i < 5; i++) {
    if (enipf[i] == true)
      swt_config->ports[i].commonCfg.ipfCfg.enIPFTable = true;
  }
}

static status_t APP_SWT_AddIngressFilterTableEntry(netc_tb_ipf_config_t *ipfEntryCfg, uint32_t *entryID, netc_tb_ipf_forward_action_t action, uint8_t *smacAddr, uint8_t port_id)
{
    status_t result  = kStatus_Success;
    uint32_t wordNum = SWT_RxIPFGetTableRemainWordNum(&g_swt_handle);
    
    /* Build Ingress Port Filter Table entry config with source MAC addr */
    memset(ipfEntryCfg, 0U, sizeof(netc_tb_ipf_config_t));
    memcpy(&ipfEntryCfg->keye.smac[0], &smacAddr[0], sizeof(ipfEntryCfg->keye.smac));
    memset(&ipfEntryCfg->keye.smacMask[0], 0xFF, 6);
    ipfEntryCfg->cfge.fltfa = action;
    if (action == kNETC_IPFRedirectToMgmtPort) {
      ipfEntryCfg->cfge.hr = kNETC_SoftwareDefHR0;
    }
    ipfEntryCfg->keye.srcPort = port_id;
    ipfEntryCfg->keye.srcPortMask = port_id;

    result = SWT_RxIPFAddTableEntry(&g_swt_handle, ipfEntryCfg, entryID);
    
    if ((kStatus_Success != result) && (*entryID != 0xFFFFFFFF))
    {
        return kStatus_Fail;
    }

    if ((wordNum == 0U) || (wordNum != (SWT_RxIPFGetTableRemainWordNum(&g_swt_handle) + 2U)))
    {
        return kStatus_Fail;
    }
#if 0       
    /* Update entry Forwarding Action to Permit */
    ipfEntryCfg->cfge.fltfa = kNETC_IPFForwardPermit;
    result                 = SWT_RxIPFUpdateTableEntry(&g_swt_handle, *entryID, &ipfEntryCfg->cfge);
    if (kStatus_Success != result)
    {
      return kStatus_Fail;
    }
#endif
          
        return result;
}
#endif

static status_t APP_SWT_AddVlanFilterEntry(netc_tb_vf_config_t *vfEntryCfg, uint32_t *vfEntryID, uint32_t vid, uint8_t mfo, uint8_t pbitmap)
{
    status_t result   = kStatus_Success;
    uint32_t entryNum = SWT_BridgeGetVFTableRemainEntryNum(&g_swt_handle);

    /* Add VLAN Filter Table entry */
    memset(vfEntryCfg, 0U, sizeof(netc_tb_vf_config_t));
    vfEntryCfg->keye.vid            = vid;
    vfEntryCfg->cfge.portMembership =  pbitmap;
    vfEntryCfg->cfge.mfo            = mfo;
    vfEntryCfg->cfge.mlo            = kNETC_DisableMACLearn;
    /* for now we are using a default FID. It's the same as the one set for FDB*/
    vfEntryCfg->cfge.fid            = VLAN_FILTER_ID;
    result                        = SWT_BridgeAddVFTableEntry(&g_swt_handle, vfEntryCfg, vfEntryID);
    if ((kStatus_Success != result))
    {
        return kStatus_Fail;
    }

    if ((entryNum == 0U) || (entryNum != (SWT_BridgeGetVFTableRemainEntryNum(&g_swt_handle) + 1U)))
    {
        return kStatus_Fail;
    }
    
    return result;
}

static status_t APP_SWT_AddFDBEntry(netc_tb_fdb_config_t *fdbEntryCfg, uint32_t *fdbEntryID, uint8_t *macAddr, uint8_t *port, uint32_t *etEID)
{
	status_t result   = kStatus_Success;
    /* Add FDB Table entry */
    memset(fdbEntryCfg, 0U, sizeof(netc_tb_fdb_config_t));
    
    /* for now we are using a default FID*/
    fdbEntryCfg->keye.fid = VLAN_FILTER_ID;

    memcpy(&fdbEntryCfg->keye.macAddr[0], &macAddr[0], 6U);
    fdbEntryCfg->cfge.portBitmap |= port[0];
    fdbEntryCfg->cfge.dynamic    = 1U;
	
#if ENETTABLEANDSEQREC	
	fdbEntryCfg->cfge.etEID      = *etEID;
    fdbEntryCfg->cfge.oETEID     = kNETC_MulitPortPackedETAccess;
#endif	
    result                      = SWT_BridgeAddFDBTableEntry(&g_swt_handle, fdbEntryCfg, fdbEntryID);
    if ((kStatus_Success != result))
    {
        return kStatus_Fail;
    }
    
    return result;

}


#if ENQBV
#define NUM_TIME_GATE_LIST_ENTRIES (2U)
#define EXAMPLE_TIME_LIST_ENTRYS 2
static status_t  APP_SWT_ConfigQBVGCL(netc_tb_tgs_gcl_t *tgclEntryCfg, netc_tb_tgs_entry_id_t *port_map, TasScheduleEntry *entries, uint32_t num_entries, uint64_t base_time)
{
	status_t result = kStatus_Success;
	netc_tb_tgs_gcl_t wTgsList;
        uint32_t cycle = 0;
#if EN_1588_TIMER
	uint64_t nanosecond = 0, delta, n;
#endif
        netc_tgs_gate_entry_t *gate = SDK_Malloc(((entries) ? num_entries: NUM_TIME_GATE_LIST_ENTRIES) * sizeof(netc_tgs_gate_entry_t), 4U);

    memset(tgclEntryCfg, 0U, sizeof(*tgclEntryCfg));
    tgclEntryCfg->entryID = port_map[0];
     /* keep this branch for non-console mode*/
    if (!entries) {

      tgclEntryCfg->cycleTime  = 600000U;
      tgclEntryCfg->extTime    = 0U;
      tgclEntryCfg->numEntries = NUM_TIME_GATE_LIST_ENTRIES;
      tgclEntryCfg->gcList     = gate;
      for (uint32_t i = 0U; i < NUM_TIME_GATE_LIST_ENTRIES; i++)
      {
          gate[i].interval    =300000U;
          gate[i].tcGateState = 0x00;
          gate[i].operType    = kNETC_SetGateStates;
      }
          gate[1].interval    =300000U;
          gate[1].tcGateState = 0x4U;
          gate[1].operType    = kNETC_SetGateStates;
#if !EN_1588_TIMER
      /* Set base time to current time + 1s */
      TMR0_BASE->TMR_DEF_CNT_L;
      TMR0_BASE->TMR_DEF_CNT_H;
      tgclEntryCfg->baseTime   = (((uint64_t)TMR0_BASE->TMR_DEF_CNT_H << 32U) | TMR0_BASE->TMR_DEF_CNT_L) + 1000000000ULL;
#else
      NETC_TimerGetCurrentTime(&g_timer_handle, &nanosecond);
      tgclEntryCfg->baseTime   = nanosecond + 3000000000;
#endif
    } else {
      tgclEntryCfg->extTime    = 0U;
      tgclEntryCfg->numEntries = num_entries;
      tgclEntryCfg->gcList     = gate;
      for (uint32_t i = 0U; i < num_entries; i++)
      {
          gate[i].interval    = entries[i].duration_ns;
          gate[i].tcGateState = entries[i].gate_mask;
          gate[i].operType    = entries[i].state;
          //gate[i].gate = entries[i].gate_mask;
          //gate[i].operType = kNETC_SetGateStates;
          cycle += entries[i].duration_ns;
      }
      tgclEntryCfg->cycleTime  = cycle;
#if !EN_1588_TIMER
      /* Set base time to current time + 1s */
      TMR0_BASE->TMR_DEF_CNT_L;
      TMR0_BASE->TMR_DEF_CNT_H;
      tgclEntryCfg->baseTime   = (((uint64_t)TMR0_BASE->TMR_DEF_CNT_H << 32U) | TMR0_BASE->TMR_DEF_CNT_L) + 1000000000ULL;
 #else
      NETC_TimerGetCurrentTime(&g_timer_handle, &nanosecond);
      PRINTF("\r\ncurrent time %llu\r\n", nanosecond);
      delta = nanosecond - base_time;
      n = (delta + cycle - 1) / cycle;
      tgclEntryCfg->baseTime   = base_time + n * cycle;
       PRINTF("\r\n adjusted base time %llu\r\n",   tgclEntryCfg->baseTime);
 #endif
    }
    result = SWT_TxTGSConfigAdminGcl(&g_swt_handle, tgclEntryCfg);

    if (kStatus_Success == result)
    {
        /* Wait unitl time gate control list becomes operational */
        while (0U != (SWT_GetPortTGSListStatus(&g_swt_handle, port_map[0]) & kNETC_AdminListPending))
        {
        }
        netc_tb_tgs_gcl_t rTgsList;
        netc_tgs_gate_entry_t *rGate = SDK_Malloc(((entries) ? num_entries: NUM_TIME_GATE_LIST_ENTRIES) * sizeof(netc_tgs_gate_entry_t), 4U);
        memset(&rTgsList, 0U, sizeof(rTgsList));
        rTgsList.entryID = port_map[0];
        rTgsList.gcList  = rGate;
        result           = SWT_TxtTGSGetOperGcl(&g_swt_handle, &rTgsList, ((entries) ? num_entries: NUM_TIME_GATE_LIST_ENTRIES));
        PRINTF("\r\n gate 0 %lu gate 1 %lu\r\n", rGate[0].interval,rGate[1].interval);
        SDK_Free(rGate);
    }

    SDK_Free(gate);
	
    return result;
	
}

#endif

#if ENBP
static status_t APP_SWT_SharedBPUpdate(uint32_t *s_bpid)
{
    status_t result = kStatus_Success;

    netc_tb_sbp_config_t sbpEntryCfg = {0};
	/* Add Shared Buffer pool Table entry */
    sbpEntryCfg.entryID = s_bpid[0];
    sbpEntryCfg.cfge.fcOnThresh = 0xb43;
    sbpEntryCfg.cfge.fcOffThresh = 0x3c3;
    if (kStatus_Success != SWT_UpdateSBPTableEntry(&g_swt_handle, &sbpEntryCfg))
    {
        return kStatus_Fail;
    }

	return result;
}

static status_t APP_SWT_BPUpdate(uint32_t *bpid)
{
    status_t result = kStatus_Success;

    netc_tb_bp_config_t bpEntryCfg   = {0};

	/* Check Buffer pool entry number before add operation */
    if (SWT_GetBPTableEntryNum(&g_swt_handle) == 0U)
    {
        return kStatus_Fail;
    }

    bpEntryCfg.entryID = bpid[0];
    bpEntryCfg.cfge.fcOnThresh = 0xb43;
    bpEntryCfg.cfge.fcOffThresh = 0x3c3;
    bpEntryCfg.cfge.gcCfg = kNETC_FlowCtrlWithBP;
    bpEntryCfg.cfge.fcPorts = 7;
   // bpEntryCfg.cfge.sbpEn = true;
   // bpEntryCfg.cfge.sbpEid    =  (bpid[0] <= 3) ? 0: 1;
 

    if (kStatus_Success != SWT_UpdateBPTableEntry(&g_swt_handle, &bpEntryCfg))
    {
            return kStatus_Fail;
    }

    return result;
}
#endif
    
#if !(defined(FSL_FEATURE_NETC_HAS_NO_SWITCH) && FSL_FEATURE_NETC_HAS_NO_SWITCH)
status_t APP_SWT_XferLoopBack(void)
{
    status_t result                  = kStatus_Success;
#if (NETC_VSI_NUM_USED > 0)
    uint32_t msg_recv_flags = kNETC_PsiRxMsgFromVsi1Flag;
#endif
    netc_rx_bdr_config_t rxBdrConfig = {0};
    netc_tx_bdr_config_t txBdrConfig = {0};
    netc_bdr_config_t bdrConfig      = {.rxBdrConfig = &rxBdrConfig, .txBdrConfig = &txBdrConfig};
#if defined(FSL_FEATURE_NETC_HAS_SWITCH_TAG) && FSL_FEATURE_NETC_HAS_SWITCH_TAG
    uint32_t dataOffset              = 12U + sizeof(netc_swt_tag_port_no_ts_t);
#else
    swt_mgmt_tx_arg_t txArg          = {0};
#endif
    netc_buffer_struct_t txBuff      = {.buffer = &g_txFrame, .length = sizeof(g_txFrame)};
    netc_frame_struct_t txFrame      = {.buffArray = &txBuff, .length = 1};
    bool link                        = false;
    
    netc_msix_entry_t msixEntry[3];
    netc_hw_mii_mode_t phyMode;
    netc_hw_mii_speed_t phySpeed;
    netc_hw_mii_duplex_t phyDuplex;
    ep_config_t g_ep_config;
   // uint32_t entryID;
    uint32_t msgAddr;
    uint32_t length;
    uint32_t i;
    char *taskID = "A";

    PRINTF("\r\nNETC Switch frame loopback example start.\r\n");

    /* MSIX and interrupt configuration. */
    MSGINTR_Init(EXAMPLE_MSGINTR, &msgintrCallback);
    msgAddr              = MSGINTR_GetIntrSelectAddr(EXAMPLE_MSGINTR, 0);
    msixEntry[0].control = kNETC_MsixIntrMaskBit;
    msixEntry[0].msgAddr = msgAddr;
    msixEntry[0].msgData = EXAMPLE_TX_INTR_MSG_DATA;
    msixEntry[1].control = kNETC_MsixIntrMaskBit;
    msixEntry[1].msgAddr = msgAddr;
    msixEntry[1].msgData = EXAMPLE_RX_INTR_MSG_DATA;
#if (NETC_VSI_NUM_USED > 0)
    msixEntry[2].control = kNETC_MsixIntrMaskBit;
    msixEntry[2].msgAddr = msgAddr;
    msixEntry[2].msgData = SI_COM_INTR_MSG_DATA;
#endif
    bdrConfig.rxBdrConfig[0].bdArray       = &g_rxBuffDescrip[0];
    bdrConfig.rxBdrConfig[0].len           = EXAMPLE_EP_RXBD_NUM;
    bdrConfig.rxBdrConfig[0].extendDescEn  = false;
    bdrConfig.rxBdrConfig[0].buffAddrArray = &rxBuffAddrArray[0];
    bdrConfig.rxBdrConfig[0].buffSize      = EXAMPLE_EP_RXBUFF_SIZE_ALIGN;
    bdrConfig.rxBdrConfig[0].msixEntryIdx  = EXAMPLE_RX_MSIX_ENTRY_IDX;
    bdrConfig.rxBdrConfig[0].enThresIntr   = true;
    bdrConfig.rxBdrConfig[0].enCoalIntr    = true;
    bdrConfig.rxBdrConfig[0].intrThreshold = 1;

    (void)EP_GetDefaultConfig(&g_ep_config);
    g_ep_config.si                 = EXAMPLE_SWT_SI;
    g_ep_config.siConfig.txRingUse = 1;
    g_ep_config.siConfig.rxRingUse = 1;
    g_ep_config.reclaimCallback    = APP_ReclaimCallback;
    g_ep_config.msixEntry          = &msixEntry[0];
#if (NETC_VSI_NUM_USED > 0)
    g_ep_config.entryNum           = 3;
#else
    g_ep_config.entryNum           = 2;
#endif    
#if (NETC_VSI_NUM_USED > 0)
    g_ep_config.siComEntryIdx = SI_COM_MSIX_ENTRY_IDX;
#endif
#ifdef EXAMPLE_ENABLE_CACHE_MAINTAIN
    g_ep_config.rxCacheMaintain = true;
    g_ep_config.txCacheMaintain = true;
#endif
    NVIC_SetPriority(NETC_MSGINTR_IRQ, NETC_MSGINTR_PRIORITY);
    
    result = EP_Init(&g_ep_handle, &g_macAddr[0], &g_ep_config, &bdrConfig);
    if (result != kStatus_Success)
    {
        return result;
    }

    g_ep_handle.getLinkStatus = EP_getlinkstatus;
    g_ep_handle.getLinkSpeed  = EP_getlinkspeed;
    
    SWT_GetDefaultConfig(&g_swt_config);

    /* Wait PHY link up. */
    PRINTF("Wait for PHY link up...\r\n");
    for (i = 0; i < EXAMPLE_SWT_MAX_PORT_NUM; i++)
    {
        if (i == EXAMPLE_SWT_PSEUDO_PORT)
          goto skip_phy;

        /* Only check the enabled port. */
        if (((1U << i) & EXAMPLE_SWT_USED_PORT_BITMAP) == 0U)
        {
            continue;
        }

        do
        {
            result = APP_PHY_GetLinkStatus(EXAMPLE_SWT_PORT0 + i, &link);
        } while ((result != kStatus_Success) || (!link));
        result = APP_PHY_GetLinkModeSpeedDuplex(EXAMPLE_SWT_PORT0 + i, &phyMode, &phySpeed, &phyDuplex);
        if (result != kStatus_Success)
        {
            PRINTF("\r\n%s: %d, Failed to get link status(mode, speed, dumplex)!\r\n", __func__, __LINE__);
            return result;
        }

skip_phy:
        g_swt_config.ports[i].ethMac.miiMode   = phyMode;
        g_swt_config.ports[i].ethMac.miiSpeed  = phySpeed;
        g_swt_config.ports[i].ethMac.miiDuplex = phyDuplex;
        
#if ENIPF
    APP_SWT_enIPF(enipf, &g_swt_config);
#endif
    
#if ENISI  
        /* Enable switch ports do first stream identification with key construction rule 0 */
        g_swt_config.ports[i].isiCfg.enKC0 = true;
        g_swt_config.ports[i].isiCfg.defaultISEID = defaultisEID;
#endif

#if ENVLANFWD
        g_swt_config.ports[i].bridgeCfg.isRxVlanAware = true;
        g_swt_config.ports[i].bridgeCfg.txVlanAction = kNETC_TxDelOuterVlan;
        g_swt_config.ports[i].bridgeCfg.defaultVlan.vid = 0x01;
        g_swt_config.ports[i].bridgeCfg.enMacStationMove = true;
#endif
        
#if ENVLANQOSMAP
        /* Set port i use outer VLAN info to determine IPV with VLAN to QoS mapping profile 0 */
        g_swt_config.ports[i].commonCfg.qosMode.enVlanInfo    = true;
        g_swt_config.ports[i].commonCfg.qosMode.vlanTagSelect = true;
        g_swt_config.ports[i].commonCfg.qosMode.vlanQosMap    = 0;
        g_swt_config.ports[i].ethMac.rxMaxFrameSize       = 9600;
        /* port speed is 100Mbps = (9 + 1) *10Mbps for ports 0 and 1 and (99 + 1) * 10mbps for 2*/
        if (i != 2)
          g_swt_config.ports[i].commonCfg.pSpeed = 9;
        else
          g_swt_config.ports[i].commonCfg.pSpeed = 99;
        
       // if (i != 2)
         g_swt_config.ports[i].ethMac.preemptMode   = kNETC_PreemptOn64B;
        
        for (int prio = 0; prio < 8; prio++) {
          g_swt_config.ports[i].ipvToTC[prio] = prio;
          
       

#if ENBP          
          g_swt_config.ports[i].ipvToBP[prio] = prio;
#endif
        }
#endif

#if ENQBV
	/* Enable time gatting for switch port i*/
       // g_swt_config.ports[i].enableTg     = true;
#endif
    }
    for (int prio = 0; prio < 8; prio++) {
     uint32_t idx = (prio << 1) | 0x00;
     /* configure internal prio value to PCP /DEI index Mapping */
     g_swt_config.rxqosCfg.profiles[0].ipv[idx] = prio;
    }
#if ENISI
    /* Config key construction rule 0 to make ource MAC address and outer VLAN ID present */
    g_swt_config.psfpCfg.kcRule[0].ovidp = true;
    g_swt_config.psfpCfg.kcRule[0].smacp = true;
    g_swt_config.psfpCfg.kcRule[0].valid = true;

#endif
    //g_swt_config.bridgeCfg.dVFCfg.portMembership = 0x7U;
    /*in curent case if classified VLAN is nit found in Vlan fiter table, the frame is discarded.
     Changing the option below will modify the begavior. FID will be used to search the FDB. If FID is found in the
    FDB then MFO is applied. If FID is not found frame is discarded. */
   // g_swt_config.bridgeCfg.dVFCfg.enUseFilterID = true;
    g_swt_config.bridgeCfg.dVFCfg.filterID = EXAMPLE_FRAME_FID;
 #if ENVLANFWD
    g_swt_config.bridgeCfg.dVFCfg.mfo = kNETC_FDBLookUpWithDiscard;
#else
    g_swt_config.bridgeCfg.dVFCfg.mfo = kNETC_FDBLookUpWithFlood;
#endif
    g_swt_config.bridgeCfg.dVFCfg.mlo = kNETC_DisableMACLearn;

    g_swt_config.cmdRingUse            = 1U;
    g_swt_config.cmdBdrCfg[0].bdBase   = &g_cmdBuffDescrip[0];
    g_swt_config.cmdBdrCfg[0].bdLength = 8U;

    result = SWT_Init(&g_swt_handle, &g_swt_config);
    if (result != kStatus_Success)
    {
        PRINTF("\r\n%s: %d, Failed to initialize switch!\r\n", __func__, __LINE__);
        return result;
    }

#if ENBP
//    for (uint32_t s_pool_id = 0U; s_pool_id < 2; s_pool_id++) {
//      result = APP_SWT_SharedBPUpdate(&s_pool_id);
//      if (result != kStatus_Success)
//      {
//                      PRINTF("Could not update SBPOOL r\n");
//                      return result;
//      }
//    }

    for (uint32_t pool_id = 0U; pool_id < 7; pool_id++) {
      result = APP_SWT_BPUpdate(&pool_id);
      if (result != kStatus_Success)
      {
        PRINTF("Could not update BPOOL r\n");
        return result;
      }
    }
#endif

#if ENISI
    /* add default iSEID to permit all the streams that do not match ISI */
    result = APP_SWT_PSPFAddISTableEntry(&isEntryCfg, &defaultisEID, &defaultisEID, kNETC_ISBridgeForward, sgiEID[0], isqEID[0]);
    if (result != kStatus_Success)
    {
            PRINTF("Could not add ingress stream  table default entry\r\n");
            return result;
    }
#endif
    /* Configure switch transfer resource. */
    swtTxRxConfig.enUseMgmtRxBdRing            = false;
    swtTxRxConfig.enUseMgmtTxBdRing            = true;
    swtTxRxConfig.mgmtTxBdrConfig.bdArray      = &g_mgmtTxBuffDescrip[0];
    swtTxRxConfig.mgmtTxBdrConfig.len          = EXAMPLE_EP_TXBD_NUM;
    swtTxRxConfig.mgmtTxBdrConfig.dirtyArray   = &g_mgmtTxDirty[0];
    swtTxRxConfig.mgmtTxBdrConfig.msixEntryIdx = EXAMPLE_TX_MSIX_ENTRY_IDX;
    swtTxRxConfig.mgmtTxBdrConfig.enIntr       = true;
    swtTxRxConfig.reclaimCallback              = APP_SwtReclaimCallback;
#ifdef EXAMPLE_ENABLE_CACHE_MAINTAIN
    swtTxRxConfig.rxCacheMaintain = true;
    swtTxRxConfig.txCacheMaintain = true;
#endif
    result = SWT_ManagementTxRxConfig(&g_swt_handle, &g_ep_handle, &swtTxRxConfig);
    if (kStatus_Success != result)
    {
        PRINTF("\r\n%s: %d, Failed to config TxRx!\r\n", __func__, __LINE__);
        return result;
    }

    netc_si_l2mf_config_t l2mfConfig = {
        .macUCPromis = false,
        .macMCPromis = false,

        .rejectUC = false,
        .rejectMC = false,
        .rejectBC = false
    };

    EP_RxL2MFInit(&g_ep_handle, &l2mfConfig);
    /* Unmask MSIX message interrupt. */
    EP_MsixSetEntryMask(&g_ep_handle, EXAMPLE_TX_MSIX_ENTRY_IDX, false);
    EP_MsixSetEntryMask(&g_ep_handle, EXAMPLE_RX_MSIX_ENTRY_IDX, false);
#if (NETC_VSI_NUM_USED > 0)
    EP_MsixSetEntryMask(&g_ep_handle, SI_COM_MSIX_ENTRY_IDX, false);

    EP_PsiClearStatus(&g_ep_handle, msg_recv_flags);
    EP_PsiEnableInterrupt(&g_ep_handle, msg_recv_flags, true);

    EP_PsiSetRxBuffer(&g_ep_handle, kNETC_Vsi1, (uintptr_t)&psiMsgBuff1[0]);
     /* Create VSI-PSI messaging thread */
    if (xTaskCreate(netc_si_msg_thread, "netc_si_msg_thread", SI_MSG_THREAD_STACKSIZE, NULL, SI_MSG_THREAD_PRIO, NULL) != pdPASS)
    {
        PRINTF("\r\nFailed to create netc_si_msg_thread\r\n");
        while (1)
        {
        }
    }
#endif

#if !(defined(FSL_FEATURE_NETC_HAS_SWITCH_TAG) && FSL_FEATURE_NETC_HAS_SWITCH_TAG)
    APP_BuildBroadCastFrame();
#endif
    rxFrameNum = 0;
    txFrameNum = 0;
#if EN_1588_TIMER
    netc_timer_config_t timerConfig = {0};
    uint64_t timestamp;
    uint64_t second;
    uint32_t nanosecond;
    uint8_t count;
    
    timerConfig.clockSelect = kNETC_TimerSystemClk;
    timerConfig.refClkHz    =  CCM_RootGetRate(CLOCK_ROOT_ENET) / 2;
    timerConfig.enableTimer = true;
    timerConfig.defaultPpb  = 0;
    result                  = NETC_TimerInit(&g_timer_handle, &timerConfig);
    if (result != kStatus_Success)
    {
        return result;
    }
#if 0
        PRINTF("\r\n Check if the timestamp is running.\r\n");
    for (count = 1U; count <= 10U; count++)
    {
        NETC_TimerGetCurrentTime(&g_timer_handle, &timestamp);
        second     = timestamp / 1000000000U;
        nanosecond = timestamp % 1000000000U;

        PRINTF(" Get the %u-th time", count);
        PRINTF(" %llu second,", second);
        PRINTF(" %u nanosecond\r\n", nanosecond);
        SDK_DelayAtLeastUs(200000, SDK_DEVICE_MAXIMUM_CPU_CLOCK_FREQUENCY);
    }
#endif
#endif

    APP_SRTM_NETC_VirtualizePCIConfig();
#if 1
    if (xTaskCreate(console, "Console", 4200, NULL, tskIDLE_PRIORITY + 1U, NULL) != pdPASS)
    {
        PRINTF("\r\nFailed to create Console task\r\n");
        while (1)
        {
        }
    }
#endif
    /* Start FreeRTOS scheduler. */
    vTaskStartScheduler();

#if 1
    /* Set FDB table, input frame only forwards to pseudo MAC port. */
/*    netc_tb_fdb_config_t fdbEntryCfg = {.keye.fid = EXAMPLE_FRAME_FID, .cfge.portBitmap = (1U << EXAMPLE_SWT_PSEUDO_PORT) | 0x1 | 0x2, .cfge.dynamic = 1};
    memset(&fdbEntryCfg.keye.macAddr[0], 0xFF, 6U);
    result = SWT_BridgeAddFDBTableEntry(&g_swt_handle, &fdbEntryCfg, &entryID);
    if ((kStatus_Success != result) || (0xFFFFFFFFU == entryID))
    {
        PRINTF("\r\n%s: %d, Failed to add FDB table!, result = %d, entryID = %d\r\n", __func__, __LINE__, result, entryID);
        return kStatus_Fail;
    }
 */
    
PRINTF("\r\nWhen running in static mode, please set the interface MAC to the MAC address from smacAddr[i] otherwise the flows will not match!!");
#if ENIPF
    for (uint32_t i = 0U; i < EXAMPLE_SWT_MAX_PORT_NUM; i++) {
       if (enipf[i] == true) {
              result = APP_SWT_AddIngressFilterTableEntry(&ipfEntryCfg, &entryID[i], kNETC_IPFForwardPermit, smacAddr[i], i);
              if (result != kStatus_Success)
              {
                PRINTF("Could not add ingress port filter entry\r\n");
                return result;
              }
       }
    }
#endif
    
#if ENPOLICER
    result = APP_SWT_RxPSFPAddRPTableEntry(&policerEntry, &rpEID, rate_params);
    if (result != kStatus_Success)
     {
      PRINTF("Could not add rate policer\r\n");
      return result;
     }
#endif

#if ENISI
	for (uint32_t i = 0U; i < EXAMPLE_SWT_MAX_PORT_NUM; i++) {
		/* add entry in stream identifcation table with smac corresponding to port i and  iSEID corresponding to port i. VID is hardcoded to 100.*/
		result = APP_SWT_PSPFAddISIEntry(&isiEntryCfg, &isiEntryID, iSEID[i], smacAddr[i], 100);
		if (result != kStatus_Success)
		{
			PRINTF("Could not add ingress stream identification entry\r\n");
			return result;
		}
		
		/* add stream counter table entries for port i. Two entries are added for port i: one entry for stream table and one for stream filter table*/
		result = APP_SWT_PSPFAddISCTableEntry(entries_ids[i]);
		if (result != kStatus_Success)
		{
			PRINTF("Could not add ingress stream counter table entries\r\n");
			return result;
		}

		/* add entry in stream table for ingress stream entry id corresponding to port i. Counter table entry[i][0] will be used */
		result = APP_SWT_PSPFAddISTableEntry(&isEntryCfg, iSEID[i], entries_ids[i], kNETC_ISBridgeForward, sgiEID[i], isqEID[i]);
		if (result != kStatus_Success)
		{
			PRINTF("Could not add ingress stream  table entries\r\n");
			return result;
		}
                

		/* add entry in stream filter table for ingress stream entry id corresponding to port i. Counter table entry[i][1] will be used.  */
		result = APP_SWT_PSPFAddISFTableEntry(&isfEntryCfg, iSEID[i], entries_ids[i], &isfEntryID, pcp[i], &rpEID, sgiEID[i]);
		if (result != kStatus_Success)
		{
			PRINTF("Could not add ingress stream  table entries\r\n");
			return result;
		}
	}
	
#endif

#if ENVLANFWD
#define VID (100U)
    

	result = APP_SWT_AddVlanFilterEntry(&vfEntryCfg, &vfEntryID, VID, kNETC_FDBLookUpWithDiscard/*kNETC_NoFDBLookUp*/, 0x3);
	if (result != kStatus_Success)
	{
            PRINTF("Could not add VLAN / FDB entry\r\n");
            return result;
	}

	for (uint32_t i = 0U; i < EXAMPLE_SWT_MAX_PORT_NUM; i++) {	
          result = APP_SWT_AddFDBEntry(&fdbEntryCfg, &fdbEntryID, smacAddr[i], port_bitmap[i], etEID[i]);
          if (result != kStatus_Success)
          {
              PRINTF("Could not add VLAN / FDB entry\r\n");
            return result;
          }
	}




#endif
    
        
 /* sanity check for ingress port filter */
#if ENIPF
        for(int i=0; i< EXAMPLE_SWT_MAX_PORT_NUM; i++) {
          /* Check Ingress Port Filter entry match count */
          result = SWT_RxIPFGetMatchedCount(&g_swt_handle, i, &matchCount);
          if (kStatus_Success != result)
            continue;

          if ((0U != matchCount))
          {
              PRINTF("\r\nFor port %d port filter match count is %lx\r\n",i,matchCount);
  //            result = SWT_RxIPFResetMatchCounter(&g_swt_handle, entryID);
  //            if (kStatus_Success != result)
  //            {
  //                    return kStatus_Fail;
  //            }
  //            result = SWT_RxIPFGetMatchedCount(&g_swt_handle, entryID, &matchCount);
  //            if ((0U != matchCount) || (kStatus_Success != result))
  //            {
  //                    return kStatus_Fail;
  //            }
          }
        }
#endif
       
#if ENISI
         for(int i=0; i < EXAMPLE_SWT_MAX_PORT_NUM; i++) {
          netc_tb_isc_stse_t isCount[2] = {0};
           if ((kStatus_Success != SWT_RxPSFPGetISCStatistic(&g_swt_handle, entries_ids[i][0], &isCount[0])) ||
                (kStatus_Success != SWT_RxPSFPGetISCStatistic(&g_swt_handle, entries_ids[i][1], &isCount[1])))
            {
              return kStatus_Fail;
            } else if ((isCount[0].rxCount) || (isCount[1].rxCount) ){
                PRINTF("\r\ns Ingress stream table stream match count %lu!\r\n", isCount[0].rxCount);
                PRINTF("\r\ns Ingress stream filter table stream match count %lu!\r\n", isCount[1].rxCount);
            }
          }     
#endif
        
#if ENPOLICER
    /* Check Rate Policer entry State data */
    result = SWT_RxPSFPGetRPStatistic(&g_swt_handle, rpEID, &swtRpStatis);
    if ((result != kStatus_Success))
    {
        result = kStatus_Fail;
    } else {
      PRINTF("\r\ns Ingress rate policer stats byte count %lu!\r\n",swtRpStatis.byteCount[0]);
    }
      
#endif

#if ENSGCL


#if !EN_1588_TIMER
	/* Reset default ns timer function */
    NETC_F0_PCI_HDR_TYPE0->PCI_CFC_PCIE_DEV_CTL |= ENETC_PCI_TYPE0_PCI_CFC_PCIE_DEV_CTL_INIT_FLR_MASK;
    while ((NETC_F0_PCI_HDR_TYPE0->PCI_CFC_PCIE_DEV_CTL & ENETC_PCI_TYPE0_PCI_CFC_PCIE_DEV_CTL_INIT_FLR_MASK) != 0U)
    {
    }

	/* Enable master bus and memory access for default ns timer*/
    NETC_F0_PCI_HDR_TYPE0->PCI_CFH_CMD |=
        (ENETC_PCI_TYPE0_PCI_CFH_CMD_MEM_ACCESS_MASK | ENETC_PCI_TYPE0_PCI_CFH_CMD_BUS_MASTER_EN_MASK);

#endif
for (uint32_t i = 0U; i < EXAMPLE_SWT_MAX_PORT_NUM; i++) {
        result = APP_SWT_PSPFAddSGCLTableEntry(&sgclEntryCfg, sgclEID[i],  NULL, 0);
        if (result != kStatus_Success)
        {
                        PRINTF("Could not add ingress stream  gate control list entry\r\n");
                        return result;
        }

        result = APP_SWT_PSPFAddandUpdateSGITableEntry(&sgiEntryCfg, sgiEID[i], sgclEID[i]);
        if (result != kStatus_Success)
        {
                        PRINTF("Could not add /update stream gate instance table entry\r\n");
                        return result;
        }
}
    
    
#endif   
#endif

#if ENQBV
	
	for (uint32_t i = 0U; i < EXAMPLE_SWT_MAX_PORT_NUM; i++) {
          if (i == 0)
             continue;
		result = APP_SWT_ConfigQBVGCL(&tgclEntryCfg, port_map[i], NULL, 0, 0);
		if (result != kStatus_Success)
		{
				PRINTF("Could not configure QBV GCL r\n");
				return result;
		}
	}
#endif
    i = 0;
    while (txFrameNum < EXAMPLE_EP_TXFRAME_NUM)
    {
        /* Only use the enabled port. */
        if (((1U << i) & EXAMPLE_SWT_USED_PORT_BITMAP) == 0U)
        {
            i++;
            i = ((i >= EXAMPLE_SWT_MAX_PORT_NUM) ? 0 : i);
            continue;
        }

#if defined(FSL_FEATURE_NETC_HAS_SWITCH_TAG) && FSL_FEATURE_NETC_HAS_SWITCH_TAG
        APP_BuildBroadCastFrameSwtTag(i);
#endif

       // txOver     = false;
#if defined(FSL_FEATURE_NETC_HAS_SWITCH_TAG) && FSL_FEATURE_NETC_HAS_SWITCH_TAG
       // result     = SWT_SendFrame(&g_swt_handle, &txFrame, NULL, NULL);
#else
        txArg.ring = 0;
        result     = SWT_SendFrame(&g_swt_handle, txArg, (netc_hw_port_idx_t)(kNETC_SWITCH0Port0 + i), false, &txFrame, NULL, NULL);
#endif
        if (result != kStatus_Success)
        {
            PRINTF("\r\nTransmit frame failed!\r\n");
            return result;
        }
     //   while (!txOver)
      //  {
       // }
#if defined(FSL_FEATURE_NETC_HAS_SWITCH_TAG) && FSL_FEATURE_NETC_HAS_SWITCH_TAG
        SWT_ReclaimTxDescriptor(&g_swt_handle, 0);
#else
        SWT_ReclaimTxDescriptor(&g_swt_handle, false, 0);
#endif
        if (mgmtTxFrameInfo.status != kNETC_EPTxSuccess)
        {
            PRINTF("\r\nTransmit frame has error!\r\n");
            return kStatus_Fail;
        }
        txFrameNum++;
        PRINTF("The %u frame transmitted success on port %u!\r\n", txFrameNum, (uint8_t)(kNETC_SWITCH0Port0 + i));

        do
        {
            result = EP_GetRxFrameSize(&g_ep_handle, 0, &length);
        } while (result == kStatus_NETC_RxFrameEmpty);
        if (result != kStatus_Success)
        {
            return result;
        }

        result = EP_ReceiveFrameCopy(&g_ep_handle, 0, g_rxFrame, length, NULL);
        if (result != kStatus_Success)
        {
            return result;
        }
        rxFrameNum++;
        PRINTF(" A frame received. The length is %d ", length);
        PRINTF(" Dest Address %02x:%02x:%02x:%02x:%02x:%02x Src Address %02x:%02x:%02x:%02x:%02x:%02x \r\n",
               g_rxFrame[0], g_rxFrame[1], g_rxFrame[2], g_rxFrame[3], g_rxFrame[4], g_rxFrame[5], g_rxFrame[6],
               g_rxFrame[7], g_rxFrame[8], g_rxFrame[9], g_rxFrame[10], g_rxFrame[11]);

#if defined(FSL_FEATURE_NETC_HAS_SWITCH_TAG) && FSL_FEATURE_NETC_HAS_SWITCH_TAG
        if (memcmp(&g_rxFrame[dataOffset], &g_txFrame[dataOffset], sizeof(g_txFrame) - dataOffset))
#else
        if (memcmp(g_rxFrame, g_txFrame, sizeof(g_txFrame)))
#endif
        {
            PRINTF("\r\nTx/Rx frames don't match!\r\n");
            return kStatus_Fail;
        }

        i++;
        i = ((i >= EXAMPLE_SWT_MAX_PORT_NUM) ? 0 : i);
    }

    return result;
}
#endif


void hard_fault_handler_c(uint32_t *stack_address)
{
  uint32_t pc = stack_address[6];
  uint32_t lr = stack_address[5];
  volatile uint32_t _pc = pc;
  volatile uint32_t _lr = lr;
  
  while (1);


}

int main(void)
{
    status_t result = kStatus_Success;
    //uint32_t index;

    
    BOARD_InitHardware();

    result = APP_MDIO_Init();
    if (result != kStatus_Success)
    {
        PRINTF("\r\nMDIO Init failed!\r\n");
        return result;
    }

    result = APP_PHY_Init(NULL);
    if (result != kStatus_Success)
    {
        PRINTF("\r\nPHY Init failed!\r\n");
        return result;
    }

        for (uint8_t index = 0U; index < EXAMPLE_EP_RXBD_NUM; index++)
        {
            rxBuffAddrArray[index] = (uint64_t)(uintptr_t)&g_rxDataBuff[index];
        }

#if !(defined(FSL_FEATURE_NETC_HAS_NO_SWITCH) && FSL_FEATURE_NETC_HAS_NO_SWITCH)
    APP_SWT_XferLoopBack();
#endif

    while (1)
    {
    }
}