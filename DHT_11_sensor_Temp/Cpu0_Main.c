#include "Ifx_Types.h"              /* Include basic AURIX data types */
#include "IfxCpu.h"                 /* Include CPU control functions */
#include "IfxScuWdt.h"              /* Include watchdog control functions */
#include "IfxPort.h"                /* Include GPIO port driver */
#include "IfxStm.h"                 /* Include STM timer driver */
#include "Bsp.h"                    /* Include delay helper functions */

IFX_ALIGN(4) IfxCpu_syncEvent cpuSyncEvent = 0; /* Create CPU synchronization event */

#define DHT11_PIN &MODULE_P33, 3    /* Define DHT11 DATA pin: X102 pin 15 = P33.3 */
#define BUZZER_PIN &MODULE_P33, 0   /* Define onboard buzzer pin: P33.0 */

/* For testing use 20U so the buzzer must sound at room temperature.
   After confirming the DHT11 works, change this to 35U or 40U. */
#define TEMP_THRESHOLD_C 40U        /* Turn buzzer alarm ON when temperature is equal or higher than this value */

#define DHT11_START_LOW_MS 20U      /* DHT11 start signal LOW time */
#define DHT11_POWER_UP_MS 2000U     /* Wait after power-up before first DHT11 read */
#define DHT11_READ_PERIOD_MS 2000U  /* Wait between reads because DHT11 is slow */

#define DHT11_IDLE_WAIT_MS 50U      /* Wait after releasing DATA before checking idle */
#define DHT11_IDLE_TIMEOUT_US 20000U /* Wait up to 20 ms for DATA to become HIGH before start */

#define DHT11_RELEASE_HIGH_TIMEOUT_US 3000U  /* Wait until DATA becomes HIGH after MCU release */
#define DHT11_RESPONSE_FALL_TIMEOUT_US 5000U /* Wait for DHT11 response falling edge */
#define DHT11_RESPONSE_EDGE_TIMEOUT_US 3000U /* Wait for response edges */
#define DHT11_BIT_EDGE_TIMEOUT_US 1000U      /* Wait for bit edges */

#define DHT11_BIT_ONE_THRESHOLD_US 50U       /* HIGH pulse above this is bit 1 */

#define BUZZER_HALF_PERIOD_US 244U  /* Half period for about 2048 Hz buzzer tone */

/* Store DHT11 data */
typedef struct
{
    uint8 humidityInteger;          /* Store humidity integer byte */
    uint8 humidityDecimal;          /* Store humidity decimal byte */
    uint8 temperatureInteger;       /* Store temperature integer byte */
    uint8 temperatureDecimal;       /* Store temperature decimal byte */
    uint8 checksum;                 /* Store checksum byte */
} Dht11_Data;

volatile uint8 g_lastError = 0U;       /* Store last error code for debugger */
volatile uint8 g_lastRaw[5] = {0U};    /* Store last raw frame for debugger */
volatile uint32 g_debugResult = 0U;  /* Store last DHT11 read result as number */
volatile uint32 g_debugTemp = 0U;    /* Store last temperature as number */
volatile uint32 g_debugHum = 0U;     /* Store last humidity as number */
volatile uint32 g_debugAlarm = 0U;   /* Store alarm state as number */
volatile uint8 g_debugDataLevel = 0U;  /* Store DHT11 DATA level */

/* Initialize GPIO pins */
void initPins(void)
{
    IfxPort_setPinModeInput(DHT11_PIN, IfxPort_InputMode_noPullDevice); /* Set DHT11 DATA as input and use external pull-up only */
    IfxPort_setPinModeOutput(BUZZER_PIN, IfxPort_OutputMode_pushPull, IfxPort_OutputIdx_general); /* Set buzzer pin as push-pull output */
    IfxPort_setPinLow(BUZZER_PIN); /* Keep buzzer OFF at startup */
}

/* Read DHT11 DATA level using iLLD API */
uint8 dht11ReadLevel(void)
{
    return (IfxPort_getPinState(DHT11_PIN) == IfxPort_State_high) ? 1U : 0U; /* Return 1 if DATA is HIGH, otherwise return 0 */
}

/* Drive DHT11 DATA LOW using push-pull output */
void dht11DriveLow(void)
{
    IfxPort_setPinModeOutput(DHT11_PIN, IfxPort_OutputMode_pushPull, IfxPort_OutputIdx_general); /* Configure DATA as output */
    IfxPort_setPinLow(DHT11_PIN); /* Drive DATA LOW */
}

/* Release DHT11 DATA line */
void dht11ReleaseLine(void)
{
    IfxPort_setPinModeInput(DHT11_PIN, IfxPort_InputMode_noPullDevice); /* Configure DATA as input without internal pull */
}

/* Wait until DATA reaches selected level */
boolean dht11WaitForLevel(uint8 expectedLevel, uint32 timeoutUs)
{
    Ifx_TickTime startTicks = IfxStm_get(BSP_DEFAULT_TIMER); /* Read current STM tick */
    Ifx_TickTime timeoutTicks = IfxStm_getTicksFromMicroseconds(BSP_DEFAULT_TIMER, timeoutUs); /* Convert timeout from us to STM ticks */

    while ((IfxStm_get(BSP_DEFAULT_TIMER) - startTicks) < timeoutTicks) /* Loop until timeout expires */
    {
        g_debugDataLevel = dht11ReadLevel(); /* Store DATA level for debugger */

        if (g_debugDataLevel == expectedLevel) /* Check if DATA reached expected level */
        {
            return TRUE; /* Expected level detected */
        }
    }

    return FALSE; /* Timeout happened */
}

/* Wait for rising edge LOW -> HIGH */
boolean dht11WaitForRisingEdge(uint32 timeoutUs)
{
    if (dht11WaitForLevel(0U, timeoutUs) == FALSE) /* First make sure the line is LOW */
    {
        return FALSE; /* LOW was not detected */
    }

    if (dht11WaitForLevel(1U, timeoutUs) == FALSE) /* Then wait until the line becomes HIGH */
    {
        return FALSE; /* HIGH was not detected */
    }

    return TRUE; /* Rising edge detected */
}

/* Wait for falling edge HIGH -> LOW */
boolean dht11WaitForFallingEdge(uint32 timeoutUs)
{
    if (dht11WaitForLevel(1U, timeoutUs) == FALSE) /* First make sure the line is HIGH */
    {
        return FALSE; /* HIGH was not detected */
    }

    if (dht11WaitForLevel(0U, timeoutUs) == FALSE) /* Then wait until the line becomes LOW */
    {
        return FALSE; /* LOW was not detected */
    }

    return TRUE; /* Falling edge detected */
}

/* Read full raw 40-bit DHT11 frame */
uint8 dht11ReadRawFrame(uint8 rawData[5])
{
    boolean interruptState; /* Store interrupt state before critical timing section */
    Ifx_TickTime highStartTicks; /* Store HIGH pulse start tick */
    Ifx_TickTime highTicks; /* Store HIGH pulse duration */
    Ifx_TickTime oneThresholdTicks = IfxStm_getTicksFromMicroseconds(BSP_DEFAULT_TIMER, DHT11_BIT_ONE_THRESHOLD_US); /* Convert bit threshold to ticks */

    for (uint8 i = 0U; i < 5U; i++) /* Clear raw data array */
    {
        rawData[i] = 0U; /* Reset each byte */
    }

    dht11ReleaseLine(); /* Make sure DATA is released before starting */
    waitTime(IfxStm_getTicksFromMilliseconds(BSP_DEFAULT_TIMER, DHT11_IDLE_WAIT_MS)); /* Give DATA enough time to return HIGH */

    if (dht11WaitForLevel(1U, DHT11_IDLE_TIMEOUT_US) == FALSE) /* Wait until DATA becomes HIGH before start */
    {
        return 1U; /* DATA line is stuck LOW before start */
    }

    dht11DriveLow(); /* Pull DATA LOW to start DHT11 communication */
    waitTime(IfxStm_getTicksFromMilliseconds(BSP_DEFAULT_TIMER, DHT11_START_LOW_MS)); /* Keep DATA LOW for 20 ms */

    interruptState = IfxCpu_disableInterrupts(); /* Disable interrupts during timing-sensitive DHT11 frame */

    dht11ReleaseLine(); /* Release DATA line so external pull-up can raise it and DHT11 can respond */

    if (dht11WaitForLevel(1U, DHT11_RELEASE_HIGH_TIMEOUT_US) == FALSE) /* Wait until DATA really goes HIGH after release */
    {
        IfxCpu_restoreInterrupts(interruptState); /* Restore interrupts before returning */
        return 2U; /* Response LOW cannot be trusted because line did not release HIGH */
    }

    if (dht11WaitForFallingEdge(DHT11_RESPONSE_FALL_TIMEOUT_US) == FALSE) /* Wait for DHT11 response LOW falling edge */
    {
        IfxCpu_restoreInterrupts(interruptState); /* Restore interrupts before returning */
        return 2U; /* DHT11 response LOW not detected */
    }

    if (dht11WaitForRisingEdge(DHT11_RESPONSE_EDGE_TIMEOUT_US) == FALSE) /* Wait for DHT11 response HIGH rising edge */
    {
        IfxCpu_restoreInterrupts(interruptState); /* Restore interrupts before returning */
        return 3U; /* DHT11 response HIGH not detected */
    }

    if (dht11WaitForFallingEdge(DHT11_RESPONSE_EDGE_TIMEOUT_US) == FALSE) /* Wait for falling edge after response HIGH */
    {
        IfxCpu_restoreInterrupts(interruptState); /* Restore interrupts before returning */
        return 4U; /* Data frame start failed */
    }

    for (uint8 bitIndex = 0U; bitIndex < 40U; bitIndex++) /* Read 40 bits from DHT11 */
    {
        if (dht11WaitForRisingEdge(DHT11_BIT_EDGE_TIMEOUT_US) == FALSE) /* Wait for bit HIGH pulse start */
        {
            IfxCpu_restoreInterrupts(interruptState); /* Restore interrupts before returning */
            return 5U; /* Bit read failed */
        }

        highStartTicks = IfxStm_get(BSP_DEFAULT_TIMER); /* Save HIGH pulse start time */

        if (dht11WaitForFallingEdge(DHT11_BIT_EDGE_TIMEOUT_US) == FALSE) /* Wait for bit HIGH pulse end */
        {
            IfxCpu_restoreInterrupts(interruptState); /* Restore interrupts before returning */
            return 5U; /* Bit read failed */
        }

        highTicks = IfxStm_get(BSP_DEFAULT_TIMER) - highStartTicks; /* Calculate HIGH pulse duration */

        rawData[bitIndex / 8U] <<= 1U; /* Shift current byte left to make room for new bit */

        if (highTicks > oneThresholdTicks) /* Check if HIGH pulse is long enough to be bit 1 */
        {
            rawData[bitIndex / 8U] |= 1U; /* Store bit 1 */
        }
    }

    IfxCpu_restoreInterrupts(interruptState); /* Restore interrupts after frame is completely read */

    return 0U; /* Raw frame read successfully */
}

/* Read and validate DHT11 data */
uint8 dht11ReadData(Dht11_Data *data)
{
    uint8 rawData[5]; /* Store 5 bytes from DHT11 */
    uint8 result; /* Store read result */
    uint8 checksumCalculated; /* Store calculated checksum */

    result = dht11ReadRawFrame(rawData); /* Read raw 40-bit frame */

    for (uint8 i = 0U; i < 5U; i++) /* Copy raw frame to global debug array */
    {
        g_lastRaw[i] = rawData[i]; /* Store byte for debugger watch window */
    }

    if (result != 0U) /* Check if raw read failed */
    {
        g_lastError = result; /* Store error code for debugger */
        return result; /* Return read error */
    }

    checksumCalculated = rawData[0] + rawData[1] + rawData[2] + rawData[3]; /* Calculate DHT11 checksum */

    if (checksumCalculated != rawData[4]) /* Compare calculated checksum with received checksum */
    {
        g_lastError = 6U; /* Store checksum error */
        return 6U; /* Checksum failed */
    }

    data->humidityInteger = rawData[0]; /* Copy humidity integer byte */
    data->humidityDecimal = rawData[1]; /* Copy humidity decimal byte */
    data->temperatureInteger = rawData[2]; /* Copy temperature integer byte */
    data->temperatureDecimal = rawData[3]; /* Copy temperature decimal byte */
    data->checksum = rawData[4]; /* Copy checksum byte */

    g_lastError = 0U; /* Clear error code */

    return 0U; /* Read success */
}

/* Turn buzzer OFF */
void buzzerOff(void)
{
    IfxPort_setPinLow(BUZZER_PIN); /* Force buzzer pin LOW */
}

/* Generate one buzzer beep */
void buzzerBeep(uint32 durationMs)
{
    uint32 totalTimeUs = durationMs * 1000U; /* Convert duration from ms to us */
    uint32 toggleCount = totalTimeUs / BUZZER_HALF_PERIOD_US; /* Calculate number of pin toggles */

    for (uint32 i = 0U; i < toggleCount; i++) /* Toggle buzzer for selected duration */
    {
        IfxPort_togglePin(BUZZER_PIN); /* Toggle buzzer output */
        waitTime(IfxStm_getTicksFromMicroseconds(BSP_DEFAULT_TIMER, BUZZER_HALF_PERIOD_US)); /* Wait half tone period */
    }

    buzzerOff(); /* Stop buzzer after beep */
}

/* Generate repeated short beep pattern */
void buzzerPattern(uint8 count, uint32 beepMs)
{
    for (uint8 i = 0U; i < count; i++) /* Repeat selected number of beeps */
    {
        buzzerBeep(beepMs); /* Make one beep */
        waitTime(IfxStm_getTicksFromMilliseconds(BSP_DEFAULT_TIMER, 200U)); /* Wait between beeps */
    }
}

/* Show DHT11 error using buzzer diagnostic pattern */
void showError(uint8 errorCode)
{
    if (errorCode == 1U) /* DATA line stuck LOW before start */
    {
        buzzerBeep(1000U); /* Long beep */
    }
    else if (errorCode == 2U) /* Response LOW not detected */
    {
        buzzerPattern(2U, 120U); /* Two short beeps */
    }
    else if (errorCode == 3U) /* Response HIGH not detected */
    {
        buzzerPattern(3U, 120U); /* Three short beeps */
    }
    else if (errorCode == 4U) /* Data frame start failed */
    {
        buzzerPattern(4U, 120U); /* Four short beeps */
    }
    else if (errorCode == 5U) /* Bit read failed */
    {
        buzzerPattern(5U, 120U); /* Five short beeps */
    }
    else if (errorCode == 6U) /* Checksum failed */
    {
        buzzerBeep(500U); /* Medium beep */
    }
}

/* CPU0 main function */
void core0_main(void)
{
    Dht11_Data dht11Data; /* Create variable to store DHT11 data */
    uint8 result; /* Store DHT11 read result */

    IfxCpu_enableInterrupts(); /* Enable global interrupts */

    IfxScuWdt_disableCpuWatchdog(IfxScuWdt_getCpuWatchdogPassword()); /* Disable CPU watchdog */
    IfxScuWdt_disableSafetyWatchdog(IfxScuWdt_getSafetyWatchdogPassword()); /* Disable safety watchdog */

    IfxCpu_emitEvent(&cpuSyncEvent); /* Emit CPU sync event */
    IfxCpu_waitEvent(&cpuSyncEvent, 1); /* Wait for CPU sync event */

    initPins(); /* Initialize DHT11 and buzzer pins */

    buzzerPattern(1U, 120U); /* Startup beep to confirm that the code is running */

    waitTime(IfxStm_getTicksFromMilliseconds(BSP_DEFAULT_TIMER, DHT11_POWER_UP_MS)); /* Wait for DHT11 power-up */

    while (1) /* Main loop */
    {
        result = dht11ReadData(&dht11Data); /* Read DHT11 temperature and humidity */

        g_debugResult = result; /* Store result for debugger */

        if (result == 0U) /* Check if DHT11 read succeeded */
        {
            g_debugTemp = dht11Data.temperatureInteger; /* Store temperature for debugger */
            g_debugHum = dht11Data.humidityInteger; /* Store humidity for debugger */

            if (dht11Data.temperatureInteger >= TEMP_THRESHOLD_C) /* Check high temperature condition */
            {
                g_debugAlarm = 1U; /* Mark alarm as active */
                buzzerBeep(1200U); /* Sound alarm because temperature is above test threshold */
            }
            else
            {
                g_debugAlarm = 0U; /* Mark alarm as inactive */
                buzzerOff(); /* Keep buzzer OFF if temperature is below threshold */
            }
        }
        else
        {
            g_debugAlarm = 0U; /* Mark alarm as inactive because reading failed */
            showError(result); /* Show diagnostic beep pattern for DHT11 error */
        }

        waitTime(IfxStm_getTicksFromMilliseconds(BSP_DEFAULT_TIMER, DHT11_READ_PERIOD_MS)); /* Wait before next DHT11 read */
    }
}
