#ifndef __MM_H__
#define __MM_H__

#define     MEGABYTE                0x100000
#define     KERNEL_MAX_SIZE         (2 * MEGABYTE)
#define     KERNEL_STACK_SIZE	    0x20000 
#define     EXCEPTION_STACK_SIZE	0x8000

#if __aarch64__
    #define MEM_KERNEL_START	    0x80000
#else
    #define MEM_KERNEL_START	    0x8000
#endif

#define MEM_KERNEL_END		    (MEM_KERNEL_START + KERNEL_MAX_SIZE)
#define MEM_KERNEL_STACK	    (MEM_KERNEL_END + KERNEL_STACK_SIZE)

#if BCM2835
    #define CORES               1
    #define MEM_ABORT_STACK     (MEM_KERNEL_STACK + EXCEPTION_STACK_SIZE)
    #define MEM_IRQ_STACK       (MEM_ABORT_STACK + EXCEPTION_STACK_SIZE)
    #define MEM_FIQ_STACK       (MEM_IRQ_STACK + EXCEPTION_STACK_SIZE)
#else 
    #define CORES               4
    #define MEM_ABORT_STACK     (MEM_KERNEL_STACK + KERNEL_STACK_SIZE * (CORES-1) + EXCEPTION_STACK_SIZE)
    #define MEM_IRQ_STACK       (MEM_ABORT_STACK + EXCEPTION_STACK_SIZE * (CORES-1) + EXCEPTION_STACK_SIZE)
    #define MEM_FIQ_STACK       (MEM_IRQ_STACK + EXCEPTION_STACK_SIZE * (CORES-1) + EXCEPTION_STACK_SIZE)
#endif

#endif // __MM_H__