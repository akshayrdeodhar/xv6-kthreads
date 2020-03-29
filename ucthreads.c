#include "types.h"
#include "param.h"
#include "user.h"

// Cotton Threads

#define PGSIZE 4096

int 
cthread_create(cthread_t *thread, int (*fn)(void *, void *), 
	       void *arg1, void *arg2)
{
  thread->stack = malloc(PGSIZE) + PGSIZE;
  if(thread->stack == 0){
    return -1;
  }

  // little magic cheating here?
  // breaking the kernel abstraction?
  // top - 04: arg2
  // top - 08: arg1
  // top - 12: return address..

  //*(thread_stack - 12) = exit;

  thread->pid = clone(fn, arg1, arg2, thread->stack, 0);
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

void
lock_init(lock_t *lk)
{
  lk->turn = 0;
  lk->ticket = 0;
}

void 
lock_acquire(lock_t *lk)
{
  uint ticket;
  ticket = xaddl(&lk->ticket, 1);

  __sync_synchronize();

  while(lk->turn != ticket)
    ;

}

void
lock_release(lock_t *lk)
{
  __sync_synchronize();
  lk->turn += 1;
}
