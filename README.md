# xv6 Kernel Threads

> Kernel level support for user threads, and a user library which provides a
> wrapper and synchronization primitives.

Based on:
1. [Remzi Arpaci-Dusseu's specification](https://github.com/remzi-arpacidusseau/ostep-projects/tree/master/concurrency-xv6-threads)
2. [This random post which pointed out several problems and suggested solutions
   to them](https://www.ietfng.org/nwf/cs/osassign-kthreads.html)

## New fields in struct proc
1. *threadcount*: the number of threads of the process which are not ZOMBIES or
   UNUSED. This is protected by *ptable.lock*.
2. *vlock*: a spinlock which protects the address space size and the page
   directory- when vlock is held, *there will be no changes to or usage of sz by
another thread of execution*.
3. *tgid*: thread group ID, equal to the PID of the thread group leader
4. *process*: a pointer to the thread group leader. This is a hack- instead of
   seperating the *per-process* and *per-thread* variables, the common variables
of the process group leader are used by all threads. The process group leader is
accessed using this pointer.
5. *cwdlock*: protects access to the current working directory of the process.

## New system calls

### **clone**

	int clone(int (*fn)(void *, void *), void *a, void *b, char *stack, int flags)

Creates a *thing* which shares the memory space and working directory of the
calling process.  The *thing* is basically a *struct proc*.  The process a group
of threads *any* thread calling clone will create another thread in the same
process.

Arguments:
1. *fn* is a function pointer to the function that will run in the thread
2. *a*
3. and *b*- arguments which will be passed to *fn*
4. *stack* the **top** of a page-sized stack. The function checks whether page
   lies in the address space of the caller. 
5. *flags* Ignored.

The child process shares the address space, which means that anything which is
on the *stack*, *heap*, or *data* of the process can be accessed by the child
created by clone.

The stack is constructed as follows:

		top->	-----------
        	 	|   arg2  |
		 	-----------
		 	|   arg1  |
		 	-----------
        	 	|  0xffff |
           	esp->   -----------

Then, this esp is set as the esp in the trap frame, and the address *fn* is set
as the eip.

For a thread to terminate properly, it should call *exit*. Otherwise, it will be
terminated by a page fault. For legal termination without *exit*, a system call which jumps
back into the kernel will be required. However, *the virtual address of this
system call exists in the virtual address space of* **the process**. This
address varies from process to process. So, if the kernel lays out the stack,
it **cannot** cook up a stack with a virtual address which exists in userland.
A solution to this would be writing a clone similar to fork() which continues on
the next *eip*, and then writing a userland wrapper which calls *clone*. This
would not require laying out of the stack in the kernel, and hence has not been
done to keep things interesting. That clone will probably work as follows-

	int child = clone();
	if(!child) 
		exit(fn(arg1, arg2));

The glibc clone() wrapper probably does this.

### **join**

	int join(int pid)

Waits for the thread with pid *pid* from the same process group to exit. If it
does not exist, returns -1. If the thread is not the process group leader, frees
up it's kernel stack. Holds *ptable.lock* at all times except when it is
sleep-waiting for the thread to exit. If successful, returns *pid*. 

If *exec* occurs while it is sleep-waiting, dies.

This nature is for supporting waiting for a specific thread to exit. Waiting for
any thread to exit seems useless.


### **park**

	int park(void *chan)

Causes a thread to sleep on some pointer. This is basically "block", to allow
creation of blocking (in contrast to spinning) synchronisation primitives. If
the most recent wakeup delivered to the thread is on *chan*, returns
immediately. Otherwise, sleeps on *chan*.


### **unpark**

	int unpark(int pid, void *chan)

If thread with pid *pid* is sleeping on *chan*, wakes it up. Otherwise, sets
it's *lostwakeup* attribute to *chan* while holding ptable.lock.

If some thread is sleeping due to the system call *sleep*, *lostwakeup* is still
set. This feature was initially for testing, but was retained because this
allows the following sequence

	T1: does some work
	T2: does some work
	T1: unpark(T2, chan)
	T2: does some work
	T1: proceeds independent of T2
	T2: sleeps on something else (for example I/O)
	T2: park(chan) -> return

## Changes to old system calls

### **fork**

When a thread forks, the child considers the *thread group leader* of the
calling process as it's parent. The lock on the virtual address space is held
while it is being copied. Initializes the *threadcount* to 1.

### **exec**

When a thread execs, all other threads in the process are terminated, and the
calling thread waits for them to terminate before becoming RUNNABLE. The calling
thread assumes the identity of the thread group leader before marking any of the
other threads as *killed*. All the attributes which belong to the process are
copied to the new group leader. There is a small window where these attributes
exist in the former and new group leader.

The lock on the virtual address space is held when the new process is assigned
the page directory constructed and the size, *sz*.

If two threads call exec at the same time, the one which is called first will
become the thread group leader. 

If one thread calls exec and another calls clone, *marking the new thread as
RUNNABLE, and marking all threads in the process as killed is serialized by
ptable.lock*. So either the thread is created and then terminated, or it is not
created at all.

### **wait**

Waits for an entire *process* which is the child of the current process to die.
The process which is cleaned up is the *first child process whose threadcount
becomes zero*.  Cleans up the kernel stack, resests the attributes for all
threads in this process 0, and marks their struct procs as UNUSED. *ptable.lock*
is held while checking and cleaning. The reason behind this is to allow *join*
to clean up threads before *wait* tries to clean them up, and to ensure that a
single call to *wait* touches only one child process.

If exec is called in a process, the other threads in the process (which have
been terminated by the thread calling *exec*) are cleaned *after* the new
process which runs after *exec* is complete.

> Note: *threadcount* is the number of threads in the process which are not ZOMBIE
> or UNUSED.

### **userinit**

Has to initialize the locks, and the new attributes in struct proc.


### **growproc**

This implements the system call *sbrk*. Changes the heap for the *entire
process*. The lock on the virtual address space is held during the entire
operation.

### **exit**

Causes the **calling thread** (not process) to exit. Decrements threadcount.
Wakes up any other thread which is sleep-waiting for it to exit inside *join*.

## The TOCTOU problem

When an address space is shared, and can be *modified*, problems processes
accessing data of other processes, or *gasp* kernel data arise. Dereferening of
user pointers inside the kernel might lead to panics. (Because you **cannot**
have page faults inside the kernel)

Here is an example

	T1: creates buffer *A* using sbrk()
	T1: creates thread T2 (assume that it's stack was allocated before *A*)
	T1: calls read(fd, A), sys_read checks that A[] is valid for dereferencing <- CHECK
	T2: calls sbrk() to deallocate *A*, page directory entries for *A* removed
	T1: read() dereferences *A* inside the kernel <- USE
	The End.

What is required is, *the address space should not change between the time when
the validity of a user pointer is checked and used*. Check and use should be
atomic. This neccesitates the usage of *safe copy primitives* inside the kernel,
and modification of all *read* and *write* functions to use these, and creating
a kernel-space copy of strings passed into the kernel, and using these instead
of userland strings.

The lock to be held is the processes' *vlock*, or lock on virtual memory.

### Copy string from user

Any system call that takes a user string uses a kernel-space buffer of size 512
bytes to which the string is copied to atomically with a validity check for the
user string. If the string length exceeds 512 bytes, the system call fails. This
is done using the function *copy_str_from_user*

	int copy_str_from_user(char *dst, const char *src, uint limit)

### Safe buffer copy primitives

	int copy_from_user(char *dst, const char *src, uint n)
	
	int copy_to_user(char *dst, const char *src, uint n)

These functions copy n bytes from *src* to *dst* from and to userland
respectively. These acquire *vlock*, sanitize the user pointer, and call
*memmove* while holding the lock.

### readi, writei

At this level of abstraction, the copy happens between the user buffer and the
buffer cache which contains the inode. *memmove* is replaced by the appropriate
safe copy primitive.

### piperead, pipewrite

Because of the buffer being circular, pipewrite writes bytes one by one in a
loop. Using safe copy primitives for moving a single byte is inefficient, so the
new implementation splits the bytes to be written into two parts chunk1 and
chunk2, and uses copy primitives on these. 

	 ----------------------------------------
	|  chunk2  |    unusable    |  chunk1   |
	 ---------------------------------------
  	           |                |
                  agent1          agent2

	chunk1 and chunk2 are either the bytes that can
	be read or slots that can be written to.

### consoleread, consolewrite

In consoleread, copy has to happen byte-by-byte, so copy primitives cannot be
used, and checks are inserted into the code directly. The *vlock* is held when
the function is not sleeping. It is released before sleeping on the console
lock, and acquired on waking up.

## Cotton Threads

cthreads or "Cotton Threads" is a userland threading library, which also
provides synchronization primitives.

### cthread_t
The cotton thread type. Stores a pointer to the stack and the thread's pid

### cthread\_create
	cthread_create(cthread_t *thread, int (*fn)(void *, void *), 
                       void *arg1, void *arg2)

*malloc*s a stack, and calls clone. The page obtained thus may or may not be
page-aligned (because the implementation of malloc may or may not guarentee
this). *sbrk* cannot be used because there will be no way to free the stack on
joining if the joins are not LIFO.

### cthread\_cut
	
	int cthread_cut(cthread_t *t)

Calls *kill* on the thread t. *does not wait till this happens*. Call join to
ensure that *t* has terminated.

### cthread\_join

	int cthread_join(cthread_t *t)

Calls *join* on *t*, frees it's stack, and returns.

### cthread\_exit
	
	int cthread_exit(void)

LOL.

## Ticket Lock

A FIFO spinlock which does not use a queue. Uses *xaddl* or *Fetch-and-Add*.
Using this is a bad idea, because it could as well happen that the thread which
has it's turn does not get scheduled for a long time.

Based on [remzi's
implementation](http://pages.cs.wisc.edu/~remzi/OSTEP/threads-locks.pdf) with
fetch and add code copied from
[wikipedia](https://en.wikipedia.org/wiki/Fetch-and-add). Uses the
\_\_sync\_synchronize() memory barrier.

Provides

	tlock_t (the ticket lock type)
	tlock_init(tlock_t *lock)
	tlock_acquire(tlock_t *lock)
	tlock_release(tlock_t *lock)

## Spinlock

A random contention spinlock. May cause starvation.

Provides

	slock_t
	slock_init(slock_t *lock)
	slock_acquire(slock_t *lock)
	slock_release(slock_t *lock)

## Semaphore

A blocking semaphore which does not suffer from lost wakeups. Uses *park* and
*unpark* system calls. Does not use kernel-space memory or mapping of procesess
to futexes like in linux, at the cost of two system calls when blocking or
wakeups take place. No system calls when lock is not contended or no thread is
waiting. Uses an internal *guard* spinlock and an *array-based* queue. Undefined
behaviour when the number of threads waiting exceeds the size of the queue.
Array-based queue used because the xv6 memory allocator is **thread unsafe**.

Provides

	semaphore_t
	sem_init(semaphore_t *sem, int n)
	sem_up(semaphore_t *sem)   // V or signal
	sem_down(semaphore_t *sem) // P or wait

> Users are encouraged to use semaphores for everything. (They have no choice,
> as the library does not provide *mutexes*, *condition variables* or *monitors*.) 
> But semaphores are supposed to be general anyway :)
