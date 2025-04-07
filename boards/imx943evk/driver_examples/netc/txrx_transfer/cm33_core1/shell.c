/*
 * Copyright 2019-2021, 2023 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
//#include <getopt.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "fsl_debug_console.h"
#include "fsl_shell.h"



#define SHELL_TASK_NAME         "shell"
#define __SHELL_TASK_STACK_SIZE BOARD_SHELL_TASK_STACK_SIZE
#define __SHELL_TASK_PRIORITY   4U

#define SHELL_PROMPT_LEN 20

struct shell_ctx {
    uint8_t shell_handle[SHELL_HANDLE_SIZE];
    TaskHandle_t task_handle;
    void (*init)(shell_handle_t);
};

static struct shell_ctx shell_ctx;
static char prompt[SHELL_PROMPT_LEN];


shell_handle_t shell_init(char *prompt_name)
{
    shell_handle_t shell = &shell_ctx.shell_handle[0];
    shell_status_t status;
    char *prompt_end = ">>";

    if (strlen(prompt_name) >= (SHELL_PROMPT_LEN - strlen(prompt_end)))
        goto err;

    strcpy(prompt, prompt_name);
    strcat(prompt, prompt_end);

    //status = SHELL_Init(shell, g_serialHandle, prompt);
    if (status != kStatus_SHELL_Success)
        goto err;
#if 0
    SHELL_RegisterCommand(shell, SHELL_COMMAND(write));
    SHELL_RegisterCommand(shell, SHELL_COMMAND(cat));
    SHELL_RegisterCommand(shell, SHELL_COMMAND(ls));
    SHELL_RegisterCommand(shell, SHELL_COMMAND(rm));
    SHELL_RegisterCommand(shell, SHELL_COMMAND(cd));
    SHELL_RegisterCommand(shell, SHELL_COMMAND(pwd));
    SHELL_RegisterCommand(shell, SHELL_COMMAND(mkdir));
    SHELL_RegisterCommand(shell, SHELL_COMMAND(help_config));
    SHELL_RegisterCommand(shell, SHELL_COMMAND(log));
    SHELL_RegisterCommand(shell, SHELL_COMMAND(port_stats));
#endif
    return shell;

err:
    return NULL;
}