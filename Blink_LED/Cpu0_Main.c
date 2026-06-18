#include "Ifx_Types.h"              /* Include basic AURIX data types */
#include "IfxCpu.h"                 /* Include CPU control functions */
#include "IfxScuWdt.h"              /* Include watchdog control functions */
#include "IfxPort.h"                /* Include GPIO port driver */
#include "Bsp.h"                    /* Include delay helper functions */

IFX_ALIGN(4) IfxCpu_syncEvent cpuSyncEvent = 0;   /* Create CPU synchronization event used by all CPU cores */

/* Onboard LED D107 is connected to P13.0 and it is low active */
#define LED_D107        &MODULE_P13, 0            /* Define onboard LED D107 pin */

/* Onboard beeper is connected to P33.0 */
#define BEEPER_PIN      &MODULE_P33, 0            /* Define onboard beeper pin */

/* Buzzer target frequency is around 2048 Hz, so half period is about 244 us */
#define BEEPER_HALF_PERIOD_US   244U              /* Define half period for 2048 Hz square wave */

#define BEEP_TIME_MS            100U              /* Define beeper ON time */
#define OFF_TIME_MS             400U              /* Define silent time */

/* Initialize LED and beeper pins */
void initPins(void)
{
    IfxPort_setPinModeOutput(LED_D107, IfxPort_OutputMode_pushPull, IfxPort_OutputIdx_general);    /* Configure LED pin as output */
    IfxPort_setPinModeOutput(BEEPER_PIN, IfxPort_OutputMode_pushPull, IfxPort_OutputIdx_general);  /* Configure beeper pin as output */

    IfxPort_setPinHigh(LED_D107);     /* Turn LED OFF because onboard LED is low active */
    IfxPort_setPinLow(BEEPER_PIN);    /* Turn beeper OFF at startup */
}

/* Turn LED ON */
void ledOn(void)
{
    IfxPort_setPinLow(LED_D107);      /* Set pin LOW to turn ON the low-active LED */
}

/* Turn LED OFF */
void ledOff(void)
{
    IfxPort_setPinHigh(LED_D107);     /* Set pin HIGH to turn OFF the low-active LED */
}

/* Generate beeper sound using software toggling */
void beepBlocking(uint32 durationMs)
{
    uint32 totalTimeUs = durationMs * 1000U;                  /* Convert duration from milliseconds to microseconds */
    uint32 toggleCount = totalTimeUs / BEEPER_HALF_PERIOD_US; /* Calculate number of pin toggles */

    for (uint32 i = 0U; i < toggleCount; i++)                 /* Loop until the sound duration finishes */
    {
        IfxPort_togglePin(BEEPER_PIN);                        /* Toggle beeper pin to generate square wave */
        waitTime(IfxStm_getTicksFromMicroseconds(BSP_DEFAULT_TIMER, BEEPER_HALF_PERIOD_US)); /* Wait half period */
    }

    IfxPort_setPinLow(BEEPER_PIN);                            /* Force beeper OFF after sound finishes */
}

/* CPU0 main function */
void core0_main(void)
{
    IfxCpu_enableInterrupts();                                /* Enable global interrupts */

    IfxScuWdt_disableCpuWatchdog(IfxScuWdt_getCpuWatchdogPassword());       /* Disable CPU watchdog */
    IfxScuWdt_disableSafetyWatchdog(IfxScuWdt_getSafetyWatchdogPassword()); /* Disable safety watchdog */

    IfxCpu_emitEvent(&cpuSyncEvent);                          /* Send CPU synchronization event */
    IfxCpu_waitEvent(&cpuSyncEvent, 1);                       /* Wait for CPU synchronization event */

    initPins();                                               /* Initialize LED and beeper pins */

    while (1)                                                 /* Run forever */
    {
        ledOn();                                              /* Turn LED ON */
        beepBlocking(BEEP_TIME_MS);                           /* Play beeper sound for 100 ms */

        ledOff();                                             /* Turn LED OFF */
        waitTime(IfxStm_getTicksFromMilliseconds(BSP_DEFAULT_TIMER, OFF_TIME_MS)); /* Wait 400 ms */
    }
}
