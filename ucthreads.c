#include "types.h"
#include "param.h"
#include "user.h"

// Cotton Threads

#define PGSIZE 4096

int 
cthread_create(cthread_t *thread, int (*fn)(void *, void *), 
	       void *arg1, void *arg2)
{
  thread->stack = (char *)malloc(PGSIZE);
  if(thread->stack == 0){
    return -1;
  }

  // little magic cheating here?
  // breaking the kernel abstraction?
  // top - 04: arg2
  // top - 08: arg1
  // top - 12: return address..

  //*(thread_stack - 12) = exit;

  thread->pid = clone(fn, arg1, arg2, thread->stack + PGSIZE, 0);
  if(thread->pid == -1){
    return -1;
  }

  return thread->pid;
}

int 
cthread_cut(cthread_t *thread)
{
  return kill(thread->pid);
}

int cthread_join(cthread_t *thread)
{
  if(join(thread->pid) != thread->pid){
    return -1;
  }
  return thread->pid;
}
  
void cthread_exit()
{
  exit();
}  

static inline uint
xaddl(volatile uint *addr, uint value)
{
  // stolen from: https://en.wikipedia.org/wiki/Fetch-and-add

  // The + in "+m" denotes a read-modify-write operand.
  asm volatile("lock; xaddl %0, %1" :
               "+r" (value), "+m" (*addr) :
                : // no input-only
               "memory");
  return value;
}

static inline uint
xchg(volatile uint *addr, uint newval)
{
  uint result;

  // The + in "+m" denotes a read-modify-write operand.
  asm volatile("lock; xchgl %0, %1" :
               "+m" (*addr), "=a" (result) :
               "1" (newval) :
               "cc");
  return result;
}

void
tlock_init(tlock_t *lk)
{
  lk->turn = 0;
  lk->ticket = 0;
}

void 
tlock_acquire(tlock_t *lk)
{
  uint ticket;
  ticket = xaddl(&lk->ticket, 1);

  __sync_synchronize();

  while(lk->turn != ticket)
    ;

}

void
tlock_release(tlock_t *lk)
{
  __sync_synchronize();
  lk->turn += 1;
}

void
slock_init(slock_t *lk)
{
  *lk = 0;
}

void 
slock_acquire(slock_t *lk)
{
  while(xchg(lk, 1) != 0)
    ;

  __sync_synchronize();
}

void
slock_release(slock_t *lk)
{
  __sync_synchronize();
  *lk = 0;
}
