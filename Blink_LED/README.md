# Blink_LED Project - AURIX TC3X7

This project is a simple first GPIO project on the **Infineon AURIX TC3X7 Application Kit**.

The goal is to understand the basic flow:

```text
Create Project → Write Code → Build → Flash/Debug → Run
```

The project controls:

```text
Onboard LED D107  → P13.0
Onboard Beeper    → P33.0
```

---

## 1. Project Idea

The application runs on **CPU0** and repeats forever:

```text
1. Turn ON onboard LED D107
2. Generate a short beeper sound
3. Turn OFF onboard LED D107
4. Wait for a short delay
5. Repeat forever
```

Expected behavior:

```text
LED turns ON
Beeper makes a short sound
LED turns OFF
Delay
Repeat
```

---

## 2. Used Board Resources

### Onboard LED

The project uses onboard LED **D107**.

```text
D107 → P13.0
```

Important note:

```text
The onboard LED is LOW active
```

That means:

```text
P13.0 = LOW   → LED ON
P13.0 = HIGH  → LED OFF
```

So in code:

```c
IfxPort_setPinLow(LED_D107);   /* Turn ON the LED because it is low active */
IfxPort_setPinHigh(LED_D107);  /* Turn OFF the LED because it is low active */
```

---

### Onboard Beeper

The project uses the onboard beeper.

```text
Beeper → P33.0
```

In this first simple project, the beeper is driven using **software toggling**.

The code toggles `P33.0` quickly to generate a square wave around `2048 Hz`.

This is good for learning, but it keeps the CPU busy while the beep is running.

Later, the better solution is to use:

```text
GTM TOM PWM
```

because PWM hardware can generate the beeper frequency without blocking the CPU.

---

## 3. Create New Project

Open **AURIX Development Studio**.

Create a new project:

```text
File → New → New AURIX Project
```

Project name:

```text
Blink_LED
```

Select the board/device that matches the used kit.

Example board names may look like:

```text
KIT_A2G_TC397_5V_TFT
KIT_A2G_TC397_3V3_TFT
KIT_A2G_TC387_5V_TFT
KIT_A2G_TC387_3V3_TFT
```

Then press:

```text
Finish
```

---

## 4. Main Code File

Open:

```text
Cpu0_Main.c
```

This is the main application file for CPU0.

The important parts in the code are:

```c
#define LED_D107        &MODULE_P13, 0
#define BEEPER_PIN      &MODULE_P33, 0
```

These macros define the used pins.

---

## 5. Code Explanation

### Include Files

```c
#include "Ifx_Types.h"              /* Include basic AURIX data types */
#include "IfxCpu.h"                 /* Include CPU control functions */
#include "IfxScuWdt.h"              /* Include watchdog control functions */
#include "IfxPort.h"                /* Include GPIO port driver */
#include "Bsp.h"                    /* Include delay helper functions */
```

These headers provide:

```text
Ifx_Types.h   → basic data types like uint32
IfxCpu.h      → CPU functions and CPU sync
IfxScuWdt.h   → watchdog control
IfxPort.h     → GPIO control
Bsp.h         → delay functions
```

---

### CPU Sync Variable

```c
IFX_ALIGN(4) IfxCpu_syncEvent cpuSyncEvent = 0;
```

This variable is used for CPU synchronization.

Important:

```text
The name must be cpuSyncEvent
```

If the name is changed, other CPU files like `Cpu1_Main.c` may fail during linking with an error like:

```text
unresolved external: cpuSyncEvent
```

---

### GPIO Initialization

```c
void initPins(void)
{
    IfxPort_setPinModeOutput(LED_D107, IfxPort_OutputMode_pushPull, IfxPort_OutputIdx_general);    /* Configure LED pin as output */
    IfxPort_setPinModeOutput(BEEPER_PIN, IfxPort_OutputMode_pushPull, IfxPort_OutputIdx_general);  /* Configure beeper pin as output */

    IfxPort_setPinHigh(LED_D107);     /* Turn LED OFF because onboard LED is low active */
    IfxPort_setPinLow(BEEPER_PIN);    /* Turn beeper OFF at startup */
}
```

This function:

```text
1. Configures P13.0 as output for LED
2. Configures P33.0 as output for beeper
3. Turns LED OFF at startup
4. Turns beeper OFF at startup
```

---

### LED Functions

```c
void ledOn(void)
{
    IfxPort_setPinLow(LED_D107);      /* Set pin LOW to turn ON the low-active LED */
}

void ledOff(void)
{
    IfxPort_setPinHigh(LED_D107);     /* Set pin HIGH to turn OFF the low-active LED */
}
```

Because the LED is low active:

```text
LOW  → ON
HIGH → OFF
```

---

### Beeper Function

```c
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
```

This function generates sound by toggling the beeper pin.

The half period is:

```c
#define BEEPER_HALF_PERIOD_US   244U
```

So the approximate frequency is:

```text
Frequency = 1 / (244 us + 244 us)
Frequency ≈ 2049 Hz
```

---

### Main Function

```c
void core0_main(void)
{
    IfxCpu_enableInterrupts();                                            /* Enable global interrupts */

    IfxScuWdt_disableCpuWatchdog(IfxScuWdt_getCpuWatchdogPassword());     /* Disable CPU watchdog */
    IfxScuWdt_disableSafetyWatchdog(IfxScuWdt_getSafetyWatchdogPassword()); /* Disable safety watchdog */

    IfxCpu_emitEvent(&cpuSyncEvent);                                      /* Send CPU synchronization event */
    IfxCpu_waitEvent(&cpuSyncEvent, 1);                                   /* Wait for CPU synchronization event */

    initPins();                                                           /* Initialize LED and beeper pins */

    while (1)                                                             /* Run forever */
    {
        ledOn();                                                          /* Turn LED ON */
        beepBlocking(BEEP_TIME_MS);                                       /* Play beeper sound for 100 ms */

        ledOff();                                                         /* Turn LED OFF */
        waitTime(IfxStm_getTicksFromMilliseconds(BSP_DEFAULT_TIMER, OFF_TIME_MS)); /* Wait 400 ms */
    }
}
```

The main loop repeats forever.

---

## 6. Build Project

Before flashing/debugging, build the project.

In AURIX Development Studio, press:

```text
Build Active Project
```

Expected result:

```text
Build Finished
0 errors
```

If you get this error:

```text
unresolved external: cpuSyncEvent
```

Check that the sync variable name is exactly:

```c
cpuSyncEvent
```

not:

```c
g_cpuSyncEvent
```

---

## 7. Flash / Debug Project

Connect the board using the correct USB port.

Then open:

```text
Debug Configurations
```

Select:

```text
Blink_LED TriCore Debug (TASKING)
```

Make sure:

```text
Project to launch = Blink_LED
Debug binary = Blink_LED.elf
Enable Flashing = checked
Stop at function = core0_main
```

Then choose the connected board from:

```text
Select a board
```

Press:

```text
Apply
Debug
```

When the debug session opens, press:

```text
Resume
```

---

## 8. TAS/DAS Debugger Notes

If `Select a board` is empty, the IDE does not see the board through TAS/DAS.

Check these points:

```text
1. Use a USB data cable, not a charge-only cable
2. Connect directly to the PC, not through a weak USB hub
3. Check Device Manager
4. Make sure Infineon DAS/TAS is installed
5. Restart Windows after installing DAS/TAS
6. Reopen AURIX Studio and press Update in Select a board
```

In Device Manager, the board/debugger may appear like:

```text
Infineon DAS JDS COM (COMx)
```

If DAS/TAS is missing, install it from the AURIX Studio folder.

Example installer path:

```text
C:\Infineon\AURIX-Studio-1.10.32\DAS_V8_3_0_1_SETUP.exe
```

Use:

```text
Quick Installation (Recommended)
```

Then restart Windows.

---

## 9. Full Expected Output

After flashing and pressing Resume:

```text
D107 LED turns ON
Beeper makes short sound
D107 LED turns OFF
Short delay
Repeat forever
```

---

