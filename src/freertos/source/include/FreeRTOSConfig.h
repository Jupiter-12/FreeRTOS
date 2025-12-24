#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* 这里是一些 FreeRTOS 的配置选项 */
#define configUSE_16_BIT_TICKS          0  /* 使用 32 位 Tick */
#define configMAX_TASK_NAME_LEN         16 /* 任务名称最大长度 */
#define configSUPPORT_STATIC_ALLOCATION 1  /* 支持静态内存分配 */
#define configMAX_PRIORITIES            5  /* 最大优先级数（默认 5 个，最大支持 256 个优先级） */

#define configKERNEL_INTERRUPT_PRIORITY      255 /* 高四位有效，即等于0xff，或者是15 */
#define configMAX_SYSCALL_INTERRUPT_PRIORITY 191 /* 中断优先级配置，高四位有效，即等于0xb0，或者是11 */

#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler SysTick_Handler
#define vPortSVCHandler     SVC_Handler

#endif /* FREERTOS_CONFIG_H */
