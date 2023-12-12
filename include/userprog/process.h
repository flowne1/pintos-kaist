#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "filesys/file.h"
#include "threads/thread.h"
#include "threads/synch.h"

typedef int pid_t;
typedef int fd_t;

#define MAX_FD 64
#define PID_ERROR ((pid_t)-1)
#define FD_ERROR ((fd_t)-1)

struct fd {
    fd_t fd;
    bool closed;
    struct file *file;
};

enum process_status {
    PROCESS_INIT,
    PROCESS_READY,
    PROCESS_WAIT,
    PROCESS_EXITED,
    PROCESS_DYING,
    PROCESS_FAIL
};

struct task_info {
    int exit_code;
    bool exited;
}

struct task {
    char *name;                 /* Name of the process. */
    pid_t pid;                  /* Process ID. */
    struct thread *thread;      /* The thread currently running the task. */
    pid_t parent_pid;           /* PID of parent process. */
    struct list_elem elem;      /* List element for PCB */
    struct list_elem celem;     /* List element for child process. */
    struct fd fds[MAX_FD];      /* File descriptor table. */
    struct semaphore fork_lock; /* Lock for fork system call. */
    struct semaphore wait_lock; /* Lock for wait system call. */
    struct list children;       /* List of child processes. */
    struct intr_frame *if_;     /* Temporary interrupt frame. */
    struct file* executable;    /* Executable file. */
    enum process_status status;
    int exit_code;              /* Exit code. */
    void *args;                 /* Temporary argument for deterministic
                                   creation of processes. */
};

void process_init (void);
pid_t process_create_initd (const char *file_name);
pid_t process_fork (const char *name, struct intr_frame *if_);
struct task *process_find_by_pid (pid_t pid);
struct task *process_find_by_tid (tid_t tid); 
int process_exec (void *f_name);
int process_wait (pid_t);
void process_exit (void);
void process_activate (struct thread *next);

#endif /* userprog/process.h */
