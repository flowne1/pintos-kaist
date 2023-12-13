#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "filesys/file.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "userprog/task.h"

struct lock process_filesys_lock;

void process_init (void);
pid_t process_create_initd (const char *file_name);
pid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (pid_t);
void process_exit (void);
void process_activate (struct thread *next);

#endif /* userprog/process.h */
