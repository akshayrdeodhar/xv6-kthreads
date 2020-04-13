# xv6 Kernel Threads

> Kernel level support for user threads, and a user library which provides a
> wrapper and synchronization primitives.

Based on:
1. [Remzi Arpaci-Dusseu's specification](https://github.com/remzi-arpacidusseau/ostep-projects/tree/master/concurrency-xv6-threads)
2. [This random post which pointed out several problems and suggested solutions
   to them](https://www.ietfng.org/nwf/cs/osassign-kthreads.html)

## Features

### New Fields struct proc
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

		-----------
        	+   arg2  +
		-----------
		+   arg1  +
		-----------
        	+  0xffff +
           esp->-----------

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

