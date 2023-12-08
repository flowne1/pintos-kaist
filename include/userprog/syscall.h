#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
//
#include <stdint.h>
#include <stdbool.h>

void syscall_init (void);

//
bool syscall_create (const char *file, unsigned initial_size);
bool syscall_remove (const char *file);
int syscall_open (const char *file);


#endif /* userprog/syscall.h */
