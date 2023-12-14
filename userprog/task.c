#include "userprog/task.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "threads/malloc.h"
#include "threads/init.h"
#include "threads/palloc.h"
#ifdef VM
#include "vm/vm.h"
#endif

/* List of processes. */
static struct list process_list;

/* Lock used by allocate_pid(). */
static struct lock pid_lock;

static pid_t allocate_pid (void);
static void init_process (struct task *task);

void task_init (void) {
	lock_init (&pid_lock);
	list_init (&process_list);
}

struct task *
task_create (const char *file_name, struct thread* thread) {
	char *fn_copy;
	struct task *t;
	pid_t pid;
	char *args_begin;
	size_t name_len;

	t = palloc_get_page (PAL_ZERO);
	if (t == NULL) {
		return NULL;
	}
	init_process (t);
	
	args_begin = strchr (file_name, ' ');
	name_len = args_begin - file_name + 1;
	if (args_begin == NULL) {
		name_len = strlen (file_name) + 1;
	}

	fn_copy = malloc (name_len + 1);
	if (fn_copy == NULL) {
		palloc_free_page (t);
		return NULL;
	}

	strlcpy (fn_copy, file_name, name_len);
	t->name = fn_copy;
	t->thread = thread;
	pid = t->pid = allocate_pid ();
	list_push_back (&process_list, &t->elem);

	if (thread != NULL) {
		t->status = PROCESS_READY;
	}
	return t;
}

bool
task_set_thread (struct task *task, struct thread *thrd) {
	if (thrd == NULL || task == NULL) {
		return false;
	}

	if (task->thread != NULL) {
		return false;
	}

	task->thread = thrd;
	task_set_status (task, PROCESS_READY);
	return true;
}

bool
task_set_status (struct task *task, enum process_status status) {
	ASSERT (status > PROCESS_MIN && status < PROCESS_MAX);

    if (task == NULL) {
		return false;
	}

	task->status = status;
	return true;
}

void
task_inherit_initd (struct task *t) {
	struct task *initd = task_find_by_pid (1);
	struct list_elem *e = NULL;

    ASSERT (initd != NULL);

    if (initd == t) {
        return;
    }

	for (e = list_begin (&t->children); e != list_end(&t->children);) {
		struct task *child = list_entry (e, struct task, celem);
		if (child->status == PROCESS_DYING || child->status == PROCESS_EXITED) {
			e = list_remove (&child->celem);
			task_free (child);
			continue;
		}

		e = list_remove (&child->celem);
		child->celem.next = NULL;
		child->celem.prev = NULL;
		child->parent_pid = initd->pid;
		list_push_back (&initd->children, &child->celem);
	}
}

void
task_free (struct task *t) {
	if (t == NULL) {
		return;
	}

	list_remove(&t->elem);
	free (t->name);
	palloc_free_page (t);
}

void 
task_cleanup (struct task *t) {
    /* Close opened files. */
	for (size_t i = 0; i < MAX_FD; i++) {
		if (!t->fds[i].closed && !t->fds[i].duplicated) {
			t->fds[i].closed = true;
			file_close (t->fds[i].file);
			t->fds[i].file = NULL;
		}
	}

	/* Close the executable file. */
	file_close (t->executable);    
}

void 
task_fork_fd (struct task *parent, struct task *child) {
    for (size_t i = 0; i < MAX_FD; i++) {
	    if (parent->fds[i].file != NULL && !parent->fds[i].duplicated) {
	    	child->fds[i].file = file_duplicate (parent->fds[i].file);
	    }

		if (parent->fds[i].duplicated) {
			child->fds[i].file = parent->fds[i].file;
		}
	    child->fds[i].closed = parent->fds[i].closed;
	    child->fds[i].fd = parent->fds[i].fd;
        child->fds[i].dup_count = parent->fds[i].dup_count;
        child->fds[i].duplicated = parent->fds[i].duplicated;
		child->fds[i].fd_map = parent->fds[i].fd_map;
		child->fds[i].stdio = parent->fds[i].stdio;
	}
}

void 
task_exit (int status) {
    struct task *task = task_find_by_tid (thread_tid ());
    if (task == NULL) {
        return;
    }

    task->exit_code = status;
    thread_exit ();
}

struct task *
task_find_by_pid (pid_t pid) {
	struct list_elem *e = NULL;
	struct task *t = NULL;
	struct task *found = NULL;
	for (e = list_begin (&process_list); e != list_end (&process_list); e = list_next (e)) {
		t = list_entry (e, struct task, elem);
		if (t->pid == pid) {
			found = t;
			break;
		}
	}

	return found; 
}

struct task *
task_find_by_tid (tid_t tid) {
	struct list_elem *e = NULL;
	struct task *t = NULL;
	struct task *found = NULL;
	for (e = list_begin (&process_list); e != list_end (&process_list); e = list_next (e)) {
		t = list_entry(e, struct task, elem);
		if (t->thread != NULL && t->thread->tid == tid) {
			found = t;
			break;
		}
	}

	return found;
}

size_t
task_child_len (struct task *t) {
    return list_size (&t->children);
}

static pid_t
allocate_pid (void) {
	static pid_t next_pid = 1;
	pid_t pid;

	lock_acquire (&pid_lock);
	pid = next_pid++;
	lock_release (&pid_lock);

	return pid;
}

static void
init_process (struct task *task) {
	ASSERT (task != NULL)

	task->name = NULL;
	task->elem.next = NULL;
	task->elem.prev = NULL;
	task->if_ = NULL;
	task->parent_pid = -1;
	task->thread = NULL;
	task->status = PROCESS_INIT;
	sema_init (&task->fork_lock, 0);
	sema_init (&task->wait_lock, 0);
	list_init (&task->children);

    for (size_t i = 0; i < 3; i++) {
		fd_init (&task->fds[i], i);
		task->fds[i].closed = false;
		task->fds[i].stdio = i;
    }

	for (size_t i = 3; i < MAX_FD; i++) {
		fd_init (&task->fds[i], i);
	}

	task->exit_code = 0;
}

fd_t
task_find_original_fd (struct task* task, int fd) {
    int parent_fd = fd;
	int depth = 0;
	while (depth != MAX_FD) {
		parent_fd = task->fds[parent_fd].fd;
		if (parent_fd == task->fds[parent_fd].fd) {
			return parent_fd;
		}
		depth++;
	}

    return -1;
}

fd_t 
task_find_fd_map (struct task *task, int fd) {
    for (size_t i = 0; i < MAX_FD; i++) {
        if (fd == task->fds[i].fd_map) {
            return i;
        }
    }

    return -1;
}

bool
task_inherit_fd (struct task *task, int fd) {
	/* Find successor first */
	int successor = -1;
	for (size_t i = 0; i < MAX_FD; i++) {
		if (task->fds[i].fd == fd && i != fd) {
			successor = i;
			break;
		}
	}

	if (successor == -1) {
		return false;
	}

	/* Update fd. */
	for (size_t i = 0; i < MAX_FD; i++) {
		if (task->fds[i].fd == fd && i != fd) {
			task->fds[i].fd = successor;
		}
	}

	/* Update succssor. */
	task->fds[successor].duplicated = false;
	task->fds[successor].dup_count = task->fds[fd].dup_count - 1;
	task->fds[successor].stdio = task->fds[fd].stdio;
	return true;
}

void
fd_init (struct fd *fdt, fd_t fd) {
	fdt->closed = true;
	fdt->fd = fd;
	fdt->fd_map = fd;
	fdt->dup_count = 0;
	fdt->duplicated = false;
	fdt->file = NULL;
	fdt->stdio = -1;
}