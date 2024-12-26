/**
* @file hw2.c
*
* This file is used in homework 2 of SJTU MST4311 to include a test for dual
* channel gpio drivers and gpio interrupts.

* This file contains a design example implementing a dual channel GPIO driver
* (XGpio) which controls the LEDs and two GPIO drivers (XGpio) in an
* interrupt driven mode of operation. This example assumes that there is NOT
* an interrupt controller in the hardware system and the GPIO interrupt
* signal is connected to the Zynq core IRQ_F2P[1:0] pins directly.
*
* Buttons 0-1 and 2-3 are on 2 separate channels of the GPIO so that interrupts
* are not caused when the buttons 2-3 are turned on and off.
*
*/

/***************************** Include Files *********************************/

#include <stdio.h>
#include "xparameters.h"
#include "xgpio.h"
#include "xil_exception.h"
#include "xil_printf.h"
#include "xscugic.h"
#include "xscutimer.h"
#include "xtime_l.h"
#include "xtmrctr.h"

/************************** Constant Definitions *****************************/

/** Device ID for button 0 (interrupt generator) */
#define BTN0_DEVICE_ID XPAR_BTN_0_DEVICE_ID
/** Device ID for button 1 (interrupt generator) */
#define BTN1_DEVICE_ID XPAR_BTN_1_DEVICE_ID
/** Device ID for the LED output */
#define LED_DEVICE_ID XPAR_LED_DEVICE_ID
/** Device ID for dip switches 0-3 and button 2, 3 (control LEDs) */
#define CTRL_DEVICE_ID XPAR_REVOLVE_CTRL_DEVICE_ID
/** Device ID for the GIC (Generic Interrupt Controller) in Zynq */
#define INTC_DEVICE_ID XPAR_SCUGIC_SINGLE_DEVICE_ID
/** Device ID for the AXI Timer */
#define TMRCTR_DEVICE_ID XPAR_TMRCTR_0_DEVICE_ID

/** IRQ_F2P[0:0] */
#define BTN_0_INTR_ID XPAR_FABRIC_BTN_0_IP2INTC_IRPT_INTR
/** IRQ_F2P[1:1] */
#define BTN_1_INTR_ID XPAR_FABRIC_BTN_1_IP2INTC_IRPT_INTR
/** IRQ_F2P[2:2] */
#define TMRCTR_INTERRUPT_ID XPAR_FABRIC_AXI_TIMER_0_INTERRUPT_INTR

#define RISING_EDGE 0x3        // Rising edge active trigger
#define ONE_TENTH 32500000     // half of the CPU clock speed/10
#define INPUT_MASK 0xFFFFFFFF  // Mask for input pins
#define OUTPUT_MASK 0x00000000 // Mask for output pins
#define GPIO_CHANNEL_MASK 0x1  // Mask for GPIO channel interrupt

/*
 * The following constant determines which timer counter of the device that is
 * used for this example, there are currently 2 timer counters in a device
 * and this example uses the first one, 0, the timer numbers are 0 based
 */
#define TIMER_CNTR_0 0

/************************** Function Prototypes ******************************/

int GpioInitialize();

void BTN0_Handler(void *CallBackRef);

void BTN1_Handler(void *CallBackRef);

int TmrCtrInitialize();

void TmrCtrHandler(void *CallBackRef, u8 TmrCtrNumber);

/************************** Variable Definitions *****************************/

XGpio btn0, btn1, ctrl, led; // GPIO Driver Instance
XScuGic Intc;                // Interrupt Controller Instance
XScuTimer Timer;             // Cortex A9 SCU Private Timer Instance
XTmrCtr TmrCtr;              // AXI Timer Instance

u32 psb_check, psb_check_prev, dip_check, dip_check_prev;

/**
 * This function is the main function of the GPIO project.  It is responsible
 * for initializing the GPIO device, setting up interrupts and providing a
 * foreground loop such that interrupt can occur in the background.
 *
 * @param	None.
 *
 * @return
 *		- XST_SUCCESS to indicate success.
 *		- XST_FAILURE to indicate failure.
 *
 * @note		None.
 *
 */
int main(void)
{

    int count, Status;
    XScuGic_Config *IntcConfig;
    XScuTimer_Config *ConfigPtr;
    XScuTimer *TimerInstancePtr = &Timer;

    xil_printf("-- Start of the Program --\r\n");

    /*
     * Initialize the exception table and register the interrupt
     * controller handler with the exception table
     */
    Xil_ExceptionInit();

    /*
     * Initialize the interrupt controller driver so that it is ready to
     * use.
     */
    IntcConfig = XScuGic_LookupConfig(INTC_DEVICE_ID);
    if (NULL == IntcConfig)
    {
        return XST_FAILURE;
    }

    Status = XScuGic_CfgInitialize(&Intc, IntcConfig,
                                   IntcConfig->CpuBaseAddress);
    if (Status != XST_SUCCESS)
    {
        return XST_FAILURE;
    }

    /* Enable non-critical exceptions */
    Xil_ExceptionEnable();

    Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,
                                 (Xil_ExceptionHandler)XScuGic_InterruptHandler, &Intc);

    /*
     * Initialize the GPIO drivers
     */
    Status = GpioInitialize();
    if (Status != XST_SUCCESS)
    {
        xil_printf("GPIO initialization failed\r\n");
        return XST_FAILURE;
    }

    /*
     * Initialize the AXI timer
     */
    Status = TmrCtrInitialize();
    if (Status != XST_SUCCESS)
    {
        xil_printf("AXI Timer initialization failed\r\n");
        return XST_FAILURE;
    }

    /*
     * Initialize the SCU timer
     */
    ConfigPtr = XScuTimer_LookupConfig(XPAR_PS7_SCUTIMER_0_DEVICE_ID);
    Status = XScuTimer_CfgInitialize(TimerInstancePtr, ConfigPtr,
                                     ConfigPtr->BaseAddr);
    if (Status != XST_SUCCESS)
    {
        xil_printf("SCU Timer initialization failed\r\n");
        return XST_FAILURE;
    }

    // Read dip switch values
    dip_check_prev = XGpio_DiscreteRead(&ctrl, 1);
    psb_check_prev = XGpio_DiscreteRead(&ctrl, 2);

    // Load timer with delay in multiple of ONE_TENTH
    XScuTimer_LoadTimer(TimerInstancePtr,
                        ONE_TENTH * (dip_check_prev + psb_check_prev));

    // Set AutoLoad mode
    XScuTimer_EnableAutoReload(TimerInstancePtr);

    // Start the timer
    XScuTimer_Start(TimerInstancePtr);

    count = 0;

    // Main loop
    while (1)
    {
        // Read led control switches & buttons
        dip_check = XGpio_DiscreteRead(&ctrl, 1);
        psb_check = XGpio_DiscreteRead(&ctrl, 2);
        if (dip_check != dip_check_prev || psb_check != psb_check_prev)
        {
            xil_printf("DIP Switch Status %x, %x\r\n", dip_check_prev, dip_check);
            xil_printf("Push Button Status %x, %x\r\n", psb_check_prev, psb_check);
            dip_check_prev = dip_check;
            psb_check_prev = psb_check;
            // load timer with the new switch settings
            XScuTimer_LoadTimer(TimerInstancePtr,
                                ONE_TENTH * (dip_check + psb_check));
            count = 0;
        }
        if (XScuTimer_IsExpired(TimerInstancePtr))
        {
            // clear status bit
            XScuTimer_ClearInterruptStatus(TimerInstancePtr);
            // output the count to LED and increment the count
            XGpio_DiscreteWrite(&led, 1, count);
            count++;
        }
    }
    return 0;
}

/**
 *
 * This function performs the GPIO set up for Interrupts
 *
 * @param	IntcInstancePtr is a reference to the Interrupt Controller
 *		driver Instance
 * @param	InstancePtr is a reference to the GPIO driver Instance
 * @param	DeviceId is the XPAR_<GPIO_instance>_DEVICE_ID value from
 *		xparameters.h
 * @param	IntrId is XPAR_<INTC_instance>_<GPIO_instance>_IP2INTC_IRPT_INTR
 *		value from xparameters.h
 * @param	IntrMask is the GPIO channel mask
 *
 * @return	XST_SUCCESS if the Test is successful, otherwise XST_FAILURE
 *
 * @note		None.
 *
 */
int GpioInitialize()
{
    int Result;

    /**
     * Initialize GPIO drivers. If an error occurs then exit
     */
    xil_printf(" Initializing GPIO drivers...\r\n");

    Result = XGpio_Initialize(&btn0, BTN0_DEVICE_ID);
    if (Result != XST_SUCCESS)
    {
        xil_printf(" Button 0 initialization failed\r\n");
        return XST_FAILURE;
    }
    Result = XGpio_Initialize(&btn1, BTN1_DEVICE_ID);
    if (Result != XST_SUCCESS)
    {
        xil_printf(" Button 1 initialization failed\r\n");
        return XST_FAILURE;
    }
    Result = XGpio_Initialize(&ctrl, CTRL_DEVICE_ID);
    if (Result != XST_SUCCESS)
    {
        xil_printf(" Revolve controlling pins initialization failed\r\n");
        return XST_FAILURE;
    }
    Result = XGpio_Initialize(&led, LED_DEVICE_ID);
    if (Result != XST_SUCCESS)
    {
        xil_printf(" LED initialization failed\r\n");
        return XST_FAILURE;
    }

    XGpio_SetDataDirection(&btn0, 1, INPUT_MASK);
    XGpio_SetDataDirection(&btn1, 1, INPUT_MASK);
    XGpio_SetDataDirection(&ctrl, 1, INPUT_MASK);
    XGpio_SetDataDirection(&ctrl, 2, INPUT_MASK);
    XGpio_SetDataDirection(&led, 1, OUTPUT_MASK);

    /**
     * Initialize GPIO Interrupts. If an error occurs then exit
     */
    XScuGic_SetPriorityTriggerType(&Intc, BTN_0_INTR_ID, 0xA0, RISING_EDGE);
    XScuGic_SetPriorityTriggerType(&Intc, BTN_1_INTR_ID, 0xA0, RISING_EDGE);

    /*
     * Connect the interrupt handler that will be called when an
     * interrupt occurs for the device.
     */
    Result = XScuGic_Connect(&Intc, BTN_0_INTR_ID,
                             (Xil_ExceptionHandler)BTN0_Handler, &btn0);
    if (Result != XST_SUCCESS)
    {
        return Result;
    }
    Result = XScuGic_Connect(&Intc, BTN_1_INTR_ID,
                             (Xil_ExceptionHandler)BTN1_Handler, &btn1);
    if (Result != XST_SUCCESS)
    {
        return Result;
    }

    /* Enable the interrupt for the GPIO device.*/
    XScuGic_Enable(&Intc, BTN0_DEVICE_ID);
    XScuGic_Enable(&Intc, BTN1_DEVICE_ID);

    /*
     * Enable the GPIO channel interrupts so that push button can be
     * detected and enable interrupts for the GPIO device
     */
    XGpio_InterruptEnable(&btn0, GPIO_CHANNEL_MASK);
    XGpio_InterruptEnable(&btn1, GPIO_CHANNEL_MASK);
    XGpio_InterruptGlobalEnable(&btn0);
    XGpio_InterruptGlobalEnable(&btn1);

    xil_printf(" GPIO drivers initialized successfully.\r\n");

    return XST_SUCCESS;
}

/**
 *
 * This is the interrupt handler routine for BTN0. It is called when an
 * interrupt has been detected from button 0 being pressed. The handler
 * export a message to the UART terminal, and then disables the interrupt for
 * 1 second before re-enabling it.
 *
 * @param	CallbackRef is the Callback reference for the handler.
 *
 * @return	None.
 *
 * @note		None.
 *
 */
void BTN0_Handler(void *CallbackRef)
{
    XGpio *GpioPtr = (XGpio *)CallbackRef;

    /* Clear and disable the Interrupt */
    XGpio_InterruptClear(GpioPtr, GPIO_CHANNEL_MASK);
    XGpio_InterruptGlobalDisable(GpioPtr);
    XGpio_InterruptGlobalDisable(&btn1);

    /* Export message to UART terminal */
    xil_printf("Button 0 pressed\r\n");

    /* Restart the AXI timer for interrupt blocking */
    XTmrCtr_Stop(&TmrCtr, 0);
    XTmrCtr_Reset(&TmrCtr, 0);
    XTmrCtr_Start(&TmrCtr, 0);
}

/**
 *
 * This is the interrupt handler routine for BTN1. It is called when an
 * interrupt has been detected from button 1 being pressed. The handler
 * export a message to the UART terminal, and then disables the interrupt for
 * 1 second before re-enabling it.
 *
 * @param	CallbackRef is the Callback reference for the handler.
 *
 * @return	None.
 *
 * @note		None.
 *
 */
void BTN1_Handler(void *CallbackRef)
{
    XGpio *GpioPtr = (XGpio *)CallbackRef;

    /* Clear and disable the Interrupt */
    XGpio_InterruptClear(GpioPtr, GPIO_CHANNEL_MASK);
    XGpio_InterruptGlobalDisable(GpioPtr);
    XGpio_InterruptGlobalDisable(&btn0);

    /* Export message to UART terminal */
    xil_printf("Button 1 pressed\r\n");

    /* Restart the AXI timer for interrupt blocking */
    XTmrCtr_Stop(&TmrCtr, 0);
    XTmrCtr_Reset(&TmrCtr, 0);
    XTmrCtr_Start(&TmrCtr, 0);
}

/**
 * This function setups the interrupt system such that interrupts can occur
 * for the timer counter.
 *
 * @param	None.
 *
 * @return	XST_SUCCESS if the AXI Timer device was successfully initialized,
 *		otherwise XST_FAILURE.
 *
 * @note		None.
 *
 */
int TmrCtrInitialize()
{
    int Status;

    xil_printf(" Initializing AXI Timer...\r\n");

    /*
     * Initialize the timer counter so that it's ready to use,
     * specify the device ID that is generated in xparameters.h
     */
    Status = XTmrCtr_Initialize(&TmrCtr, TMRCTR_DEVICE_ID);
    if (Status != XST_SUCCESS)
    {
        xil_printf(" AXI Timer initialization failed\r\n");
        return XST_FAILURE;
    }

    XScuGic_SetPriorityTriggerType(&Intc, TMRCTR_INTERRUPT_ID,
                                   0xA0, RISING_EDGE);

    /*
     * Connect the interrupt handler that will be called when an
     * interrupt occurs for the device.
     */
    Status = XScuGic_Connect(&Intc, TMRCTR_INTERRUPT_ID,
                             (Xil_ExceptionHandler)XTmrCtr_InterruptHandler,
                             &TmrCtr);
    if (Status != XST_SUCCESS)
    {
        return Status;
    }

    /*
     * Enable the interrupt for the Timer device.
     */
    XScuGic_Enable(&Intc, TMRCTR_INTERRUPT_ID);

    /*
     * Setup the handler for the timer counter that will be called from the
     * interrupt context when the timer expires, specify a pointer to the
     * timer counter driver instance as the callback reference so the
     * handler is able to access the instance data
     */
    XTmrCtr_SetHandler(&TmrCtr, TmrCtrHandler, &TmrCtr);

    /*
     * Enable the interrupt of the timer counter so interrupts will occur
     * and use auto reload mode such that the timer counter will reload
     * itself automatically and continue repeatedly, without this option
     * it would expire once only
     */
    XTmrCtr_SetOptions(&TmrCtr, TIMER_CNTR_0,
                       XTC_INT_MODE_OPTION | XTC_AUTO_RELOAD_OPTION);

    /*
     * Set a reset value for the timer counter such that it will expire
     * eariler than letting it roll over from 0, the reset value is loaded
     * into the timer counter when it is started
     */
    XTmrCtr_SetResetValue(&TmrCtr, TIMER_CNTR_0, COUNTS_PER_SECOND);

    xil_printf(" AXI Timer initialized successfully.\r\n");

    return XST_SUCCESS;
}

/**
 * This function is called when the timer counter expires if interrupts are
 * enabled.
 *
 * This handler provides an example of how to handle timer counter interrupts
 * but is application specific.
 *
 * @param	CallBackRef is a pointer to the callback function
 * @param	TmrCtrNumber is the number of the timer to which this
 *		handler is associated with.
 *
 * @return	None.
 *
 * @note		None.
 *
 */
void TmrCtrHandler(void *CallBackRef, u8 TmrCtrNumber)
{
    xil_printf(" Button 0 and 1 interrupt re-enabled\r\n");

    /* Enable the GPIO channel interrupts */
    XGpio_InterruptGlobalEnable(&btn0);
    XGpio_InterruptGlobalEnable(&btn1);
}
