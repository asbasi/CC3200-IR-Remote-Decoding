// **********************************************************
// Arvinder Basi & Kelvin Lu
// EEC 172 - Embedded Systems
// Lab Assignment 3: IR Remote Communication
// 2/9/2016
// **********************************************************

// Standard includes
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

// Driverlib includes
#include "hw_types.h"
#include "hw_memmap.h"
#include "hw_common_reg.h"
#include "hw_ints.h"
#include "spi.h"
#include "rom.h"
#include "rom_map.h"
#include "utils.h"
#include "prcm.h"
#include "gpio.h"
#include "uart.h"
#include "interrupt.h"
#include "systick.h"
#include "timer.h"

// Common interface includes
#include "gpio_if.h"
#include "uart_if.h"
#include "timer_if.h"
#include "pin_mux_config.h"

#include "Adafruit_SSD1351.h"
#include "glcdfont.h"
#include "Adafruit_GFX.h"
#include "test.h"

#define APPLICATION_VERSION     "1.1.1"
#define SPI_IF_BIT_RATE  100000
#define TR_BUFF_SIZE     100

#define CONSOLE              		UARTA0_BASE
#define PAIRDEV              		UARTA1_BASE
#define PAIRDEV_PERIPH  		PRCM_UARTA1

#define UartConsoleGetChar()        MAP_UARTCharGet(CONSOLE)
#define UartConsolePutChar(c)       MAP_UARTCharPut(CONSOLE,c)
#define UartPairDevGetChar()        MAP_UARTCharGet(PAIRDEV)
#define UartPairDevPutChar(c)       MAP_UARTCharPut(PAIRDEV,c)

typedef void (*P_INT_HANDLER)(void);

//*****************************************************************************
//                 GLOBAL VARIABLES -- Start
//*****************************************************************************
#if defined(ccs)
extern void (* const g_pfnVectors[])(void);
#endif
#if defined(ewarm)
extern uVectorEntry __vector_table;
#endif
//*****************************************************************************
//                 GLOBAL VARIABLES -- End
//*****************************************************************************

//*****************************************************************************
//
//! Board Initialization & Configuration
//!
//! \param  None
//!
//! \return None
//
//*****************************************************************************
static void
BoardInit(void)
{
/* In case of TI-RTOS vector table is initialize by OS itself */
#ifndef USE_TIRTOS
  //
  // Set vector table base
  //
#if defined(ccs)
    MAP_IntVTableBaseSet((unsigned long)&g_pfnVectors[0]);
#endif
#if defined(ewarm)
    MAP_IntVTableBaseSet((unsigned long)&__vector_table);
#endif
#endif
    //
    // Enable Processor
    //
    MAP_IntMasterEnable();
    MAP_IntEnable(FAULT_SYSTICK);

    PRCMCC3200MCUInit();
}

//*****************************************************************************
// Interrupt Handlers & Global variable declarations
//*****************************************************************************
#define SYSTICK_WRAP_TICKS		16777216 // 2^24

#define SYS_CLK				80000000
#define MILLISECONDS_TO_TICKS(ms)   ((SYS_CLK/1000) * (ms))
#define TICKS_TO_MILLISECONDS(ts)   (((double) 1000/(double) SYS_CLK) * (double) (ts))


#define NO_PROCESSING_MODE 	0
#define	SHORT_PROCESSING_MODE 	1
#define LONG_PROCESSING_MODE 	2

#define EDGE_TYPE_FALLING 	0
#define EDGE_TYPE_RISING 	1

void PhotoresistorIntDisable();
void EdgeTriggerHandler();
void PhotoresistorIntInit();
void PhotoresistorIntEnable();
void PhotoresistorIntDisable();
void SystickInit();
void SystickReset();
unsigned long tickDiff();
void TransitionTimerIntStart(unsigned long);
void FinishTimerIntStart(unsigned long);
void TransitionTimerIntStop();
void FinishTimerIntStop();
void TransitionTimerIntHandler();
void FinishTimerIntHandler();
void RepeatTimerIntHandler();
void clearSender();
void clearReceiver();

unsigned int 		g_signalMode 	= NO_PROCESSING_MODE;
unsigned int 		g_finishMode 	= NO_PROCESSING_MODE;
unsigned long 		g_ticksLast 	= SYSTICK_WRAP_TICKS;
unsigned long 		g_ticksElapsed 	= 0UL;
unsigned long long 	g_bit_rep 	= 0ULL;
unsigned char 		g_last_toggle	= 0;
unsigned long long 	g_last_bit_rep	= 0ULL;
unsigned char		g_repeat        = 0;
unsigned char 		g_send_key	= 0;

int send_x_cursor = 6;
int send_y_cursor = 0;
int rec_x_cursor = 12;
int rec_y_cursor = 80;

bool rec_new_message = 0;

void SystickInit()
{
	SysTickPeriodSet(SYSTICK_WRAP_TICKS);
	SysTickIntRegister(SystickReset);
	SysTickEnable();
}

void SystickReset()
{
	SysTickPeriodSet(SYSTICK_WRAP_TICKS);
	SysTickEnable();
}

unsigned long tickDiff()
{
	// Note that systick counts down, so lesser tick values
	// represent a succeeding point in time

	unsigned long ticksCurrent = SysTickValueGet();
	unsigned long ticksLast = g_ticksLast;

	// Set up for next call
	g_ticksLast = ticksCurrent;

	// No wrap; most probable case, so we short circuit a branch here
	if (ticksCurrent < ticksLast) return ticksLast - ticksCurrent;

	// Wrapped ticks. We assume a max wrap of SYSTICK_MAX_TICKS.
	// The order of operations here is important to prevent arithmetic overflow.
	return (SYSTICK_WRAP_TICKS - ticksCurrent) + ticksLast;
}

void EdgeTriggerHandler()
{
	PhotoresistorIntDisable();

	if (!GPIOPinRead(GPIOA1_BASE,GPIO_PIN_0))
	{
		// Falling edges
		switch (g_signalMode)
		{
			case NO_PROCESSING_MODE:
				g_bit_rep = 0ULL;
				g_signalMode = SHORT_PROCESSING_MODE;
				TransitionTimerIntStart(3);
				break;
			case SHORT_PROCESSING_MODE:
				TransitionTimerIntStop();
				FinishTimerIntStart(3);
				g_ticksElapsed = tickDiff();
				g_bit_rep <<= 2;
				if (g_ticksElapsed > 50000) g_bit_rep |= 3ULL;
				else if (g_ticksElapsed > 37500) g_bit_rep |= 2ULL;
				else if (g_ticksElapsed > 25000) g_bit_rep |= 1ULL;
				break;
			case LONG_PROCESSING_MODE:
				TransitionTimerIntStop();
				FinishTimerIntStart(5);
				g_ticksElapsed = tickDiff();
				g_bit_rep <<= 1;
				g_bit_rep |= g_ticksElapsed > 90000 ? 1ULL : 0ULL;
				break;
			default:
				break;
		}
	} else {
		// Rising edges
		g_ticksLast = SysTickValueGet();
	}

	PhotoresistorIntEnable();
}

void PhotoresistorIntInit()
{
	GPIOIntTypeSet(GPIOA1_BASE,GPIO_PIN_0,GPIO_BOTH_EDGES);
	GPIOIntRegister(GPIOA1_BASE, EdgeTriggerHandler);

	IntPrioritySet(INT_GPIOA1, INT_PRIORITY_LVL_3);

	GPIOIntClear(GPIOA1_BASE,GPIO_PIN_0);
	GPIOIntEnable(GPIOA1_BASE,GPIO_INT_PIN_0);
}

void PhotoresistorIntEnable()
{
    //Enable GPIO Interrupt
    GPIOIntClear(GPIOA1_BASE,GPIO_PIN_0);
    IntPendClear(INT_GPIOA1);
    IntEnable(INT_GPIOA1);
    GPIOIntEnable(GPIOA1_BASE,GPIO_PIN_0);
}

void PhotoresistorIntDisable()
{
    //Clear and Disable GPIO Interrupt
    GPIOIntDisable(GPIOA1_BASE,GPIO_PIN_0);
    GPIOIntClear(GPIOA1_BASE,GPIO_PIN_0);
    IntDisable(INT_GPIOA1);
}

void AllTimersIntInit()
{
    Timer_IF_Init(PRCM_TIMERA0, TIMERA0_BASE, TIMER_CFG_A_ONE_SHOT | TIMER_CFG_B_ONE_SHOT, TIMER_BOTH, 0);
    Timer_IF_IntSetup(TIMERA0_BASE, TIMER_BOTH, TransitionTimerIntHandler);

    Timer_IF_Init(PRCM_TIMERA1, TIMERA1_BASE, TIMER_CFG_A_ONE_SHOT | TIMER_CFG_B_ONE_SHOT, TIMER_BOTH, 0);
    Timer_IF_IntSetup(TIMERA1_BASE, TIMER_BOTH, FinishTimerIntHandler);

    Timer_IF_Init(PRCM_TIMERA2, TIMERA2_BASE, TIMER_CFG_A_ONE_SHOT | TIMER_CFG_B_ONE_SHOT, TIMER_BOTH, 0);
    Timer_IF_IntSetup(TIMERA2_BASE, TIMER_BOTH, RepeatTimerIntHandler);
}

void TransitionTimerIntStart(unsigned long ms)
{
    Timer_IF_Start(TIMERA0_BASE, TIMER_BOTH, ms); // in milliseconds, not ticks.
}

void FinishTimerIntStart(unsigned long ms)
{
    Timer_IF_Start(TIMERA1_BASE, TIMER_BOTH, ms); // in milliseconds, not ticks.
}

void RepeatTimerIntStart(unsigned long ms)
{
    Timer_IF_Start(TIMERA2_BASE, TIMER_BOTH, ms); // in milliseconds, not ticks.
}

void TransitionTimerIntStop()
{
	Timer_IF_Stop(TIMERA0_BASE, TIMER_BOTH);
}

void FinishTimerIntStop()
{
	Timer_IF_Stop(TIMERA1_BASE, TIMER_BOTH);
}

void RepeatTimerIntStop()
{
	Timer_IF_Stop(TIMERA2_BASE, TIMER_BOTH);
}

void TransitionTimerIntHandler()
{
    Timer_IF_InterruptClear(TIMERA0_BASE);

    switch (g_signalMode)
	{
		case SHORT_PROCESSING_MODE:
			g_signalMode = LONG_PROCESSING_MODE;
			g_bit_rep = 0ULL;
			TransitionTimerIntStart(15);
			break;
		case LONG_PROCESSING_MODE:
			// Something is wrong after 3+15=18 ms!
			g_signalMode = NO_PROCESSING_MODE;
			FinishTimerIntStop();
			break;
	}
}

void FinishTimerIntHandler()
{
    Timer_IF_InterruptClear(TIMERA1_BASE);

    g_finishMode = g_signalMode;
	g_signalMode = NO_PROCESSING_MODE;
	g_ticksElapsed = 0UL;
}

void RepeatTimerIntHandler()
{
    Timer_IF_InterruptClear(TIMERA2_BASE);

    g_repeat = 0;
	g_send_key = 1;
}

void UARTIntHandler()
{
	unsigned char cChar;
	MAP_UARTIntDisable(UARTA1_BASE,UART_INT_RX);

	while(UARTCharsAvail(PAIRDEV))
	{
		cChar = UartPairDevGetChar();
		if(rec_new_message)
		{
			clearReceiver();
			rec_new_message = 0;
		}

		if(rec_x_cursor > 120) // About to go off right edge.
		{
			rec_x_cursor = 12;
			rec_y_cursor += 8; // go to next line.
		}

		if(rec_y_cursor > 120) // About to go off bottom edge.
		{
			clearReceiver(); // clear screen.
			rec_x_cursor = 12;
			rec_y_cursor = 80; // start at top.
		}
		else if(cChar == '\0')
		{
			rec_new_message = 1;
			rec_x_cursor = 12;
			rec_y_cursor = 80;
		}
		else
		{
			drawChar(rec_x_cursor, rec_y_cursor, cChar, BLUE, BLACK, (unsigned char) 1);
			rec_x_cursor += 6;
		}
	}
	MAP_UARTIntClear(UARTA1_BASE,UART_INT_RX);
	MAP_UARTIntEnable(UARTA1_BASE,UART_INT_RX);
}
//*****************************************************************************
//                      LOCAL FUNCTION DEFINITIONS
//*****************************************************************************

void DebugRoutine()
{
	unsigned long risingTickWidths[100];
	unsigned long i = 0;

	while (1)
	{
		i = 0;

		while (i < 100)
			if (g_ticksElapsed)
			{
				risingTickWidths[i] = g_ticksElapsed;
				++i;
				g_ticksElapsed = 0UL;
			}

		for (i = 0; i < 100; ++i)
			Report("%d: %lu ticks\n\r", i + 1, risingTickWidths[i]);
	}
}

unsigned long long SignalRoutine()
{
	unsigned long long bit_rep;
	unsigned char toggle_bit;

	while(!g_finishMode);

	switch (g_finishMode)
	{
		case SHORT_PROCESSING_MODE:
			// Get toggle bit
			toggle_bit = (g_bit_rep & 0x8000ULL) >> 15;
			// Always set the 16th least-significant bit, the toggle bit, to high
			g_bit_rep |= 0x8000ULL;
			// Only keep the 32+4=36 least significant bits
			g_bit_rep &= 0xFFFFFFFFFULL;
			break;
		case LONG_PROCESSING_MODE:
			// Only keep the 32 least significant bits
			g_bit_rep &= 0xFFFFFFFFULL;
			break;
	}

	g_finishMode = NO_PROCESSING_MODE;

	bit_rep = g_bit_rep;
	g_bit_rep = 0ULL;

	if (bit_rep == g_last_bit_rep && toggle_bit == g_last_toggle)
		return 0ULL;

	g_last_bit_rep = bit_rep;
	g_last_toggle = toggle_bit;

	return bit_rep;
}

typedef enum
{
	BUTTON_INVALID = 0,

	BUTTON_1,
	BUTTON_2,
	BUTTON_3,
	BUTTON_4,
	BUTTON_5,
	BUTTON_6,
	BUTTON_7,
	BUTTON_8,
	BUTTON_9,
	BUTTON_0,

	BUTTON_UP,
	BUTTON_DOWN,
	BUTTON_LEFT,
	BUTTON_RIGHT,

	BUTTON_CHANNEL_UP,
	BUTTON_CHANNEL_DOWN,

	BUTTON_MUTE,
} button_type;

button_type ButtonRoutine()
{
	unsigned long long bit_rep = SignalRoutine();

	switch (bit_rep)
	{
		case 591439360ULL: return BUTTON_0;
		case 591439361ULL: return BUTTON_1;
		case 591439362ULL: return BUTTON_2;
		case 591439363ULL: return BUTTON_3;
		case 591439364ULL: return BUTTON_4;
		case 591439365ULL: return BUTTON_5;
		case 591439366ULL: return BUTTON_6;
		case 591439367ULL: return BUTTON_7;
		case 591439368ULL: return BUTTON_8;
		case 591439369ULL: return BUTTON_9;

		case 50190375ULL: return BUTTON_UP;
		case 50198535ULL: return BUTTON_DOWN;
		case 50165895ULL: return BUTTON_LEFT;
		case 50157735ULL: return BUTTON_RIGHT;

		case 591439392ULL: return BUTTON_CHANNEL_UP;
		case 591439393ULL: return BUTTON_CHANNEL_DOWN;

		case 50137335ULL: return BUTTON_MUTE;

		default: return BUTTON_INVALID;
	}
}

typedef struct button_press
{
	button_type 	button;
	unsigned char 	control;
	unsigned char 	cycle;
	unsigned char 	ascii;
} button_press;

button_type	g_last_type = BUTTON_INVALID;

button_press PressRoutine()
{
	button_press press;
	button_type button = ButtonRoutine();

	press.button = button;

	if (!button) return press;

	switch (button)
	{
		case BUTTON_1:
		case BUTTON_2:
		case BUTTON_3:
		case BUTTON_4:
		case BUTTON_5:
		case BUTTON_6:
		case BUTTON_7:
		case BUTTON_8:
		case BUTTON_9:
		case BUTTON_0:
		case BUTTON_CHANNEL_UP:
		case BUTTON_CHANNEL_DOWN:
			press.control = 0;
			break;
		default:
			press.control = 1;
			break;
	}

	if(g_last_type == button)
	{
		press.cycle = g_repeat;
		++g_repeat;
	}
	else
	{
		press.cycle = 0;
		g_repeat = 1;
		g_last_type = button;
		g_send_key = 1;
	}
	RepeatTimerIntStart(1000);

	// Do not read from flag g_repeat after this point.
	// Use press.cycle instead.

	switch (button)
	{
		case BUTTON_1:
			press.ascii = (unsigned char[]){'.', ',', '?', '!', '@', '#', '$', '&', '1'}[press.cycle % 9];
			break;
		case BUTTON_2:
			press.ascii = (unsigned char[]){'a', 'b', 'c', 'A', 'B', 'C', '2'}[press.cycle % 7];
			break;
		case BUTTON_3:
			press.ascii = (unsigned char[]){'d', 'e', 'f', 'D', 'E', 'F', '3'}[press.cycle % 7];
			break;
		case BUTTON_4:
			press.ascii = (unsigned char[]){'g', 'h', 'i', 'G', 'H', 'I', '4'}[press.cycle % 7];
			break;
		case BUTTON_5:
			press.ascii = (unsigned char[]){'j', 'k', 'l', 'J', 'K', 'I', '5'}[press.cycle % 7];
			break;
		case BUTTON_6:
			press.ascii = (unsigned char[]){'m', 'n', 'o', 'M', 'N', 'O', '6'}[press.cycle % 7];
			break;
		case BUTTON_7:
			press.ascii = (unsigned char[]){'p', 'q', 'r', 's', 'P', 'Q', 'R', 'S', '7'}[press.cycle % 9];
			break;
		case BUTTON_8:
			press.ascii = (unsigned char[]){'t', 'u', 'v', 'T', 'U', 'V', '8'}[press.cycle % 7];
			break;
		case BUTTON_9:
			press.ascii = (unsigned char[]){'w', 'x', 'y', 'z', 'W', 'X', 'Y', 'Z', '9'}[press.cycle % 9];
			break;
		case BUTTON_0:
			press.ascii = (unsigned char[]){' ', '0'}[press.cycle % 2];
			break;
		case BUTTON_CHANNEL_UP:
			press.ascii = '\n';
			break;
		case BUTTON_CHANNEL_DOWN:
			press.ascii = '\b';
			break;
	}

	return press;
}

void DemoRoutine()
{
	button_press p;

	while (1)
	{
		// Remember that the underlying representation of enum values
		// are implementation specific. It's usually an int, though.
		p = PressRoutine();

		if (p.cycle) Message("\r");
		else Message("\n");
		if (p.button && !p.control) Report("%c", (char) p.ascii);
	}
}

void clearSender()
{
	fillRect(12, 0, SSD1351WIDTH, SSD1351HEIGHT / 2, BLACK);
}

void clearReceiver()
{
	fillRect(12, 80, SSD1351WIDTH, SSD1351HEIGHT, BLACK);
}

void DisplayRoutine()
{
	unsigned char cChar;
	button_press curr;

	unsigned char sendBuffer[100] = {0};
	unsigned int i;
	unsigned int send_index = 0;

	// As you write a character to the terminal it will
	// immeditely be reflected on the recievers OLED display.
	while(1)
	{
		curr = PressRoutine();
		PhotoresistorIntDisable();
		if(curr.button != BUTTON_INVALID)
		{
			cChar = curr.ascii;

			if(curr.cycle != 0 && cChar != '\b' && cChar != '\n') // Multiple presses.
			{
				drawChar(send_x_cursor, send_y_cursor, cChar, RED, BLACK, (unsigned char) 1);
				sendBuffer[send_index] = cChar;
			}
			else
			{
				if(cChar == '\n') // end of message.
				{
					sendBuffer[++send_index] = '\0';
					clearSender();
					send_x_cursor = 6;
					send_y_cursor = 0;

					for(i = 0; i <= send_index; i++)
					{
						UartPairDevPutChar(sendBuffer[i]);
						MAP_UtilsDelay(500);
					}
					send_index = 0;
				}

				else if(cChar == '\b') 	// deleting a character.
				{
					drawChar(send_x_cursor, send_y_cursor, ' ', WHITE, BLACK, (unsigned char) 1);
					if(send_index >= 1) send_index--;

					send_x_cursor -= 6; 	// go back one
					if(send_x_cursor < 6) 	// if we're about to go off screen.
					{
						send_y_cursor -= 8; 	// previous line.
						send_x_cursor = 120; // last character of line.

						if(send_y_cursor < 0) // Went off top edge.
						{
							send_x_cursor = 6;
							send_y_cursor = 0;
						}
					}
				}
				else
				{
					send_x_cursor += 6;

					if(send_x_cursor > 120) // About to go off right edge.
					{
						send_x_cursor = 12;
						send_y_cursor += 8; // go to next line.
					}

					if(send_y_cursor > 56) // About to go off bottom edge.
					{
						clearSender(); // clear screen.
						send_x_cursor = 12;
						send_y_cursor = 0; // start at top.
					}

					drawChar(send_x_cursor, send_y_cursor, cChar, RED, BLACK, (unsigned char) 1);
					sendBuffer[++send_index] = cChar;
				}
			}
		}
		PhotoresistorIntEnable();
	}
}

//*****************************************************************************
//
//! Main function for lab application
//!
//! \param none
//!
//! \return None.
//
//*****************************************************************************

void main()
{
    //
    // Initialize Board configurations
    //
    BoardInit();

    //
    // Muxing UART and SPI lines.
    //
    PinMuxConfig();

    //
    // Enable the SPI module clock
    //
    PRCMPeripheralClkEnable(PRCM_GSPI,PRCM_RUN_MODE_CLK);

    //
    // Initialising the Terminal.
    //
    InitTerm();

    //
    // Clearing the Terminal.
    //
    ClearTerm();

    //
    // Initialize the device on UART1
    //
    MAP_UARTIntRegister(UARTA1_BASE,UARTIntHandler);
    MAP_UARTIntEnable(UARTA1_BASE,UART_INT_RX);
    MAP_UARTFIFOLevelSet(UARTA1_BASE,UART_FIFO_TX1_8,UART_FIFO_RX1_8);

    MAP_UARTConfigSetExpClk(PAIRDEV,MAP_PRCMPeripheralClockGet(PAIRDEV_PERIPH),
                      UART_BAUD_RATE, (UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE |
                       UART_CONFIG_PAR_NONE));

    //
    // Reset the SPI peripheral
    //
    PRCMPeripheralReset(PRCM_GSPI);

    // SPI reset, initialize
    SPIReset(GSPI_BASE);
    SPIFIFOEnable(GSPI_BASE, SPI_TX_FIFO || SPI_RX_FIFO);
    SPIConfigSetExpClk(GSPI_BASE, PRCMPeripheralClockGet(PRCM_GSPI),
    				   SPI_IF_BIT_RATE, SPI_MODE_MASTER, SPI_SUB_MODE_0,
					   (SPI_SW_CTRL_CS | SPI_4PIN_MODE | SPI_TURBO_OFF |
					    SPI_CS_ACTIVELOW | SPI_WL_8));

    SPIEnable(GSPI_BASE);

    // Adafruit initialize
    Adafruit_Init();

    fillScreen(BLACK);
    setTextColor(RED,BLACK);
    setCursor(0, 0); Outstr("Kelvin Lu");
    setCursor(0, 8); Outstr("Arvinder Basi");
    setCursor(0, 16); Outstr("EEC 172");
    setCursor(0, 24); Outstr("Lab 3, 2/9/2016");
    setCursor(0, 40); Outstr("IR Communication");
    MAP_UtilsDelay(20000000);

    fillScreen(BLACK);
    drawFastHLine(0, 64, 128, WHITE);
    drawChar(0, 0, '>', RED, BLACK, (unsigned char) 1);
    drawChar(6, 0, ' ', RED, BLACK, (unsigned char) 1);
    drawChar(0, 80, '>', BLUE, BLACK, (unsigned char) 1);
    drawChar(6, 80, ' ', BLUE, BLACK, (unsigned char) 1);

    SystickInit();

    PhotoresistorIntInit();
    PhotoresistorIntEnable();

    AllTimersIntInit();

    DisplayRoutine();
}

