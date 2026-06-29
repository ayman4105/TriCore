/**********************************************************************************************************************
 * Modular Fault Injection Demo
 *
 * Button fault:
 * Button pressed  -> button fault active
 * Button released -> button fault inactive
 *
 * Ethernet fault:
 * ETH_CMD_INJECT_FAULT -> ethernet fault active
 * ETH_CMD_CLEAR_FAULT  -> ethernet fault inactive
 *********************************************************************************************************************/

#include "Ifx_Types.h"              /* Include Infineon basic types */
#include "IfxCpu.h"                 /* Include CPU APIs */
#include "IfxScuWdt.h"              /* Include watchdog APIs */
#include "Bsp.h"                    /* Include waitTime APIs */

#include "Button.h"                 /* Include button driver */
#include "Buzzer.h"                 /* Include buzzer driver */
#include "Fault_Manager.h"          /* Include fault manager */
#include "Ethernet_Handler.h"       /* Include ethernet handler */

/* Keep this name because other CPU files may reference it */
IFX_ALIGN(4) IfxCpu_syncEvent cpuSyncEvent = 0;

/* Debug: proves main loop is running */
volatile uint32 g_debugLoopCounter = 0;

/* Initialize application modules */
static void App_Init(void)
{
    /* Initialize button driver */
    Button_Init();

    /* Initialize buzzer driver */
    Buzzer_Init();

    /* Initialize fault manager */
    FaultManager_Init();

    /* Initialize ethernet handler */
    EthernetHandler_Init();
}

/* Run application task */
static void App_Task(void)
{
    /* Read button and update button fault source */
    FaultManager_SetButtonFault(Button_IsPressed());

    /* Simulate ethernet frame command from debugger */
    EthernetHandler_DebugCommandTask();

    /* Update final fault state from all sources */
    FaultManager_Update();

    /* Check final fault state */
    if (FaultManager_IsFaultActive() == TRUE)
    {
        /* Run buzzer while any fault is active */
        Buzzer_BeepTask();
    }
    else
    {
        /* Stop buzzer when no fault is active */
        Buzzer_Off();

        /* Small delay when no fault exists */
        waitTime(IfxStm_getTicksFromMilliseconds(BSP_DEFAULT_TIMER, 1));
    }
}

/* Core 0 main function */
void core0_main(void)
{
    /* Enable global interrupts */
    IfxCpu_enableInterrupts();

    /* Disable CPU watchdog */
    IfxScuWdt_disableCpuWatchdog(IfxScuWdt_getCpuWatchdogPassword());

    /* Disable safety watchdog */
    IfxScuWdt_disableSafetyWatchdog(IfxScuWdt_getSafetyWatchdogPassword());

    /* Emit CPU sync event */
    IfxCpu_emitEvent(&cpuSyncEvent);

    /* Wait for CPU sync */
    IfxCpu_waitEvent(&cpuSyncEvent, 1);

    /* Initialize application */
    App_Init();

    /* Run forever */
    while (1)
    {
        /* Increment loop counter */
        g_debugLoopCounter++;

        /* Run application task */
        App_Task();
    }
}
