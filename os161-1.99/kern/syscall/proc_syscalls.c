#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <array.h>
#include <synch.h>
#include <copyinout.h>
#include <vfs.h>
#include <mips/trapframe.h>
#include <limits.h>
#include "opt-A2.h"

#ifdef OPT_A2

pid_t sys_fork(struct trapframe *tf, pid_t *retval) {
  // set the initial pid
  if (curproc->pid < 0) {
    spinlock_acquire(&curproc->p_lock);
    pid_count += 1;
    curproc->pid = pid_count;
    spinlock_release(&curproc->p_lock);
  }

  // create empty child process
  struct proc *childProc = proc_create_runprogram("Child");
  KASSERT(childProc != NULL);
  
  // create new address space and store it in variable
  struct addrspace *child_addr;
  int err = as_copy(curproc->p_addrspace, &child_addr);
  if (err) {
    kprintf("Error: %s\n", strerror(err));
    proc_destroy(childProc);
    return err;
  }

  // associate new address space with child
  spinlock_acquire(&childProc->p_lock);
  childProc->p_addrspace = child_addr;
  spinlock_release(&childProc->p_lock);

  // assign PID to child
  spinlock_acquire(&childProc->p_lock);
  pid_count += 1;
  childProc->pid = pid_count;
  spinlock_release(&childProc->p_lock);

  // add assignments to parent and child
  array_add(curproc->children, childProc, NULL);
  childProc->parent = curproc;

  // create new thread
  struct trapframe *tf_c = kmalloc(sizeof(struct trapframe));
  *tf_c = *tf;
  err = thread_fork("Child thread", childProc, (void *)enter_forked_process, (void *)tf_c, childProc->pid);
  if (err) {
    kprintf("Error: %s\n", strerror(err));
    kfree(tf_c);
    return err;
  }

  // return child pid to parent
  *retval = childProc->pid;

  return 0;
}

void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;
  
  spinlock_acquire(&curproc->p_lock);
  p->exit_val = exitcode;
  array_set(exit_codes, (int)p->pid, (void *)p->exit_val);
  spinlock_release(&curproc->p_lock);

  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);

  // destroy space
  as_deactivate();
  as = curproc_setas(NULL);
  as_destroy(as);

  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);

  V(p->sem);  
  proc_destroy(p);
  
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}


int sys_getpid(pid_t *retval) {
  *retval = curproc->pid;
  return 0;
}

/* stub handler for waitpid() system call                */

int sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;
  struct proc *p = curproc;

  if (options != 0) {
    return(EINVAL);
  }

  exitstatus = (int)array_get(exit_codes, (int)pid);

  if (exitstatus != EMPTY_EXIT_CODE) {
    exitstatus = _MKWAIT_EXIT(exitstatus);
    result = copyout((void *)&exitstatus,status,sizeof(int));
    if (result) return result;
    *retval = pid;
    return 0;
  }

  // else block the child
  struct proc *child;

  // find child
  int size = array_num(p->children);
  for (int i = 0; i < size; i ++) {
    struct proc *temp = (struct proc *)array_get(p->children, i);
    if ((int)pid == (int)temp->pid) {
      child = temp;
      break;
    }
  }

  if (child == NULL) {
    return EINVAL;
  }

  // block child
  P(child->sem);

  exitstatus = (int)array_get(exit_codes, (int)pid);

  KASSERT(exitstatus != EMPTY_EXIT_CODE);

  exitstatus = _MKWAIT_EXIT(exitstatus);
  result = copyout((void *)&exitstatus,status,sizeof(int));
  // V(child->sem);
  if (result) {
    return(result);
  }
  *retval = pid;
  return 0;
}

int sys_execv(userptr_t prog, userptr_t argv) {
  struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;

  char *progname =  kmalloc(PATH_MAX);
  size_t *accSize;
  result = copyinstr(prog, (void*)progname, PATH_MAX, accSize);
  if (result) {
    kfree(progname);
    return result;
  }

  kprintf("%s\n", progname);

  char ** argsArray = (char **)argv;
  int argc = 0;
  while (argsArray[argc] != NULL) argc ++;

  char **arguments = kmalloc(argc * sizeof(char*));

  for (int i = 0; i < argc; i ++) {
    size_t argSize = (strlen(argsArray[i]) + 1) * sizeof(char);
    arguments[i] = kmalloc(argSize);
    result = copyin((userptr_t)argsArray[i], (void *)arguments[i], argSize);
  }

  // int argc = 0;
  // while(true) {
  //   char* arg = NULL;
  //   // arguments[argc] = kmalloc(sizeof(char*));
  //   copyin(argv+argc*sizeof(char*), &arg, sizeof(char*));
	// 	arguments[argc] = arg;
	// 	if(arg != NULL) argc++;
  //   else break;
	// }

  int sumOfArglengths = 0;
  int arg_lengths[argc];

  for (int i = 0; i < argc; i ++) {
    sumOfArglengths += strlen(arguments[i]);
    arg_lengths[i] = strlen(arguments[i]) + 1;
  }

  for (int i = 0; i < argc; i ++) {
    kprintf("%s\n", arguments[i]);
  }

	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) return result;


	KASSERT(v != NULL);

	// Create a new address space
	as = as_create();
	if (as ==NULL) {
		vfs_close(v);
		return ENOMEM;
	}


  // Switch to it and activate it.
	curproc_setas(as);
	as_activate();

  // load executable
	result = load_elf(v, &entrypoint);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		vfs_close(v);
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);

  // copy arguments from kernel into user space

  // for (int i = 0; i < argc; i ++) {
  //   copyout(arguments, (userptr_t)argsptr, sizeof(char **));
  // }


  // copy arguments from the user space into the new address space
  result = as_define_stack(as, &stackptr);
    if (result) {
      /* p_addrspace will go away when curproc is destroyed */
      return result;
    }
  // delete old address space

  int stacksize = (argc + 1)* 8 + ROUNDUP(sumOfArglengths + argc, 8);
  stackptr -= stacksize;
  kprintf("Stack ptr: %x\n", stackptr);
  int arg_loc = stackptr + (argc + 1)*8;

  for (int i = 0; i <= argc; i ++) {
    if (i == argc) arg_loc = (int)NULL;
    result = copyout((void *)arg_loc, (userptr_t)(stackptr + i*8), sizeof(char **));
    kprintf("Address %x: %x\n", stackptr + i*8, arg_loc);
    if (i < argc) arg_loc += arg_lengths[i];
  }

  arg_loc = stackptr + (argc + 1) * 8;

  for (int i = 0; i < argc; i ++) {
    size_t size = 0;
    copyoutstr(arguments[i], (userptr_t)arg_loc, arg_lengths[i], &size);
    kprintf("Address %x: %s\n", arg_loc, arguments[i]);
    arg_loc += arg_lengths[i];
  }

	// fix first two parameters here
	enter_new_process(argc, (userptr_t)stackptr, stackptr, entrypoint);
	
	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
}


#else // BEFORE A2

  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
  (void)exitcode;

  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);

  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);

  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
  proc_destroy(p);
  
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}


/* stub handler for getpid() system call                */
int sys_getpid(pid_t *retval) {
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
  *retval = 1;
  return(0);
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;

  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.   
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  */

  if (options != 0) {
    return(EINVAL);
  }
  /* for now, just pretend the exitstatus is 0 */
  exitstatus = 0;
  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}

#endif // A2
