# AURIX Fault Injection Button + Buzzer Demo

## 1. Core Idea

This project demonstrates a simple **fault injection flow** on an Infineon AURIX TC3xx / TC397-style board.

A push button is used as a fake fault source:

```text
Button pressed  -> fault active   -> buzzer beeps
Button released -> fault inactive -> buzzer stops
```

The project is later prepared to accept the same fault command from an **Ethernet frame**.

---

## 2. Hardware Pins

### Used pins

| Function | AURIX Pin | Connector Pin | Direction | Notes |
|---|---:|---:|---|---|
| Button input | `P14.4` | `X102 pin 39` | Input | Configured as input pull-up |
| Ground | `GND` | `X102 pin 3 or 4` | Ground | Used with button |
| Buzzer | `P33.0` | On-board buzzer | Output | Software toggled to generate sound |

### Button connection

```text
X102 pin 39 / P14.4  ---- Button ----  X102 pin 3 or 4 / GND
```

### Why button press reads LOW

`P14.4` is configured as **input pull-up**.

```text
Button released:

Internal pull-up
      |
     HIGH
      |
    P14.4 ---- open ---- GND

Result:
P14.4 = HIGH
fault = inactive
```

```text
Button pressed:

Internal pull-up
      |
     HIGH
      |
    P14.4 ---- closed button ---- GND

Result:
P14.4 = LOW
fault = active
```

So this button is **active-low**:

```text
HIGH -> button released
LOW  -> button pressed
```

---

## 3. Final Software Flow

```text
+------------------+
| Button Driver    |
| reads P14.4      |
+---------+--------+
          |
          v
+------------------+       +------------------+
| Fault Manager    | <---- | Ethernet Handler |
| combines sources |       | handles frames   |
+---------+--------+       +------------------+
          |
          v
+------------------+
| Buzzer Driver    |
| drives P33.0     |
+------------------+
```

The final fault state is:

```c
finalFault = buttonFault || ethernetFault;
```

Meaning:

```text
If button fault is active    -> buzzer ON
If ethernet fault is active  -> buzzer ON
If no fault is active        -> buzzer OFF
```

---

## 4. Project File Structure

Recommended structure:

```text
button_inject_fault/
├── Cpu0_Main.c
├── Cpu1_Main.c
├── Cpu2_Main.c
├── Cpu3_Main.c
├── Cpu4_Main.c
├── Cpu5_Main.c
│
├── Application/
│   ├── Inc/
│   │   ├── Button.h
│   │   ├── Buzzer.h
│   │   ├── Fault_Manager.h
│   │   └── Ethernet_Handler.h
│   │
│   └── Src/
│       ├── Button.c
│       ├── Buzzer.c
│       ├── Fault_Manager.c
│       └── Ethernet_Handler.c
│
├── Configurations/
├── Libraries/
└── TriCore Debug (TASKING)/
```

Do **not** put your source files inside:

```text
TriCore Debug (TASKING)
Binaries
Configurations
Libraries
```

Those are build/generated/config folders.

---

## 5. Module Responsibilities

### `Button.c / Button.h`

Responsible only for the button input.

Main APIs:

```c
void Button_Init(void);
boolean Button_IsPressed(void);
```

What it does:

```text
Configure P14.4 as input pull-up
Read Port14 input register
Check bit 4
Return TRUE if P14.4 is LOW
```

Important logic:

```c
g_debugP14_IN = MODULE_P14.IN.U;
g_debugP14LowMask = (~g_debugP14_IN) & 0xFFFFu;

if ((g_debugP14LowMask & (1u << 4)) != 0u)
{
    button is pressed
}
```

Why read register directly?

This worked reliably during debug:

```c
MODULE_P14.IN.U
```

It reads the full Port14 input register.

---

### `Buzzer.c / Buzzer.h`

Responsible only for the buzzer output.

Main APIs:

```c
void Buzzer_Init(void);
void Buzzer_Off(void);
void Buzzer_BeepTask(void);
```

The buzzer is driven using software toggling:

```text
P33.0 HIGH
delay 244 us
P33.0 LOW
delay 244 us
repeat
```

This creates an audible square wave.

Approximate frequency:

```text
Period = 244 us + 244 us = 488 us
Frequency ≈ 1 / 488 us ≈ 2049 Hz
```

Important: the debugger must be in **Resume / Run** mode.  
If the program is paused or stepping line by line, the buzzer will not make sound.

---

### `Fault_Manager.c / Fault_Manager.h`

Responsible for collecting fault sources and deciding the final fault state.

Main APIs:

```c
void FaultManager_Init(void);
void FaultManager_SetButtonFault(boolean active);
void FaultManager_SetEthernetFault(boolean active);
void FaultManager_Update(void);
boolean FaultManager_IsFaultActive(void);
```

Fault sources:

```text
buttonFault
ethernetFault
```

Final logic:

```c
g_faultActive = g_buttonFault || g_ethernetFault;
```

This makes the system scalable.

Later, more fault sources can be added:

```text
sensorFault
temperatureFault
timeoutFault
CANFault
watchdogFault
```

---

### `Ethernet_Handler.c / Ethernet_Handler.h`

Responsible for handling Ethernet-like commands.

Current version uses fake debug commands from Expressions.

Commands:

| Command | Value | Meaning |
|---|---:|---|
| `ETH_CMD_NONE` | `0` | No command |
| `ETH_CMD_INJECT_FAULT` | `1` | Set Ethernet fault active |
| `ETH_CMD_CLEAR_FAULT` | `2` | Clear Ethernet fault |
| `ETH_CMD_GET_STATUS` | `3` | Reserved for status response |

Important rule:

The Ethernet handler should **not** drive the buzzer directly.

Correct flow:

```text
Ethernet frame -> Ethernet Handler -> Fault Manager -> Buzzer Driver
```

Wrong flow:

```text
Ethernet frame -> Buzzer directly
```

Reason: the Ethernet handler should only parse frames and update state flags.

---

## 6. Main Loop Flow

`Cpu0_Main.c` should stay clean.

Main flow:

```text
Start
  |
  v
Disable watchdogs
  |
  v
Initialize modules
  |
  v
while(1)
{
    Read button
    Handle fake ethernet command
    Update fault manager
    Run buzzer based on final fault
}
```

Simplified logic:

```c
FaultManager_SetButtonFault(Button_IsPressed());

EthernetHandler_DebugCommandTask();

FaultManager_Update();

if (FaultManager_IsFaultActive() == TRUE)
{
    Buzzer_BeepTask();
}
else
{
    Buzzer_Off();
}
```

---

## 7. Debug Variables

Add these variables to **Expressions** in AURIX Development Studio.

### General

| Variable | Meaning |
|---|---|
| `g_debugLoopCounter` | Increments continuously if main loop is running |

### Button

| Variable | Meaning |
|---|---|
| `g_debugP14_IN` | Raw Port14 input register |
| `g_debugP14LowMask` | LOW pins shown as `1` |
| `g_debugButtonPressed` | `1` if `P14.4` is LOW |

### Fault Manager

| Variable | Meaning |
|---|---|
| `g_debugButtonFault` | Fault source from button |
| `g_debugEthernetFault` | Fault source from Ethernet command |
| `g_debugFaultActive` | Final fault state |

### Buzzer

| Variable | Meaning |
|---|---|
| `g_debugBuzzerActive` | `1` when buzzer task is active |

### Ethernet simulation

| Variable | Meaning |
|---|---|
| `g_debugEthCommandInject` | Write command here from Expressions |
| `g_debugLastEthCommand` | Last handled Ethernet command |

---

## 8. How to Test

### Test 1: Button fault

Connection:

```text
X102 pin 39 / P14.4 ---- Button ---- X102 pin 3 or 4 / GND
```

Expected behavior:

```text
Press button   -> buzzer ON
Release button -> buzzer OFF
```

Expected debug values while pressed:

```text
g_debugButtonPressed = 1
g_debugButtonFault   = 1
g_debugFaultActive   = 1
g_debugBuzzerActive  = 1
```

Expected debug values while released:

```text
g_debugButtonPressed = 0
g_debugButtonFault   = 0
g_debugFaultActive   = 0
g_debugBuzzerActive  = 0
```

---

### Test 2: Fake Ethernet inject fault

In Expressions:

```text
g_debugEthCommandInject = 1
```

Then press **Resume / F8**.

Expected:

```text
g_debugEthernetFault = 1
g_debugFaultActive   = 1
buzzer ON
```

---

### Test 3: Fake Ethernet clear fault

In Expressions:

```text
g_debugEthCommandInject = 2
```

Then press **Resume / F8**.

Expected:

```text
g_debugEthernetFault = 0
g_debugFaultActive   = 0
buzzer OFF
```

---

## 9. Build Notes

### Include path

Add this include path:

```text
${ProjDirPath}/Application/Inc
```

Path:

```text
Right click project
-> Properties
-> C/C++ Build
-> Settings
-> TASKING C/C++ Compiler
-> Include paths
-> Add
```

### Make sure `.c` files are included in build

For each file:

```text
Application/Src/Button.c
Application/Src/Buzzer.c
Application/Src/Fault_Manager.c
Application/Src/Ethernet_Handler.c
```

Check:

```text
Right click file
-> Resource Configurations
-> Exclude from Build
```

All configurations should be unchecked.

Correct:

```text
[ ] TriCore Debug (GCC)
[ ] TriCore Debug (TASKING)
[ ] TriCore Release (GCC)
[ ] TriCore Release (TASKING)
```

Then:

```text
Project -> Clean
Project -> Build
```

Expected build log should include:

```text
Application/Src/Button.o
Application/Src/Buzzer.o
Application/Src/Fault_Manager.o
Application/Src/Ethernet_Handler.o
```

---

## 10. Common Issues

### Issue: `unresolved external: Button_Init`

Cause:

```text
Button.c is not included in build
```

Fix:

```text
Remove Exclude from Build on Button.c
Clean and rebuild
```

---

### Issue: `unresolved external: FaultManager_Init`

Cause:

```text
Fault_Manager.c is not included in build
or the function name is different
```

Fix:

Check that the function name is exactly:

```c
void FaultManager_Init(void)
```

Not:

```c
void Fault_Manager_Init(void)
void FaultManger_Init(void)
static void FaultManager_Init(void)
```

---

### Issue: buzzer works only when running, not while stepping

Cause:

```text
The buzzer needs continuous fast toggling
```

Fix:

```text
Use Resume / F8, not step-by-step execution
```

---

### Issue: button always reads released

Possible causes:

```text
Wrong connector: using X103 instead of X102
Wrong pin counting
Button connected to VCC instead of GND
Program is paused
```

Correct connection:

```text
X102 pin 39 / P14.4 ---- Button ---- X102 pin 3 or 4 / GND
```

---

## 11. Key Concepts to Remember

### Active-low button

```text
Released -> HIGH
Pressed  -> LOW
```

### Input pull-up

The MCU internally pulls the input pin HIGH when the button is not pressed.

### Fault Manager

Do not let each source control the buzzer directly.

Correct:

```text
Source -> Fault Manager -> Output
```

### Ethernet handler rule

The frame handler should only update state.

```text
Do not block
Do not delay
Do not toggle buzzer directly
```

### `IFX_ALIGN(4)`

Used to place `cpuSyncEvent` at a 4-byte aligned memory address.

```c
IFX_ALIGN(4) IfxCpu_syncEvent cpuSyncEvent = 0;
```

It affects this variable only, not all variables in the program.

---

## 12. Final Quick Flow

```text
Button press
    |
    v
P14.4 becomes LOW
    |
    v
Button_IsPressed() returns TRUE
    |
    v
FaultManager_SetButtonFault(TRUE)
    |
    v
FaultManager_Update()
    |
    v
final fault = buttonFault OR ethernetFault
    |
    v
Buzzer_BeepTask()
    |
    v
P33.0 toggles
    |
    v
buzzer sound
```

```text
Button release
    |
    v
P14.4 becomes HIGH
    |
    v
Button_IsPressed() returns FALSE
    |
    v
FaultManager_SetButtonFault(FALSE)
    |
    v
final fault becomes inactive
    |
    v
Buzzer_Off()
```

---

## 13. Current Project Status

Completed:

```text
Button hardware input tested
Buzzer output tested
Fault concept tested
Modular architecture prepared
Fake Ethernet command path prepared
```

Next possible steps:

```text
Add real Ethernet receive driver
Parse real Ethernet frame payload
Send status response frame
Replace software buzzer toggle with GTM/TOM PWM
Add more fault sources
```
