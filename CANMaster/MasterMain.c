#include <stdbool.h>
#include <stdint.h>
#include <math.h>

//testing commit

#include "inc/hw_memmap.h"
#include "inc/hw_types.h"
#include "inc/hw_can.h"
#include "inc/hw_ints.h"
#include "driverlib/can.h"
#include "driverlib/interrupt.h"
#include "driverlib/sysctl.h"
#include "driverlib/gpio.h"
#include "driverlib/uart.h"
#include "driverlib/pin_map.h"
#include "driverlib/adc.h"
#include "driverlib/rom.h"
#include "utils/uartstdio.h"

volatile bool errFlag = 0; // Transmission error flag

unsigned int sysClock; // Clock speed in Hz

// Delay function (milliseconds)
void delay(unsigned int milliseconds) {
    ROM_SysCtlDelay((sysClock / 3) * (milliseconds / 1000.0f));
}

// CAN interrupt handler
void CANIntHandler(void) {

    unsigned long status = ROM_CANIntStatus(CAN0_BASE, CAN_INT_STS_CAUSE); // read interrupt status

    if(status == CAN_INT_INTID_STATUS) { // controller status interrupt
        status = ROM_CANStatusGet(CAN0_BASE, CAN_STS_CONTROL); // read back error bits, do something with them?
        errFlag = 1;
    } else if(status == 1) { // message object 1
        ROM_CANIntClear(CAN0_BASE, 1); // clear interrupt
        errFlag = 0; // clear any error flags
    } else { // should never happen
        UARTprintf("Unexpected CAN bus interrupt\n");
    }
}

int main(void) {

    const char maxTorque = 166;

    uint32_t ADC0Value[8];
    volatile int32_t CH0Avg;
    volatile int32_t CH1Avg;
    volatile int32_t pot1;
    volatile int32_t pot2;

    tCANMsgObject msg; // CAN message object
    uint8_t msgData[8u] = {0, 0, 0, 0, 1, 0b10000000, 0, 0}; // The CAN message is 8 bytes long
    int16_t *msgDataPtr = (int16_t *)&msgData; // make a pointer to msgData so we can access 2 bytes at a time

    // Run from the PLL at 50 MHz.
    ROM_SysCtlClockSet(SYSCTL_SYSDIV_4 | SYSCTL_USE_PLL | SYSCTL_XTAL_16MHZ | SYSCTL_OSC_MAIN);
    sysClock = ROM_SysCtlClockGet();

    // Enable ADC
    ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0);
    // Wait for the ADC0 module to be ready.
    while(!ROM_SysCtlPeripheralReady(SYSCTL_PERIPH_ADC0));
    // Enable hardware averaging
    ROM_ADCHardwareOversampleConfigure(ADC0_BASE, 64);
    // Enable analog input pin for E3 and E2
    ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
    ROM_GPIOPinTypeADC(GPIO_PORTE_BASE, GPIO_PIN_3|GPIO_PIN_2);
    // Set Sampling Sequence
    ROM_ADCSequenceConfigure(ADC0_BASE, 0, ADC_TRIGGER_PROCESSOR, 0);
    // Configure the first 4 steps to sample pin E3
    ROM_ADCSequenceStepConfigure(ADC0_BASE, 0, 0, ADC_CTL_CH0);
    ROM_ADCSequenceStepConfigure(ADC0_BASE, 0, 1, ADC_CTL_CH0);
    ROM_ADCSequenceStepConfigure(ADC0_BASE, 0, 2, ADC_CTL_CH0);
    ROM_ADCSequenceStepConfigure(ADC0_BASE, 0, 3, ADC_CTL_CH0);
    // Configure the last 4 steps to sample pin E2
    ROM_ADCSequenceStepConfigure(ADC0_BASE, 0, 4, ADC_CTL_CH1);
    ROM_ADCSequenceStepConfigure(ADC0_BASE, 0, 5, ADC_CTL_CH1);
    ROM_ADCSequenceStepConfigure(ADC0_BASE, 0, 6, ADC_CTL_CH1);
    ROM_ADCSequenceStepConfigure(ADC0_BASE, 0, 7, ADC_CTL_CH1|ADC_CTL_IE|ADC_CTL_END);
    // Enable the sequencer and clear any interrupts
    ROM_ADCSequenceEnable(ADC0_BASE, 0);
    ROM_ADCIntClear(ADC0_BASE, 0);

    // Set up debugging UART
    ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA); // enable UART0 GPIO peripheral
    while(!ROM_SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOA));
    ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);
    while(!ROM_SysCtlPeripheralReady(SYSCTL_PERIPH_UART0));
    ROM_GPIOPinConfigure(GPIO_PA0_U0RX);
    ROM_GPIOPinConfigure(GPIO_PA1_U0TX);
    ROM_GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);
    UARTStdioConfig(0, 115200, sysClock); // 115200 baud

    // Set up CAN0
    ROM_GPIOPinConfigure(GPIO_PE4_CAN0RX);
    ROM_GPIOPinConfigure(GPIO_PE5_CAN0TX);
    ROM_GPIOPinTypeCAN(GPIO_PORTE_BASE, GPIO_PIN_4 | GPIO_PIN_5);
    ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_CAN0);
    while(!ROM_SysCtlPeripheralReady(SYSCTL_PERIPH_CAN0));
    ROM_CANInit(CAN0_BASE);
    ROM_CANBitRateSet(CAN0_BASE, sysClock, 500000u);
    CANIntRegister(CAN0_BASE, CANIntHandler); // use dynamic vector table allocation
    ROM_CANIntEnable(CAN0_BASE, CAN_INT_MASTER | CAN_INT_ERROR | CAN_INT_STATUS);
    ROM_IntEnable(INT_CAN0);
    ROM_CANEnable(CAN0_BASE);

    // Set up msg object
    msg.ui32MsgID = 0x0C0;
    msg.ui32MsgIDMask = 0;
    msg.ui32Flags = MSG_OBJ_TX_INT_ENABLE;
    msg.ui32MsgLen = 8u;
    msg.pui8MsgData = msgData;


    while(1) {

        ROM_ADCProcessorTrigger(ADC0_BASE, 0);

        while(!ROM_ADCIntStatus(ADC0_BASE, 0, false));
        ROM_ADCIntClear(ADC0_BASE, 0);
        ROM_ADCSequenceDataGet(ADC0_BASE, 0, ADC0Value);

        CH0Avg = (ADC0Value[0] + ADC0Value[1] + ADC0Value[2] + ADC0Value[3] + 2)/4;
        CH1Avg = (ADC0Value[4] + ADC0Value[5] + ADC0Value[6] + ADC0Value[7] + 2)/4;

        pot1 = (((CH0Avg-16)*maxTorque)/3875)*10;
        pot2 = (((CH1Avg-125)*maxTorque)/3860)*10;

        //msgDataPtr[0] = pot1;
        //msgDataPtr[1] = pot2;

        if(abs(pot1 - pot2) < maxTorque)
            msgDataPtr[0] = (pot1 + pot2 + 1)/2;
        else
            msgDataPtr[0] = 0;

        //UARTprintf("\fPosition 1: %d\n", msgDataPtr[0]); // write pos to UART for debugging
        //UARTprintf("Position 2: %d\n", msgDataPtr[1]); // write pos to UART for debugging
        //UARTprintf("Average:    %d\n", msgDataPtr[3]); // write pos to UART for debugging

        ROM_CANMessageSet(CAN0_BASE, 1, &msg, MSG_OBJ_TYPE_TX); // send as msg object 1

        delay(10); // wait 10ms

        if(errFlag) { // check for errors
            UARTprintf("CAN Bus Error\n");
        }

    }

    return 0;
}
