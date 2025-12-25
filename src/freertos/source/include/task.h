#ifndef TASK_H
#define TASK_H

#include "list.h"
#include "projdefs.h"

#define taskYIELD() portYIELD()

#define taskENTER_CRITICAL()          portENTER_CRITICAL()              /* 进入临界段，不带中断保护版本，不能嵌套 */
#define taskENTER_CRITICAL_FROM_ISR() portSET_INTERRUPT_MASK_FROM_ISR() /* 进入临界段，带中断保护版本，可以嵌套 */

#define taskEXIT_CRITICAL()           portEXIT_CRITICAL()                  /* 退出临界段，不带中断保护版本，不能嵌套 */
#define taskEXIT_CRITICAL_FROM_ISR(x) portCLEAR_INTERRUPT_MASK_FROM_ISR(x) /* 退出临界段，带中断保护版本，可以嵌套 */

typedef struct tskTaskControlBlock
{
    volatile StackType_t *pxTopOfStack; /* 栈顶 */

    ListItem_t xStateListItem; /* 任务节点 */

    StackType_t *pxStack; /* 任务栈起始地址 */

    char pcTaskName[configMAX_TASK_NAME_LEN]; /* 任务名称，字符串形式 */
} tskTCB;
typedef tskTCB TCB_t;

/* 任务句柄 */
typedef void *TaskHandle_t;

#if (configSUPPORT_STATIC_ALLOCATION == 1)
TaskHandle_t xTaskCreateStatic(TaskFunction_t     pxTaskCode,
                               const char *const  pcName,
                               const uint32_t     ulStackDepth,
                               void *const        pvParameters,
                               StackType_t *const puxStackBuffer,
                               TCB_t *const       pxTaskBuffer);
#endif /* configSUPPORT_STATIC_ALLOCATION */

void prvInitialiseTaskLists(void);
void vTaskStartScheduler(void);
void vTaskSwitchContext(void);

#endif /* TASK_H */
