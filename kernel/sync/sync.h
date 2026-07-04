#ifndef SYNC_H
#define SYNC_H

/* ── Spinlock ── */

typedef struct {
    volatile unsigned int locked;
} spinlock_t;

#define SPINLOCK_INIT { 0 }

static inline void spinlock_init(spinlock_t *lock)
{
    lock->locked = 0;
}

static inline void spinlock_lock(spinlock_t *lock)
{
    while (1) {
        unsigned int old = 1;
        __asm__ volatile (
            "xchgl %0, %1"
            : "+r" (old), "+m" (lock->locked)
            :
            : "memory"
        );
        if (old == 0)
            return;
        __asm__ volatile ("pause");
    }
}

static inline int spinlock_try_lock(spinlock_t *lock)
{
    unsigned int old = 1;
    __asm__ volatile (
        "xchgl %0, %1"
        : "+r" (old), "+m" (lock->locked)
        :
        : "memory"
    );
    return old == 0;
}

static inline void spinlock_unlock(spinlock_t *lock)
{
    __asm__ volatile (
        "movl $0, %0"
        : "+m" (lock->locked)
        :
        : "memory"
    );
}

static inline unsigned int spinlock_lock_irqsave(spinlock_t *lock)
{
    unsigned int flags;
    __asm__ volatile (
        "pushf\n\t"
        "popl %0\n\t"
        "cli"
        : "=r" (flags)
        :
        : "memory"
    );
    spinlock_lock(lock);
    return flags;
}

static inline void spinlock_unlock_irqrestore(spinlock_t *lock, unsigned int flags)
{
    spinlock_unlock(lock);
    __asm__ volatile (
        "pushl %0\n\t"
        "popf"
        :
        : "r" (flags)
        : "memory", "cc"
    );
}

/* ── Atomic operations ── */

static inline void atomic_inc(volatile unsigned int *ptr)
{
    __asm__ volatile ("lock incl %0" : "+m" (*ptr) : : "memory");
}

static inline void atomic_dec(volatile unsigned int *ptr)
{
    __asm__ volatile ("lock decl %0" : "+m" (*ptr) : : "memory");
}

static inline unsigned int atomic_xchg(volatile unsigned int *ptr, unsigned int val)
{
    __asm__ volatile ("xchgl %0, %1" : "+r" (val), "+m" (*ptr) : : "memory");
    return val;
}

/* Returns old value; caller checks if (atomic_cmpxchg(&p, old, new) == old) */
static inline unsigned int atomic_cmpxchg(volatile unsigned int *ptr,
                                          unsigned int old, unsigned int new)
{
    __asm__ volatile (
        "lock cmpxchgl %2, %1"
        : "+a" (old), "+m" (*ptr)
        : "r" (new)
        : "memory"
    );
    return old;
}

/* ── Memory barriers ── */

static inline void mb(void)
{
    __asm__ volatile ("mfence" : : : "memory");
}

static inline void rmb(void)
{
    __asm__ volatile ("lfence" : : : "memory");
}

static inline void wmb(void)
{
    __asm__ volatile ("sfence" : : : "memory");
}

#endif
