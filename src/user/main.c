#include <stddef.h>

#include "main.h"

portCHAR flag1;
portCHAR flag2;
portCHAR flag3;

extern List_t pxReadyTasksLists[configMAX_PRIORITIES];

TaskHandle_t Task1_Handle;
#define TASK1_STACK_SIZE 128
StackType_t Task1Stack[TASK1_STACK_SIZE];
TCB_t       Task1TCB;

TaskHandle_t Task2_Handle;
#define TASK2_STACK_SIZE 128
StackType_t Task2Stack[TASK2_STACK_SIZE];
TCB_t       Task2TCB;

TaskHandle_t Task3_Handle;
#define TASK3_STACK_SIZE 128
StackType_t Task3Stack[TASK3_STACK_SIZE];
TCB_t       Task3TCB;

/* 定义空闲任务的栈 */
StackType_t IdleTaskStack[configMINIMAL_STACK_SIZE];
/* 定义空闲任务的任务控制块 */
TCB_t IdleTaskTCB;

void delay(uint32_t count);
void Task1_Entry(void *p_arg);
void Task2_Entry(void *p_arg);
void Task3_Entry(void *p_arg);

int main(void)
{
    /* 硬件初始化 */
    /* 将硬件相关的初始化放在这里，如果是软件仿真则没有相关初始化代码 */

    /* 初始化与任务相关的列表，如就绪列表 */
    prvInitialiseTaskLists();

    Task1_Handle =                                     /* 任务句柄 */
        xTaskCreateStatic((TaskFunction_t)Task1_Entry, /* 任务入口 */
                          (char *)"Task1",             /* 任务名称，字符串形式 */
                          (uint32_t)TASK1_STACK_SIZE,  /* 任务栈大小，单位为字 */
                          (void *)NULL,                /* 任务形参 */
                          (UBaseType_t)2,              /* 任务优先级，数值越大，优先级越高 */
                          (StackType_t *)Task1Stack,   /* 任务栈起始地址 */
                          (TCB_t *)&Task1TCB);         /* 任务控制块 */

    Task2_Handle =                                     /* 任务句柄 */
        xTaskCreateStatic((TaskFunction_t)Task2_Entry, /* 任务入口 */
                          (char *)"Task2",             /* 任务名称，字符串形式 */
                          (uint32_t)TASK2_STACK_SIZE,  /* 任务栈大小，单位为字 */
                          (void *)NULL,                /* 任务形参 */
                          (UBaseType_t)2,              /* 任务优先级，数值越大，优先级越高 */
                          (StackType_t *)Task2Stack,   /* 任务栈起始地址 */
                          (TCB_t *)&Task2TCB);         /* 任务控制块 */

    Task3_Handle =
        xTaskCreateStatic((TaskFunction_t)Task3_Entry,
                          (char *)"Task3",
                          (uint32_t)TASK3_STACK_SIZE,
                          (void *)NULL,
                          (UBaseType_t)3,
                          (StackType_t *)Task3Stack,
                          (TCB_t *)&Task3TCB);

    /* 启动调度器，开始多任务调度，启动成功则不返回 */
    vTaskStartScheduler();

    for (;;)
    {
        /* 系统启动成功不会到达这里 */
    }
}

/* 软件延时 */
void delay(uint32_t count)
{
    for (; count != 0; count--)
        ;
}
/* 任务 1 */
void Task1_Entry(void *p_arg)
{
    for (;;)
    {
        flag1 = 1;
        delay(100);
        flag1 = 0;
        delay(100);
    }
}

/* 任务 2 */
void Task2_Entry(void *p_arg)
{
    for (;;)
    {
        flag2 = 1;
        delay(100);
        flag2 = 0;
        delay(100);
    }
}

/* 任务 3 */
void Task3_Entry(void *p_arg)
{
    for (;;)
    {
        flag3 = 1;
        vTaskDelay(1);
        flag3 = 0;
        vTaskDelay(1);
    }
}

/* 获取空闲任务的内存 */
void vApplicationGetIdleTaskMemory(TCB_t       **ppxIdleTaskTCBBuffer,
                                   StackType_t **ppxIdleTaskStackBuffer,
                                   uint32_t     *pulIdleTaskStackSize)
{
    *ppxIdleTaskTCBBuffer   = &IdleTaskTCB;
    *ppxIdleTaskStackBuffer = IdleTaskStack;
    *pulIdleTaskStackSize   = configMINIMAL_STACK_SIZE;
}
