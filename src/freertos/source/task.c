#include <stddef.h>

#include "include/portable.h"
#include "include/task.h"

/* 当前正在运行的任务的任务控制块指针，默认初始化为NULL */
TCB_t *volatile pxCurrentTCB = NULL;

/* 任务就绪列表 */
List_t pxReadyTasksLists[configMAX_PRIORITIES];

static TaskHandle_t        xIdleTaskHandle = NULL;           /* 空闲任务句柄 */
static volatile TickType_t xTickCount      = (TickType_t)0U; /* 系统时基滴答计数器 */

static void prvInitialiseNewTask(TaskFunction_t      pxTaskCode,
                                 const char *const   pcName,
                                 const uint32_t      ulStackDepth,
                                 void *const         pvParameters,
                                 TaskHandle_t *const pxCreatedTask,
                                 TCB_t              *pxNewTCB);
static portTASK_FUNCTION(prvIdleTask, pvParameters);

#if (configSUPPORT_STATIC_ALLOCATION == 1)
/* 静态创建任务 */
TaskHandle_t xTaskCreateStatic(TaskFunction_t     pxTaskCode,
                               const char *const  pcName,
                               const uint32_t     ulStackDepth,
                               void *const        pvParameters,
                               StackType_t *const puxStackBuffer,
                               TCB_t *const       pxTaskBuffer)
{
    TCB_t       *pxNewTCB;
    TaskHandle_t xReturn;

    if ((pxTaskBuffer != NULL) && (puxStackBuffer != NULL))
    {
        pxNewTCB          = (TCB_t *)pxTaskBuffer;
        pxNewTCB->pxStack = (StackType_t *)puxStackBuffer;

        /* 创建新的任务 */
        prvInitialiseNewTask(pxTaskCode,   /* 任务入口 */
                             pcName,       /* 任务名称，字符串形式 */
                             ulStackDepth, /* 任务栈大小，单位为字 */
                             pvParameters, /* 任务形参 */
                             &xReturn,     /* 任务句柄 */
                             pxNewTCB);    /* 任务栈起始地址 */
    }
    else
    {
        xReturn = NULL;
    }

    /* 返回任务句柄，如果任务创建成功，此时 xReturn 应该指向任务控制块 */
    return xReturn;
}

#endif /* configSUPPORT_STATIC_ALLOCATION */

/* 初始化任务相关的列表 */
void prvInitialiseTaskLists(void)
{
    UBaseType_t uxPriority;

    for (uxPriority = (UBaseType_t)0U;
         uxPriority < (UBaseType_t)configMAX_PRIORITIES;
         uxPriority++)
    {
        vListInitialise(&(pxReadyTasksLists[uxPriority]));
    }
}

extern TCB_t Task1TCB;
extern TCB_t Task2TCB;

extern TCB_t IdleTaskTCB;
void         vApplicationGetIdleTaskMemory(TCB_t       **ppxIdleTaskTCBBuffer,
                                           StackType_t **ppxIdleTaskStackBuffer,
                                           uint32_t     *pulIdleTaskStackSize);
void         vTaskStartScheduler(void)
{
    /*=======================创建空闲任务 start=======================*/
    TCB_t       *pxIdleTaskTCBBuffer   = NULL; /* 用于指向空闲任务控制块 */
    StackType_t *pxIdleTaskStackBuffer = NULL; /* 用于空闲任务栈起始地址 */
    uint32_t     ulIdleTaskStackSize;

    /* 获取空闲任务的内存：任务栈和任务 TCB */
    vApplicationGetIdleTaskMemory(&pxIdleTaskTCBBuffer,
                                  &pxIdleTaskStackBuffer,
                                  &ulIdleTaskStackSize);
    /* 创建空闲任务 */
    xIdleTaskHandle =
        xTaskCreateStatic((TaskFunction_t)prvIdleTask,          /* 任务入口 */
                          (char *)"IDLE",                       /* 任务名称，字符串形式 */
                          (uint32_t)ulIdleTaskStackSize,        /* 任务栈大小，单位为字 */
                          (void *)NULL,                         /* 任务形参 */
                          (StackType_t *)pxIdleTaskStackBuffer, /* 任务栈起始地址 */
                          (TCB_t *)pxIdleTaskTCBBuffer);        /* 任务控制块 */
    /* 将任务添加到就绪列表 */
    vListInsertEnd(&(pxReadyTasksLists[0]),
                   &(((TCB_t *)pxIdleTaskTCBBuffer)->xStateListItem));
    /*==========================创建空闲任务 end=====================*/

    /* 手动指定第一个运行的任务 */
    pxCurrentTCB = &Task1TCB;

    /* 启动调度器 */
    if (xPortStartScheduler() != pdFALSE)
    {
        /* 调度器启动成功，则不会返回，即不会来到这里 */
    }
}

void vTaskSwitchContext(void)
{
    /* 如果当前任务是空闲任务，那么就去尝试执行任务 1 或者任务 2，
       看看他们的延时时间是否结束，如果任务的延时时间均没有到期，
       那就返回继续执行空闲任务 */
    if (pxCurrentTCB == &IdleTaskTCB)
    {
        if (Task1TCB.xTicksToDelay == 0)
        {
            pxCurrentTCB = &Task1TCB;
        }
        else if (Task2TCB.xTicksToDelay == 0)
        {
            pxCurrentTCB = &Task2TCB;
        }
        else
        {
            return; /* 任务延时均没有到期则返回，继续执行空闲任务 */
        }
    }
    else /* 当前任务不是空闲任务则会执行到这里 */
    {
        /* 如果当前任务是任务 1 或者任务 2 的话，检查下另外一个任务,
           如果另外的任务不在延时中，就切换到该任务
           否则，判断下当前任务是否应该进入延时状态，
           如果是的话，就切换到空闲任务。否则就不进行任何切换 */
        if (pxCurrentTCB == &Task1TCB)
        {
            if (Task2TCB.xTicksToDelay == 0)
            {
                pxCurrentTCB = &Task2TCB;
            }
            else if (pxCurrentTCB->xTicksToDelay != 0)
            {
                pxCurrentTCB = &IdleTaskTCB;
            }
            else
            {
                return; /* 返回，不进行切换，因为两个任务都处于延时中 */
            }
        }
        else if (pxCurrentTCB == &Task2TCB)
        {
            if (Task1TCB.xTicksToDelay == 0)
            {
                pxCurrentTCB = &Task1TCB;
            }
            else if (pxCurrentTCB->xTicksToDelay != 0)
            {
                pxCurrentTCB = &IdleTaskTCB;
            }
            else
            {
                return; /* 返回，不进行切换，因为两个任务都处于延时中 */
            }
        }
    }
}

/* 任务延时函数 */
void vTaskDelay(const TickType_t xTicksToDelay)
{
    TCB_t *pxTCB = NULL;

    /* 获取当前任务的 TCB */
    pxTCB = pxCurrentTCB;

    /* 设置延时时间 */
    pxTCB->xTicksToDelay = xTicksToDelay;

    /* 任务切换 */
    taskYIELD();
}

/* 系统时基滴答更新函数 */
void xTaskIncrementTick(void)
{
    TCB_t     *pxTCB = NULL;
    BaseType_t i     = 0;

    /* 更新系统时基计数器 xTickCount，xTickCount 是一个在 port.c 中定义的全局变量 */
    const TickType_t xConstTickCount = xTickCount + 1;
    xTickCount                       = xConstTickCount;

    /* 扫描就绪列表中所有任务的 xTicksToDelay，如果不为 0，则减 1 */
    for (i = 0; i < configMAX_PRIORITIES; i++)
    {
        pxTCB = (TCB_t *)listGET_OWNER_OF_HEAD_ENTRY((&pxReadyTasksLists[i]));
        if (pxTCB->xTicksToDelay > 0)
        {
            pxTCB->xTicksToDelay--;
        }
    }

    /* 任务切换 */
    portYIELD();
}

/* 初始化并创建一个新的任务 */
static void prvInitialiseNewTask(TaskFunction_t      pxTaskCode,
                                 const char *const   pcName,
                                 const uint32_t      ulStackDepth,
                                 void *const         pvParameters,
                                 TaskHandle_t *const pxCreatedTask,
                                 TCB_t              *pxNewTCB)
{
    StackType_t *pxTopOfStack;
    UBaseType_t  x;

    /* 获取栈顶地址 */
    pxTopOfStack = pxNewTCB->pxStack + (ulStackDepth - (uint32_t)1);
    /* 向下做 8 字节对齐 */
    pxTopOfStack = (StackType_t *)(((uint32_t)pxTopOfStack) & (~((uint32_t)0x0007)));

    /* 将任务的名字存储在 TCB 中 */
    for (x = (UBaseType_t)0; x < (UBaseType_t)configMAX_TASK_NAME_LEN; x++)
    {
        /* 可以用 strncpy，但在 FreeRTOS/裸机工程里通常不推荐直接使用 */
        pxNewTCB->pcTaskName[x] = pcName[x];

        if (pcName[x] == 0x00)
        {
            break;
        }
    }
    /* 任务名字的长度不能超过 configMAX_TASK_NAME_LEN */
    pxNewTCB->pcTaskName[configMAX_TASK_NAME_LEN - 1] = '\0';

    /* 初始化 TCB 中的 xStateListItem 节点 */
    vListInitialiseItem(&(pxNewTCB->xStateListItem));
    /* 设置 xStateListItem 节点的拥有者 */
    listSET_LIST_ITEM_OWNER(&(pxNewTCB->xStateListItem), pxNewTCB);

    /* 初始化任务栈 */
    pxNewTCB->pxTopOfStack = pxPortInitialiseStack(pxTopOfStack,  /* 栈顶指针 */
                                                   pxTaskCode,    /* 任务入口 */
                                                   pvParameters); /* 任务形参 */

    /* 让任务句柄指向任务控制块 */
    if ((void *)pxCreatedTask != NULL)
    {
        *pxCreatedTask = (TaskHandle_t)pxNewTCB;
    }
}

static portTASK_FUNCTION(prvIdleTask, pvParameters)
{
    /* 防止编译器的警告 */
    (void)pvParameters;

    for (;;)
    {
        /* 空闲任务暂时什么都不做 */
    }
}
