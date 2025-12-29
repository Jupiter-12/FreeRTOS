#ifndef PORTMACRO_H
#define PORTMACRO_H

#include <stdint.h>

#include "FreeRTOSConfig.h"

/* 数据类型重定义 */
#define portCHAR       char
#define portFLOAT      float
#define portDOUBLE     double
#define portLONG       long
#define portSHORT      short
#define portSTACK_TYPE uint32_t
#define portBASE_TYPE  long

typedef portSTACK_TYPE StackType_t;
typedef long           BaseType_t;
typedef unsigned long  UBaseType_t;

#if (configUSE_16_BIT_TICKS == 1)
typedef uint16_t TickType_t;
#define portMAX_DELAY (TickType_t)0xffff
#else
typedef uint32_t TickType_t;
#define portMAX_DELAY (TickType_t)0xffffffffUL
#endif

/*
 * 中断控制状态寄存器：0xe000ed04
 * Bit 28 PENDSVSET: PendSV 悬起位
 */
#define portNVIC_INT_CTRL_REG  (*((volatile uint32_t *)0xe000ed04))
#define portNVIC_PENDSVSET_BIT (1UL << 28UL)

#define portSY_FULL_READ_WRITE (15)

#define portYIELD()                                     \
    {                                                   \
        /* 触发 PendSV，产生上下文切换 */               \
        portNVIC_INT_CTRL_REG = portNVIC_PENDSVSET_BIT; \
        __dsb(portSY_FULL_READ_WRITE);                  \
        __isb(portSY_FULL_READ_WRITE);                  \
    }

/* ==========临界区管理=============== */
#define portENABLE_INTERRUPTS()  vPortSetBASEPRI(0)  /* 不带中断保护的开中断函数 */
#define portDISABLE_INTERRUPTS() vPortRaiseBASEPRI() /* 不带返回值的关中断函数，不能嵌套，不能在中断里面使用 */

#define portSET_INTERRUPT_MASK_FROM_ISR()    ulPortRaiseBASEPRI() /* 带返回值的关中断函数，可以嵌套，可以在中断里面使用 */
#define portCLEAR_INTERRUPT_MASK_FROM_ISR(x) vPortSetBASEPRI(x)   /* 带中断保护的开中断函数 */

#define portENTER_CRITICAL() vPortEnterCritical() /* 进入临界段，不带中断保护版本，不能嵌套 */
#define portEXIT_CRITICAL()  vPortExitCritical()  /* 退出临界段，不带中断保护版本，不能嵌套 */
/* ================================== */

/* 兼容 ARMCC / GCC / Clang：建议放头文件里用 static inline，避免多重定义 */
#if defined(__CC_ARM) && !defined(__clang__)
/* ARMCC(RVDS) 语法：保持原样 */
#ifndef __dsb
#define __dsb(x) __asm { dsb }
#endif /* __dsb */
#ifndef __isb
#define __isb(x) __asm { isb }
#endif /* __isb */

/* Inline 兼容 ARMCC(RVDS) 语法：保持原样 */
#define portINLINE __inline
#ifndef portFORCE_INLINE
#define portFORCE_INLINE __forceinline
#endif /* portFORCE_INLINE */

static portFORCE_INLINE void vPortRaiseBASEPRI(void)
{
    uint32_t ulNewBASEPRI = configMAX_SYSCALL_INTERRUPT_PRIORITY;
    __asm
    {
        msr basepri, ulNewBASEPRI
        dsb
        isb
    }
}

static portFORCE_INLINE uint32_t ulPortRaiseBASEPRI(void)
{
    uint32_t ulReturn, ulNewBASEPRI = configMAX_SYSCALL_INTERRUPT_PRIORITY;
    __asm
    {
        mrs ulReturn, basepri
        msr basepri, ulNewBASEPRI
        dsb
        isb
    }
    return ulReturn;
}

static portFORCE_INLINE void vPortSetBASEPRI(uint32_t ulBASEPRI)
{
    __asm
    {
        msr basepri, ulBASEPRI
    }
}

static portFORCE_INLINE void vPortClearBASEPRIFromISR(void)
{
    __asm
    {
        /* Set BASEPRI to 0 so no interrupts are masked.  This function is only
        used to lower the mask in an interrupt, so memory barriers are not
        used. */
		msr basepri, #0
    }
}
#elif defined(__GNUC__) || defined(__clang__)
/* 兼容 ARMCC 的 __dsb/__isb 与 GCC/Clang 的 CMSIS __DSB/__ISB */
/* 这些头在 STM32/CMSIS 工程里通常可用；若你工程头文件路径不同，改成能找到的那个即可 */
// #include "cmsis_armclang.h"
#ifndef __dsb
#define __dsb(x) __asm volatile("dsb 0xF" ::: "memory")
#endif /* __dsb */
#ifndef __isb
#define __isb(x) __asm volatile("isb 0xF" ::: "memory")
#endif /* __isb */

/* Inline 兼容 */
#define portINLINE inline
#ifndef portFORCE_INLINE
#define portFORCE_INLINE __attribute__((always_inline)) inline
#endif /* portFORCE_INLINE */

static portFORCE_INLINE void vPortRaiseBASEPRI(void)
{
    // __set_BASEPRI((uint32_t)configMAX_SYSCALL_INTERRUPT_PRIORITY);
    // __DSB();
    // __ISB();

    __asm volatile(
        "msr basepri, %0   \n"
        "dsb 0xF           \n"
        "isb 0xF           \n"
        :
        : "r"(configMAX_SYSCALL_INTERRUPT_PRIORITY)
        : "memory");
}

static portFORCE_INLINE uint32_t ulPortRaiseBASEPRI(void)
{
    // uint32_t ulReturn = __get_BASEPRI();

    // __set_BASEPRI((uint32_t)configMAX_SYSCALL_INTERRUPT_PRIORITY);
    // __DSB();
    // __ISB();

    // return ulReturn;

    uint32_t ulReturn, ulNewBASEPRI = configMAX_SYSCALL_INTERRUPT_PRIORITY;

    __asm volatile(
        "mrs %0, basepri   \n"
        "msr basepri, %1   \n"
        "dsb 0xF           \n"
        "isb 0xF           \n"
        : "=r"(ulReturn)
        : "r"(ulNewBASEPRI)
        : "memory");

    return ulReturn;
}

static portFORCE_INLINE void vPortSetBASEPRI(uint32_t ulBASEPRI)
{
    // __set_BASEPRI(ulBASEPRI);

    __asm volatile(
        "msr basepri, %0   \n"
        :
        : "r"(ulBASEPRI)
        : "memory");
}

static portFORCE_INLINE void vPortClearBASEPRIFromISR(void)
{
    // __set_BASEPRI(0);

    __asm volatile(
        /* Set BASEPRI to 0 so no interrupts are masked.  This function is only
        used to lower the mask in an interrupt, so memory barriers are not
        used. */
        "msr basepri, %0   \n"
        :
        : "r"(0U)
        : "memory");
}
#else

#define portINLINE inline
#ifndef portFORCE_INLINE
#define portFORCE_INLINE inline
#endif /* portFORCE_INLINE */

#endif /* defined(__CC_ARM) && !defined(__clang__) */

#define portTASK_FUNCTION(vFunction, pvParameters) void vFunction(void *pvParameters)

#endif /* PORTMACRO_H */
