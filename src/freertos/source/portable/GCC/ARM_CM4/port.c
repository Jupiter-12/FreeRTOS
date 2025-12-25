// #include "../../../include/task.h"
#include "../../../include/portable.h"

// #include "stm32f4xx.h"

#define portINITIAL_XPSR       (0x01000000)                /* 初始 xPSR 寄存器值 */
#define portSTART_ADDRESS_MASK ((StackType_t)0xfffffffeUL) /* 函数地址对齐掩码 */

/*
 * 参考资料《STM32F10xxx Cortex-M3 programming manual》4.4.7，百度搜索“PM0056”即可找到这个文档
 * 在 Cortex-M 中，内核外设 SCB 中 SHPR3 寄存器用于设置 SysTick 和 PendSV 的异常优先级
 * System handler priority register 3 (SCB_SHPR3) SCB_SHPR3：0xE000 ED20
 * Bits 31:24 PRI_15[7:0]: Priority of system handler 15, SysTick exception
 * Bits 23:16 PRI_14[7:0]: Priority of system handler 14, PendSV
 */
#define portNVIC_SYSPRI2_REG (*((volatile uint32_t *)0xe000ed20))

#define portNVIC_PENDSV_PRI  (((uint32_t)configKERNEL_INTERRUPT_PRIORITY) << 16UL)
#define portNVIC_SYSTICK_PRI (((uint32_t)configKERNEL_INTERRUPT_PRIORITY) << 24UL)

/* 用于嵌套临界区管理的变量，在调度器启动时会被重新初始化为 0 ：
vTaskStartScheduler()->xPortStartScheduler()->uxCriticalNesting = 0 */
static UBaseType_t uxCriticalNesting = 0xaaaaaaaa;

void prvStartFirstTask( void );
void vPortSVCHandler( void );
void xPortPendSVHandler( void );

/*
*************************************************************************
*                              任务栈初始化函数
*************************************************************************
*/
static void prvTaskExitError(void)
{
    /* 函数停止在这里 */
    for (;;)
        ;
}

/* 初始化任务栈 */
StackType_t *pxPortInitialiseStack(StackType_t   *pxTopOfStack,
                                   TaskFunction_t pxCode,
                                   void          *pvParameters)
{
    /* 异常发生时，自动加载到 CPU 寄存器的内容 */
    pxTopOfStack--;
    *pxTopOfStack = portINITIAL_XPSR;
    pxTopOfStack--;
    *pxTopOfStack = ((StackType_t)pxCode) & portSTART_ADDRESS_MASK;
    pxTopOfStack--;
    *pxTopOfStack = (StackType_t)prvTaskExitError;
    pxTopOfStack -= 5; /* R12, R3, R2 and R1 默认初始化为 0 */
    *pxTopOfStack = (StackType_t)pvParameters;

    /* 异常发生时，手动加载到 CPU 寄存器的内容 */
    pxTopOfStack -= 8;

    /* 返回栈顶指针，此时 pxTopOfStack 指向空闲栈 */
    return pxTopOfStack;
}

BaseType_t xPortStartScheduler(void)
{
    /* 配置 PendSV 和 SysTick 的中断优先级为最低 */
    portNVIC_SYSPRI2_REG |= portNVIC_PENDSV_PRI;
    portNVIC_SYSPRI2_REG |= portNVIC_SYSTICK_PRI;

    /* 启动第一个任务，不再返回 */
    prvStartFirstTask();

    /* 不应该运行到这里 */
    return 0;
}

#if defined(__CC_ARM) && !defined(__clang__)
/* ARMCC(RVDS) 语法：保持原样 */

/*
 * 参考资料《STM32F10xxx Cortex-M3 programming manual》4.4.3，百度搜索“PM0056”即可找到这个文档
 * 在 Cortex-M 中，内核外设SCB 的地址范围为：0xE000ED00-0xE000ED3F
 * 0xE000ED008 为 SCB 外设中 SCB_VTOR 这个寄存器的地址，里面存放的是向量表的起始地址，即 MSP 的地址
 */
__asm void prvStartFirstTask( void )
{
    PRESERVE8

    /* 在 Cortex-M 中，0xE000ED08 是 SCB_VTOR 这个寄存器的地址，
    里面存放的是向量表的起始地址，即 MSP 的地址 */
    ldr r0, =0xE000ED08
    ldr r0, [r0]
    ldr r0, [r0]

    /* 设置主堆栈指针 msp 的值 */
    msr msp, r0

    /* 使能全局中断 */
    cpsie i
    cpsie f
    dsb
    isb

    /* 调用 SVC 去启动第一个任务 */
    svc 0
    nop
    nop
}

__asm void vPortSVCHandler( void )
{
    extern pxCurrentTCB;

    PRESERVE8

    ldr	r3, =pxCurrentTCB	/* 加载pxCurrentTCB的地址到r3 */
    ldr r1, [r3]			/* 加载pxCurrentTCB到r1 */
    ldr r0, [r1]			/* 加载pxCurrentTCB指向的值到r0，目前r0的值等于第一个任务堆栈的栈顶 */
    ldmia r0!, {r4-r11}		/* 以r0为基地址，将栈里面的内容加载到r4~r11寄存器，同时r0会递增 */
    msr psp, r0				/* 将r0的值，即任务的栈指针更新到psp */
    isb
    mov r0, #0              /* 设置r0的值为0 */
    msr	basepri, r0         /* 设置basepri寄存器的值为0，即所有的中断都没有被屏蔽 */
    orr r14, #0xd           /* 当从SVC中断服务退出前,通过向r14寄存器最后4位按位或上0x0D，
                               使得硬件在退出时使用进程堆栈指针PSP完成出栈操作并返回后进入线程模式、返回 Thumb 状态。
                               在 SVC 中断服务里面，使用的是 MSP 堆栈指针，是处在 ARM 状态。 */

    bx r14                  /* 异常返回，这个时候栈中的剩下内容将会自动加载到CPU寄存器：
                               xPSR，PC（任务入口地址），R14，R12，R3，R2，R1，R0（任务的形参）
                               同时PSP的值也将更新，即指向任务栈的栈顶 */
}

__asm void xPortPendSVHandler( void )
{
    extern pxCurrentTCB;
    extern vTaskSwitchContext;

    PRESERVE8

    /* 当进入PendSVC Handler时，上一个任务运行的环境即：
       xPSR，PC（任务入口地址），R14，R12，R3，R2，R1，R0（任务的形参）
       这些CPU寄存器的值会自动保存到任务的栈中，剩下的r4~r11需要手动保存 */
    /* 获取任务栈指针到r0 */
    mrs r0, psp
    isb

    ldr	r3, =pxCurrentTCB		/* 加载pxCurrentTCB的地址到r3 */
    ldr	r2, [r3]                /* 加载pxCurrentTCB到r2 */

    stmdb r0!, {r4-r11}			/* 将CPU寄存器r4~r11的值存储到r0指向的地址 */
    str r0, [r2]                /* 将任务栈的新的栈顶指针存储到当前任务TCB的第一个成员，即栈顶指针 */


    stmdb sp!, {r3, r14}        /* 将R3和R14临时压入堆栈，因为即将调用函数vTaskSwitchContext,
                                  调用函数时,返回地址自动保存到R14中,所以一旦调用发生,R14的值会被覆盖,因此需要入栈保护;
                                  R3保存的当前激活的任务TCB指针(pxCurrentTCB)地址,函数调用后会用到,因此也要入栈保护 */
    mov r0, #configMAX_SYSCALL_INTERRUPT_PRIORITY    /* 进入临界段 */
    msr basepri, r0
    dsb
    isb
    bl vTaskSwitchContext       /* 调用函数vTaskSwitchContext，寻找新的任务运行,通过使变量pxCurrentTCB指向新的任务来实现任务切换 */
    mov r0, #0                  /* 退出临界段 */
    msr basepri, r0
    ldmia sp!, {r3, r14}        /* 恢复r3和r14 */

    ldr r1, [r3]
    ldr r0, [r1] 				/* 当前激活的任务TCB第一项保存了任务堆栈的栈顶,现在栈顶值存入R0*/
    ldmia r0!, {r4-r11}			/* 出栈 */
    msr psp, r0
    isb
    bx r14                      /* 异常发生时,R14中保存异常返回标志,包括返回后进入线程模式还是处理器模式、
                                   使用PSP堆栈指针还是MSP堆栈指针，当调用 bx r14指令后，硬件会知道要从异常返回，
                                   然后出栈，这个时候堆栈指针PSP已经指向了新任务堆栈的正确位置，
                                   当新任务的运行地址被出栈到PC寄存器后，新的任务也会被执行。*/
    nop
}

#else /* GCC/Clang/armclang */
/* GCC/Clang 语法：naked + 内联汇编（注释原封不动保留为 C 注释） */

/*
 * 参考资料《STM32F10xxx Cortex-M3 programming manual》4.4.3，百度搜索“PM0056”即可找到这个文档
 * 在 Cortex-M 中，内核外设SCB 的地址范围为：0xE000ED00-0xE000ED3F
 * 0xE000ED008 为 SCB 外设中 SCB_VTOR 这个寄存器的地址，里面存放的是向量表的起始地址，即 MSP 的地址
 */
__attribute__((naked)) void prvStartFirstTask(void)
{
    __asm volatile(
        /* 在 Cortex-M 中，0xE000ED08 是 SCB_VTOR 这个寄存器的地址，
        里面存放的是向量表的起始地址，即 MSP 的地址 */
        "ldr r0, =0xE000ED08 \n"
        "ldr r0, [r0]        \n"
        "ldr r0, [r0]        \n"

        /* 设置主堆栈指针 msp 的值 */
        "msr msp, r0         \n"

        /* 使能全局中断 */
        "cpsie i             \n"
        "cpsie f             \n"
        "dsb                 \n"
        "isb                 \n"

        /* 调用 SVC 去启动第一个任务 */
        "svc 0               \n"
        "nop                 \n"
        "nop                 \n"
        :
        :
        : "memory");
}

__attribute__((naked)) void vPortSVCHandler(void)
{
    __asm volatile(
        "ldr r3, =pxCurrentTCB \n" /* 加载pxCurrentTCB的地址到r3 */
        "ldr r1, [r3]          \n" /* 加载pxCurrentTCB到r1 */
        "ldr r0, [r1]          \n" /* 加载pxCurrentTCB指向的值到r0，目前r0的值等于第一个任务堆栈的栈顶 */
        "ldmia r0!, {r4-r11}   \n" /* 以r0为基地址，将栈里面的内容加载到r4~r11寄存器，同时r0会递增 */
        "msr psp, r0           \n" /* 将r0的值，即任务的栈指针更新到psp */
        "isb                   \n"
        "mov r0, #0            \n" /* 设置r0的值为0 */
        "msr basepri, r0       \n" /* 设置basepri寄存器的值为0，即所有的中断都没有被屏蔽 */
        "orr r14, r14, #0xd    \n" /* 当从SVC中断服务退出前,通过向r14寄存器最后4位按位或上0x0D，
                                     使得硬件在退出时使用进程堆栈指针PSP完成出栈操作并返回后进入线程模式、返回 Thumb 状态。
                                     在 SVC 中断服务里面，使用的是 MSP 堆栈指针，是处在 ARM 状态。 */
        "bx r14                \n" /* 异常返回，这个时候栈中的剩下内容将会自动加载到CPU寄存器：
                                     xPSR，PC（任务入口地址），R14，R12，R3，R2，R1，R0（任务的形参）
                                     同时PSP的值也将更新，即指向任务栈的栈顶 */
        :
        :
        : "memory");
}

__attribute__((naked)) void xPortPendSVHandler(void)
{
    __asm volatile(
        /* 当进入PendSVC Handler时，上一个任务运行的环境即：
           xPSR，PC（任务入口地址），R14，R12，R3，R2，R1，R0（任务的形参）
           这些CPU寄存器的值会自动保存到任务的栈中，剩下的r4~r11需要手动保存 */
        /* 获取任务栈指针到r0 */
        "mrs r0, psp               \n"
        "isb                       \n"

        "ldr r3, =pxCurrentTCB     \n" /* 加载pxCurrentTCB的地址到r3 */
        "ldr r2, [r3]              \n" /* 加载pxCurrentTCB到r2 */

        "stmdb r0!, {r4-r11}       \n" /* 将CPU寄存器r4~r11的值存储到r0指向的地址 */
        "str r0, [r2]              \n" /* 将任务栈的新的栈顶指针存储到当前任务TCB的第一个成员，即栈顶指针 */

        "stmdb sp!, {r3, r14}      \n" /* 将R3和R14临时压入堆栈，因为即将调用函数vTaskSwitchContext,
                                          调用函数时,返回地址自动保存到R14中,所以一旦调用发生,R14的值会被覆盖,因此需要入栈保护;
                                          R3保存的当前激活的任务TCB指针(pxCurrentTCB)地址,函数调用后会用到,因此也要入栈保护 */
        "mov r0, %[mask]           \n" /* 进入临界段 */
        "msr basepri, r0           \n"
        "dsb                       \n"
        "isb                       \n"
        "bl vTaskSwitchContext     \n" /* 调用函数vTaskSwitchContext，寻找新的任务运行,通过使变量pxCurrentTCB指向新的任务来实现任务切换 */
        "mov r0, #0                \n" /* 退出临界段 */
        "msr basepri, r0           \n"
        "ldmia sp!, {r3, r14}      \n" /* 恢复r3和r14 */

        "ldr r1, [r3]              \n"
        "ldr r0, [r1]              \n" /* 当前激活的任务TCB第一项保存了任务堆栈的栈顶,现在栈顶值存入R0*/
        "ldmia r0!, {r4-r11}       \n" /* 出栈 */
        "msr psp, r0               \n"
        "isb                       \n"
        "bx r14                    \n" /* 异常发生时,R14中保存异常返回标志,包括返回后进入线程模式还是处理器模式、
                                           使用PSP堆栈指针还是MSP堆栈指针，当调用 bx r14指令后，硬件会知道要从异常返回，
                                           然后出栈，这个时候堆栈指针PSP已经指向了新任务堆栈的正确位置，
                                           当新任务的运行地址被出栈到PC寄存器后，新的任务也会被执行。*/
        "nop                       \n"
        :
        : [mask] "i"(configMAX_SYSCALL_INTERRUPT_PRIORITY)
        : "memory");
}

#endif /* compiler switch */

/*
*************************************************************************
*                             临界段相关函数
*************************************************************************
*/
void vPortEnterCritical(void)
{
    portDISABLE_INTERRUPTS();
    uxCriticalNesting++;

    /* This is not the interrupt safe version of the enter critical function so
    assert() if it is being called from an interrupt context.  Only API
    functions that end in "FromISR" can be used in an interrupt.  Only assert if
    the critical nesting count is 1 to protect against recursive calls if the
    assert function also uses a critical section. */
    if (uxCriticalNesting == 1)
    {
        // configASSERT((portNVIC_INT_CTRL_REG & portVECTACTIVE_MASK) == 0);
    }
}

void vPortExitCritical(void)
{
    // configASSERT(uxCriticalNesting);
    uxCriticalNesting--;

    if (uxCriticalNesting == 0)
    {
        portENABLE_INTERRUPTS();
    }
}
