#include <stddef.h>

#include "include/portable.h"
#include "include/task.h"

/* 空闲任务优先级宏定义，在 task.h 中定义 */
#define tskIDLE_PRIORITY ((UBaseType_t)0U)

/* 将任务添加到就绪列表 */
#define prvAddTaskToReadyList(pxTCB)                          \
    taskRECORD_READY_PRIORITY((pxTCB)->uxPriority);           \
    vListInsertEnd(&(pxReadyTasksLists[(pxTCB)->uxPriority]), \
                   &((pxTCB)->xStateListItem));

/*
 * 当系统时基计数器溢出的时候，延时列表 pxDelayedTaskList 和
 * pxOverflowDelayedTaskList 要互相切换
 */
#define taskSWITCH_DELAYED_LISTS()                             \
    {                                                          \
        List_t *pxTemp;                                        \
        pxTemp                    = pxDelayedTaskList;         \
        pxDelayedTaskList         = pxOverflowDelayedTaskList; \
        pxOverflowDelayedTaskList = pxTemp;                    \
        xNumOfOverflows++;                                     \
        prvResetNextTaskUnblockTime();                         \
    }

/* 当前正在运行的任务的任务控制块指针，默认初始化为NULL */
TCB_t *volatile pxCurrentTCB = NULL;

/* 任务就绪列表 */
List_t pxReadyTasksLists[configMAX_PRIORITIES];

static volatile UBaseType_t uxCurrentNumberOfTasks = (UBaseType_t)0U;  /* 当前任务数量 */
static TaskHandle_t         xIdleTaskHandle        = NULL;             /* 空闲任务句柄 */
static volatile TickType_t  xTickCount             = (TickType_t)0U;   /* 系统时基滴答计数器 */
static volatile UBaseType_t uxTopReadyPriority     = tskIDLE_PRIORITY; /* 就绪任务的最高优先级，默认初始化为 0 */

/* FreeRTOS 定义了两个任务延时列表，当系统时基计数器 xTickCount 没有溢出时，用一条列表，
当 xTickCount 溢出后，用另外一条列表 */
static List_t xDelayedTaskList1;
static List_t xDelayedTaskList2;
static List_t *volatile pxDelayedTaskList;         /* 任务延时列表指针，指向 xTickCount 没有溢出时使用的那条列表 */
static List_t *volatile pxOverflowDelayedTaskList; /* 任务延时列表指针，指向 xTickCount 溢出时使用的那条列表 */

static volatile TickType_t xNextTaskUnblockTime = (TickType_t)0U; /* 下一个任务的要解除阻塞的时间 */
static volatile BaseType_t xNumOfOverflows      = (BaseType_t)0;  /* 系统时基计数器溢出次数 */

static void prvInitialiseNewTask(TaskFunction_t      pxTaskCode,    /* 任务入口 */
                                 const char *const   pcName,        /* 任务名称，字符串形式 */
                                 const uint32_t      ulStackDepth,  /* 任务栈大小，单位为字 */
                                 void *const         pvParameters,  /* 任务形参 */
                                 UBaseType_t         uxPriority,    /* 任务优先级，数值越大，优先级越高 */
                                 TaskHandle_t *const pxCreatedTask, /* 任务句柄 */
                                 TCB_t              *pxNewTCB);                  /* 任务控制块 */

static void prvAddNewTaskToReadyList(TCB_t *pxNewTCB);

static portTASK_FUNCTION(prvIdleTask, pvParameters);
static void prvAddCurrentTaskToDelayedList(TickType_t xTicksToWait);
static void prvResetNextTaskUnblockTime(void);

/* 查找最高优先级的就绪任务：通用方法 */
#if (configUSE_PORT_OPTIMISED_TASK_SELECTION == 0)
/* uxTopReadyPriority 存的是就绪任务的最高优先级 */
#define taskRECORD_READY_PRIORITY(uxPriority)  \
    {                                          \
        if ((uxPriority) > uxTopReadyPriority) \
        {                                      \
            uxTopReadyPriority = (uxPriority); \
        }                                      \
    } /* taskRECORD_READY_PRIORITY */

/*-----------------------------------------------------------*/

#define taskSELECT_HIGHEST_PRIORITY_TASK()                                              \
    {                                                                                   \
        UBaseType_t uxTopPriority = uxTopReadyPriority;                                 \
        /* 寻找包含就绪任务的最高优先级的队列 */                                        \
        while (listLIST_IS_EMPTY(&(pxReadyTasksLists[uxTopPriority])))                  \
        {                                                                               \
            --uxTopPriority;                                                            \
        }                                                                               \
        /* 获取优先级最高的就绪任务的 TCB，然后更新到 pxCurrentTCB */                   \
        listGET_OWNER_OF_NEXT_ENTRY(pxCurrentTCB, &(pxReadyTasksLists[uxTopPriority])); \
        /* 更新 uxTopReadyPriority */                                                   \
        uxTopReadyPriority = uxTopPriority;                                             \
    } /* taskSELECT_HIGHEST_PRIORITY_TASK */

/*-----------------------------------------------------------*/

/* 这两个宏定义只有在选择优化方法时才用，这里定义为空 */
#define taskRESET_READY_PRIORITY(uxPriority)
#define portRESET_READY_PRIORITY(uxPriority, uxTopReadyPriority)

#else /* configUSE_PORT_OPTIMISED_TASK_SELECTION */
/* 查找最高优先级的就绪任务：根据处理器架构优化后的方法 */

#define taskRECORD_READY_PRIORITY(uxPriority) \
    portRECORD_READY_PRIORITY(uxPriority, uxTopReadyPriority)

/*-----------------------------------------------------------*/

#define taskSELECT_HIGHEST_PRIORITY_TASK()                                              \
    {                                                                                   \
        UBaseType_t uxTopPriority;                                                      \
        /* 寻找最高优先级 */                                                            \
        portGET_HIGHEST_PRIORITY(uxTopPriority, uxTopReadyPriority);                    \
        /* 获取优先级最高的就绪任务的 TCB，然后更新到 pxCurrentTCB */                   \
        listGET_OWNER_OF_NEXT_ENTRY(pxCurrentTCB, &(pxReadyTasksLists[uxTopPriority])); \
    } /* taskSELECT_HIGHEST_PRIORITY_TASK() */

/*-----------------------------------------------------------*/
#define taskRESET_READY_PRIORITY(uxPriority)                                               \
    {                                                                                      \
        if (listCURRENT_LIST_LENGTH(&(pxReadyTasksLists[(uxPriority)])) == (UBaseType_t)0) \
        {                                                                                  \
            portRESET_READY_PRIORITY((uxPriority), (uxTopReadyPriority));                  \
        }                                                                                  \
    }

#endif /* configUSE_PORT_OPTIMISED_TASK_SELECTION */

#if (configSUPPORT_STATIC_ALLOCATION == 1)
/* 静态创建任务 */
TaskHandle_t xTaskCreateStatic(TaskFunction_t     pxTaskCode,     /* 任务入口 */
                               const char *const  pcName,         /* 任务名称，字符串形式 */
                               const uint32_t     ulStackDepth,   /* 任务栈大小，单位为字 */
                               void *const        pvParameters,   /* 任务形参 */
                               UBaseType_t        uxPriority,     /* 任务优先级，数值越大，优先级越高 */
                               StackType_t *const puxStackBuffer, /* 任务栈起始地址 */
                               TCB_t *const       pxTaskBuffer)         /* 任务控制块 */
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
                             uxPriority,   /* 任务优先级 */
                             &xReturn,     /* 任务句柄 */
                             pxNewTCB);    /* 任务栈起始地址 */

        /* 将任务添加到就绪列表 */
        prvAddNewTaskToReadyList(pxNewTCB);
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

    vListInitialise(&xDelayedTaskList1);
    vListInitialise(&xDelayedTaskList2);

    pxDelayedTaskList         = &xDelayedTaskList1;
    pxOverflowDelayedTaskList = &xDelayedTaskList2;
}

extern TCB_t IdleTaskTCB;
extern void  vApplicationGetIdleTaskMemory(TCB_t       **ppxIdleTaskTCBBuffer,
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
                          (UBaseType_t)tskIDLE_PRIORITY,        /* 任务优先级，数值越大，优先级越高 */
                          (StackType_t *)pxIdleTaskStackBuffer, /* 任务栈起始地址 */
                          (TCB_t *)pxIdleTaskTCBBuffer);        /* 任务控制块 */
    /*==========================创建空闲任务 end=====================*/

    xNextTaskUnblockTime = portMAX_DELAY; /* 初始化下一个任务解除阻塞时间 */

    /* 启动调度器 */
    if (xPortStartScheduler() != pdFALSE)
    {
        /* 调度器启动成功，则不会返回，即不会来到这里 */
    }
}

/* 任务切换，即寻找优先级最高的就绪任务 */
void vTaskSwitchContext(void)
{
    /* 获取优先级最高的就绪任务的 TCB，然后更新到 pxCurrentTCB */
    taskSELECT_HIGHEST_PRIORITY_TASK();
}

/* 任务延时函数 */
void vTaskDelay(const TickType_t xTicksToDelay)
{
    TCB_t *pxTCB = NULL;

    /* 获取当前任务的 TCB */
    pxTCB = pxCurrentTCB;

    /* 将任务插入到延时列表 */
    prvAddCurrentTaskToDelayedList(xTicksToDelay);

    /* 任务切换 */
    taskYIELD();
}

/* 系统时基滴答更新函数 */
// void xTaskIncrementTick(void)
BaseType_t xTaskIncrementTick(void)
{
    TCB_t     *pxTCB = NULL;
    TickType_t xItemValue;
    BaseType_t xSwitchRequired = pdFALSE;

    /* 更新系统时基计数器 xTickCount，xTickCount 是一个在 port.c 中定义的全局变量 */
    const TickType_t xConstTickCount = xTickCount + 1;
    xTickCount                       = xConstTickCount;

    /* 如果 xConstTickCount 溢出，则切换延时列表 */
    if (xConstTickCount == (TickType_t)0U)
    {
        taskSWITCH_DELAYED_LISTS();
    }

    /* 最近的延时任务延时到期 */
    if (xConstTickCount >= xNextTaskUnblockTime)
    {
        for (;;)
        {
            if (listLIST_IS_EMPTY(pxDelayedTaskList) != pdFALSE)
            {
                /* 延时列表为空，设置 xNextTaskUnblockTime 为可能的最大值 */
                xNextTaskUnblockTime = portMAX_DELAY;
                break;
            }
            else /* 延时列表不为空 */
            {
                pxTCB      = (TCB_t *)listGET_OWNER_OF_HEAD_ENTRY(pxDelayedTaskList);
                xItemValue = listGET_LIST_ITEM_VALUE(&(pxTCB->xStateListItem));

                /* 直到将延时列表中所有延时到期的任务移除才跳出 for 循环 */
                if (xConstTickCount < xItemValue)
                {
                    xNextTaskUnblockTime = xItemValue;
                    break;
                }

                /* 将任务从延时列表移除，消除等待状态 */
                (void)uxListRemove(&(pxTCB->xStateListItem));

                /* 将解除等待的任务添加到就绪列表 */
                prvAddTaskToReadyList(pxTCB);

#if (configUSE_PREEMPTION == 1)
                {
                    if (pxTCB->uxPriority >= pxCurrentTCB->uxPriority)
                    {
                        xSwitchRequired = pdTRUE;
                    }
                }
#endif /* configUSE_PREEMPTION */
            }
        }
    } /* xConstTickCount >= xNextTaskUnblockTime */

#if ((configUSE_PREEMPTION == 1) && (configUSE_TIME_SLICING == 1))
    {
        if (listCURRENT_LIST_LENGTH(&(pxReadyTasksLists[pxCurrentTCB->uxPriority])) > (UBaseType_t)1)
        {
            xSwitchRequired = pdTRUE;
        }
    }
#endif /* ( ( configUSE_PREEMPTION == 1 ) && ( configUSE_TIME_SLICING == 1 ) ) */

    /* 任务切换 */
    // portYIELD();

    return xSwitchRequired;
}

/* 初始化并创建一个新的任务 */
static void prvInitialiseNewTask(TaskFunction_t      pxTaskCode,    /* 任务入口 */
                                 const char *const   pcName,        /* 任务名称，字符串形式 */
                                 const uint32_t      ulStackDepth,  /* 任务栈大小，单位为字 */
                                 void *const         pvParameters,  /* 任务形参 */
                                 UBaseType_t         uxPriority,    /* 任务优先级，数值越大，优先级越高 */
                                 TaskHandle_t *const pxCreatedTask, /* 任务句柄 */
                                 TCB_t              *pxNewTCB)                   /* 任务控制块 */
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

    /* 初始化优先级 */
    if (uxPriority >= (UBaseType_t)configMAX_PRIORITIES)
    {
        uxPriority = (UBaseType_t)configMAX_PRIORITIES - (UBaseType_t)1U;
    }
    pxNewTCB->uxPriority = uxPriority;

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

static void prvAddNewTaskToReadyList(TCB_t *pxNewTCB)
{
    /* 进入临界段 */
    taskENTER_CRITICAL();
    {
        /* 全局任务计时器加一操作 */
        uxCurrentNumberOfTasks++;

        /* 如果 pxCurrentTCB 为空，则将 pxCurrentTCB 指向新创建的任务 */
        if (pxCurrentTCB == NULL)
        {
            pxCurrentTCB = pxNewTCB;

            /* 如果是第一次创建任务，则需要初始化任务相关的列表 */
            if (uxCurrentNumberOfTasks == (UBaseType_t)1)
            {
                /* 初始化任务相关的列表 */
                prvInitialiseTaskLists();
            }
        }
        else /* 如果 pxCurrentTCB 不为空，则根据任务的优先级将 pxCurrentTCB 指向最高优先级任务的 TCB */
        {
            if (pxCurrentTCB->uxPriority <= pxNewTCB->uxPriority)
            {
                pxCurrentTCB = pxNewTCB;
            }
        }

        /* 将任务添加到就绪列表 */
        prvAddTaskToReadyList(pxNewTCB);
    }
    /* 退出临界段 */
    taskEXIT_CRITICAL();
}

/* 空闲任务 */
static portTASK_FUNCTION(prvIdleTask, pvParameters)
{
    /* 防止编译器的警告 */
    (void)pvParameters;

    for (;;)
    {
        /* 空闲任务暂时什么都不做 */
    }
}

static void prvAddCurrentTaskToDelayedList(TickType_t xTicksToWait)
{
    TickType_t xTimeToWake;

    /* 获取系统时基计数器 xTickCount 的值 */
    const TickType_t xConstTickCount = xTickCount;

    /* 将任务从就绪列表中移除 */
    if (uxListRemove(&(pxCurrentTCB->xStateListItem)) == (UBaseType_t)0)
    {
        /* 将任务在优先级位图中对应的位清除 */
        portRESET_READY_PRIORITY(pxCurrentTCB->uxPriority,
                                 uxTopReadyPriority);
    }

    /* 计算任务延时到期时，系统时基计数器 xTickCount 的值是多少 */
    xTimeToWake = xConstTickCount + xTicksToWait;

    /* 将延时到期的值设置为节点的排序值 */
    listSET_LIST_ITEM_VALUE(&(pxCurrentTCB->xStateListItem),
                            xTimeToWake);

    /* 溢出 */
    if (xTimeToWake < xConstTickCount)
    {
        vListInsert(pxOverflowDelayedTaskList,
                    &(pxCurrentTCB->xStateListItem));
    }
    else /* 没有溢出 */
    {

        vListInsert(pxDelayedTaskList,
                    &(pxCurrentTCB->xStateListItem));

        /* 更新下一个任务解锁时刻变量 xNextTaskUnblockTime 的值 */
        if (xTimeToWake < xNextTaskUnblockTime)
        {
            xNextTaskUnblockTime = xTimeToWake;
        }
    }
}

static void prvResetNextTaskUnblockTime(void)
{
    TCB_t *pxTCB;

    if (listLIST_IS_EMPTY(pxDelayedTaskList) != pdFALSE)
    {
        /* The new current delayed list is empty.  Set xNextTaskUnblockTime to
        the maximum possible value so it is	extremely unlikely that the
        if( xTickCount >= xNextTaskUnblockTime ) test will pass until
        there is an item in the delayed list. */
        xNextTaskUnblockTime = portMAX_DELAY; /* 当前延时列表为空，则设置 xNextTaskUnblockTime 等于最大值 */
    }
    /* 当前列表不为空，则有任务在延时，则获取当前列表下第一个节点的排序值
       然后将该节点的排序值更新到 xNextTaskUnblockTime */
    else
    {
        /* The new current delayed list is not empty, get the value of
        the item at the head of the delayed list.  This is the time at
        which the task at the head of the delayed list should be removed
        from the Blocked state. */
        (pxTCB)              = (TCB_t *)listGET_OWNER_OF_HEAD_ENTRY(pxDelayedTaskList);
        xNextTaskUnblockTime = listGET_LIST_ITEM_VALUE(&((pxTCB)->xStateListItem));
    }
}
