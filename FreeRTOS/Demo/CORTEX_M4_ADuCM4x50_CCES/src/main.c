/*
 * FreeRTOS Kernel V10.0.0
 * Copyright (C) 2017 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software. If you wish to use our Amazon
 * FreeRTOS name, please do so in a fair use way that does not cause confusion.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * http://www.FreeRTOS.org
 * http://aws.amazon.com/freertos
 *
 * 1 tab == 4 spaces!
 */

/* Standard includes. */
#include <stdio.h>

/* Kernel includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* Eval board includes. */
#include "system_ADuCM4050.h"
#include <drivers/pwr/adi_pwr.h>
#include "common.h"

/* Demo app includes. */
#include "partest.h"
#include "flash.h"
#include "BlockQ.h"
#include "death.h"
#include "PollQ.h"
#include "recmutex.h"
#include "StaticAllocation.h"

#ifdef __GNUC__
/* CCES includes */
#include "adi_initialize.h"
#endif

/* The priorities assigned to the tasks. */
#define mainFLASH_TASK_PRIORITY             ( tskIDLE_PRIORITY + 1 )
#define mainBLOCK_Q_PRIORITY                ( tskIDLE_PRIORITY + 3 )
#define mainQUEUE_POLL_PRIORITY             ( tskIDLE_PRIORITY + 2 )
#define mainCHECK_TASK_PRIORITY             ( tskIDLE_PRIORITY + 4 )
#define mainCREATOR_TASK_PRIORITY           ( tskIDLE_PRIORITY + 3 )

/* The check task uses the sprintf function so requires a little more stack. */
#define mainCHECK_TASK_STACK_SIZE           ( configMINIMAL_STACK_SIZE + 50 )

/* Dimension the buffer used to write the error flag string. */
#define mainMAX_FLAG_STRING_LEN             ( 32 )

/* The time between cycles of the 'check' task. */
#define mainCHECK_DELAY                     ( ( TickType_t ) 5000 / portTICK_PERIOD_MS )

/* Check mask value for interrupt. */
#define mainINTERRUPT_MASK                  ( 0xE0 )

/* The total test rounds that the vCheckTask function would be executed. */
#ifndef mainTEST_ROUNDS
#define mainTEST_ROUNDS		 	        ( 25 )
#endif

/* Error status flag. */
static unsigned int ulErrorFlags = 0;
static unsigned int ulErrorFlags_for_loop = 0;
volatile static unsigned int test_round = 0;
/*-----------------------------------------------------------*/

#if ( configSUPPORT_STATIC_ALLOCATION == 1 )
/* RTOS memory */
static StaticTask_t xIdleTaskTCB;
static StackType_t uxIdleTaskStack[ configMINIMAL_STACK_SIZE ];

void vApplicationGetIdleTaskMemory(StaticTask_t ** ppxIdleTaskTCBBuffer, StackType_t ** ppxIdleTaskStackBuffer, uint32_t * pulIdleTaskStackSize)
{
    *ppxIdleTaskTCBBuffer   = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;
    *pulIdleTaskStackSize   = configMINIMAL_STACK_SIZE;
}

#if ( configUSE_TIMERS == 1 )
/* configUSE_STATIC_ALLOCATION and configUSE_TIMERS are both set to 1, so the
application must provide an implementation of vApplicationGetTimerTaskMemory()
to provide the memory that is used by the Timer service task. */
void vApplicationGetTimerTaskMemory( StaticTask_t **ppxTimerTaskTCBBuffer, StackType_t **ppxTimerTaskStackBuffer, uint32_t *pulTimerTaskStackSize )
{
/* If the buffers to be provided to the Timer task are declared inside this
function then they must be declared static - otherwise they will be allocated on
the stack and so not exists after this function exits. */
static StaticTask_t xTimerTaskTCB;
static StackType_t uxTimerTaskStack[ configTIMER_TASK_STACK_DEPTH ];

	/* Pass out a pointer to the StaticTask_t structure in which the Timer
	task's state will be stored. */
	*ppxTimerTaskTCBBuffer = &xTimerTaskTCB;

	/* Pass out the array that will be used as the Timer task's stack. */
	*ppxTimerTaskStackBuffer = uxTimerTaskStack;

	/* Pass out the size of the array pointed to by *ppxTimerTaskStackBuffer.
	Note that, as the array is necessarily of type StackType_t,
	configMINIMAL_STACK_SIZE is specified in words, not bytes. */
	*pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}
#endif /* configUSE_TIMERS == 1 */
#endif /* configSUPPORT_STATIC_ALLOCATION == 1 */

/*
 * Configure the hardware as necessary to run this demo.
 */
static void prvSetupHardware( void );

/*
 * Checks the status of all the demo tasks then prints a message to terminal
 * IO.  The message will be either PASS or a message that describes which of the
 * standard demo tasks an error has been discovered in.
 */
static void vCheckTask( void *pvParameters );

/*
 * Checks that all the demo application tasks are still executing without error
 * - as described at the top of the file.  Called by vCheckTask().
 */
static void prvCheckOtherTasksAreStillRunning( void );

/*
 * Checks interrupt config in FreeRTOSConfig.h.
 */
static void prvIntConfigCheck( void );

/*-----------------------------------------------------------*/

int main( int argc, char *argv[] )
{
#ifdef __GNUC__
    /* Initialize managed drivers and/or services */
    adi_initComponents();
#endif

    /* Configure the hardware ready to run the demo. */
    prvSetupHardware();

    /* Create a subset of the standard demo tasks. */
    vStartLEDFlashTasks( mainFLASH_TASK_PRIORITY );
    vStartPolledQueueTasks( mainQUEUE_POLL_PRIORITY );
    vStartRecursiveMutexTasks();
    vStartBlockingQueueTasks( mainBLOCK_Q_PRIORITY );
    vStartStaticallyAllocatedTasks();

    /* Start the tasks defined within this file/specific to this demo. */
    xTaskCreate( vCheckTask, "Check", mainCHECK_TASK_STACK_SIZE, NULL, mainCHECK_TASK_PRIORITY, NULL );

    /* The death demo tasks must be started last as the sanity checks performed
	require knowledge of the number of other tasks in the system. */
    vCreateSuicidalTasks( mainCREATOR_TASK_PRIORITY );

    /* Start the scheduler. */
    vTaskStartScheduler();

    /* If all is well then this line will never be reached.  If it is reached
    then it is likely that there was insufficient (FreeRTOS) heap memory space
    to create the idle task.  This may have been trapped by the malloc() failed
    hook function, if one is configured. */
    for( ;; );
}

/*-----------------------------------------------------------*/

static void assignInterruptPriorities(void)
{
    /* Any ISR that calls into the FreeRTOS must change its priority   */
    /* The most likely call is a to xSemaphoreGiveFromISR from many of */
    /* the drivers.                                                    */

    /* The tuning of interrupt priorities is application specific.     */
    /* set Priority for UART0 Interrupt */
#if defined(__CC_ARM)
    NVIC_SetPriority(UART0_EVT_IRQn, (1UL << __NVIC_PRIO_BITS) - 1UL);
#endif
}

/*-----------------------------------------------------------*/

static void prvClockInit ( void )
{
    if(adi_pwr_Init()!= ADI_PWR_SUCCESS)
    {
        DEBUG_MESSAGE("\n Failed to initialize the power service \n");
    }
    if(ADI_PWR_SUCCESS != adi_pwr_SetClockDivider(ADI_CLOCK_HCLK,1))
    {
        DEBUG_MESSAGE("Failed to set ADI_CLOCK_HCLK \n");
    }
    if(ADI_PWR_SUCCESS != adi_pwr_SetClockDivider(ADI_CLOCK_PCLK,1))
    {
        DEBUG_MESSAGE("Failed to set ADI_CLOCK_PCLK \n");
    }
}

/*-----------------------------------------------------------*/

static void prvSetupHardware( void )
{
    /* Clock initialization */
    prvClockInit();
    /* System initialization */
    SystemInit();
    /* test system initialization */
    common_Init();
    /* For the FreeRTOS, assign HW interrupt priorities */
    assignInterruptPriorities();
    /* Initialise the LEDs. */
    vParTestInitialise();
    /* FreeRTOS Interrupt Config Check */
    prvIntConfigCheck();
}

/*-----------------------------------------------------------*/

static void prvCheckOtherTasksAreStillRunning( void )
{
    if( xAreBlockingQueuesStillRunning() != pdTRUE )
    {
        ulErrorFlags |= 0x01;
    }

    if ( xArePollingQueuesStillRunning() != pdTRUE )
    {
        ulErrorFlags |= 0x02;
    }

    if( xIsCreateTaskStillRunning() != pdTRUE )
    {
        ulErrorFlags |= 0x04;
    }

    if( xAreRecursiveMutexTasksStillRunning() != pdTRUE )
    {
        ulErrorFlags |= 0x08;
    }

    if( xAreStaticAllocationTasksStillRunning() != pdTRUE )
    {
        ulErrorFlags |= 0x10;
    }
}

/*-----------------------------------------------------------*/

static void prvPrintInformation( void )
{
    if (ulErrorFlags)
    {
        printf("Test failed: %d round(s)\r\n", test_round);
        ulErrorFlags = 0;
        ulErrorFlags_for_loop = 1;
    }
    else
    {
        printf("The test is ok for %d round(s)\r\n", test_round);
    }

    if (test_round == mainTEST_ROUNDS && ulErrorFlags_for_loop == 0)
    {
        printf("Test passed\r\n");
    }
}

/*-----------------------------------------------------------*/

static void vCheckTask( void *pvParameters )
{
TickType_t xLastExecutionTime;

    xLastExecutionTime = xTaskGetTickCount();

    for( ;; )
    {
        test_round++;
     /* Delay until it is time to execute again. */
        vTaskDelayUntil( &xLastExecutionTime, mainCHECK_DELAY );

     /* Check all the other tasks to see if the error flag needs updating. */
        prvCheckOtherTasksAreStillRunning();

     /* print the test result */
        prvPrintInformation();
    }
}

/*-----------------------------------------------------------*/

static void prvIntConfigCheck( void )
{
#if defined(configMAX_SYSCALL_INTERRUPT_PRIORITY) && defined(configKERNEL_INTERRUPT_PRIORITY)
    configASSERT( (configMAX_SYSCALL_INTERRUPT_PRIORITY & mainINTERRUPT_MASK) != 0 );
    configASSERT( (configMAX_SYSCALL_INTERRUPT_PRIORITY < configKERNEL_INTERRUPT_PRIORITY) );
#endif
}

/*-----------------------------------------------------------*/

void vApplicationStackOverflowHook( TaskHandle_t pxTask, char *pcTaskName )
{
    ( void ) pcTaskName;
    ( void ) pxTask;

    /* Run time stack overflow checking is performed if
    configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2.  This hook
    function is called if a stack overflow is detected. */
    taskDISABLE_INTERRUPTS();
    for( ;; );
}

/*-----------------------------------------------------------*/

void vAssertCalled( const char * pcFile, unsigned long ulLine )
{
volatile unsigned long ul = 0;

    ( void ) pcFile;
    ( void ) ulLine;

    __asm volatile( "cpsid i" );
    while ( ul == 0 )
    {
        __asm volatile( "NOP" );
        __asm volatile( "NOP ");
    }
    __asm volatile( "cpsie i" );
}

/*-----------------------------------------------------------*/
