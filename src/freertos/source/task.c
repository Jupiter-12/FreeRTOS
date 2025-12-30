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

/* 当前正在运行的任务的任务控制块指针，默认初始化为NULL */
TCB_t *volatile pxCurrentTCB = NULL;

/* 任务就绪列表 */
List_t pxReadyTasksLists[configMAX_PRIORITIES];

static volatile UBaseType_t uxCurrentNumberOfTasks = (UBaseType_t)0U;  /* 当前任务数量 */
static TaskHandle_t         xIdleTaskHandle        = NULL;             /* 空闲任务句柄 */
static volatile TickType_t  xTickCount             = (TickType_t)0U;   /* 系统时基滴答计数器 */
static volatile UBaseType_t uxTopReadyPriority     = tskIDLE_PRIORITY; /* 就绪任务的最高优先级，默认初始化为 0 */

static void prvInitialiseNewTask(TaskFunction_t      pxTaskCode,    /* 任务入口 */
                                 const char *const   pcName,        /* 任务名称，字符串形式 */
                                 const uint32_t      ulStackDepth,  /* 任务栈大小，单位为字 */
                                 void *const         pvParameters,  /* 任务形参 */
                                 UBaseType_t         uxPriority,    /* 任务优先级，数值越大，优先级越高 */
                                 TaskHandle_t *const pxCreatedTask, /* 任务句柄 */
                                 TCB_t              *pxNewTCB);                  /* 任务控制块 */

static void prvAddNewTaskToReadyList(TCB_t *pxNewTCB);

static portTASK_FUNCTION(prvIdleTask, pvParameters);

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
#if 0
#define taskRESET_READY_PRIORITY(uxPriority)                                               \
    {                                                                                      \
        if (listCURRENT_LIST_LENGTH(&(pxReadyTasksLists[(uxPriority)])) == (UBaseType_t)0) \
        {                                                                                  \
            portRESET_READY_PRIORITY((uxPriority), (uxTopReadyPriority));                  \
        }                                                                                  \
    }
#else
#define taskRESET_READY_PRIORITY(uxPriority)                          \
    {                                                                 \
        portRESET_READY_PRIORITY((uxPriority), (uxTopReadyPriority)); \
    }
#endif /* 0 */

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
}

extern TCB_t Task1TCB;
extern TCB_t Task2TCB;

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

    /* 设置延时时间 */
    pxTCB->xTicksToDelay = xTicksToDelay;

    /* 将任务从就绪列表移除 */
    // uxListRemove(&(pxTCB->xStateListItem));
    taskRESET_READY_PRIORITY(pxTCB->uxPriority);

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

            /* 延时时间到，将任务就绪 */
            if (pxTCB->xTicksToDelay == 0)
            {
                taskRECORD_READY_PRIORITY(pxTCB->uxPriority);
            }
        }
    }

    /* 任务切换 */
    portYIELD();
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

static portTASK_FUNCTION(prvIdleTask, pvParameters)
{
    /* 防止编译器的警告 */
    (void)pvParameters;

    for (;;)
    {
        /* 空闲任务暂时什么都不做 */
    }
}
