#include "thread_pool.h"
#include <pthread.h>
#include <string.h>
#include <stdbool.h>

struct thread_task {
	thread_task_f function;
	void *arg;

	bool is_finished;
	bool is_running;
};

struct thread_pool {
	pthread_t *threads;

	int threads_count;
};

int
thread_pool_new(int max_thread_count, struct thread_pool **pool)
{
	if (max_thread_count > TPOOL_MAX_THREADS || max_thread_count <= 0) {
		return TPOOL_ERR_INVALID_ARGUMENT;
	}

	struct thread_pool *new_pool = (struct thread_pool*) malloc(sizeof(struct thread_pool));
	new_pool->threads = (pthread_t*) malloc(max_thread_count * sizeof(pthread_t));
	new_pool->threads_count = 0;

	*pool = new_pool;
	return 0;
}

int
thread_pool_thread_count(const struct thread_pool *pool)
{
	return pool->threads_count;
}

int
thread_pool_delete(struct thread_pool *pool)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)pool;
	return TPOOL_ERR_NOT_IMPLEMENTED;
}

int
thread_pool_push_task(struct thread_pool *pool, struct thread_task *task)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)pool;
	(void)task;
	return TPOOL_ERR_NOT_IMPLEMENTED;
}

int
thread_task_new(struct thread_task **task, thread_task_f function, void *arg)
{
	struct thread_task *new_task = (struct thread_task*) malloc(sizeof(struct thread_task));

	new_task->function = function;
	new_task->arg = arg;
	new_task->arg;

	*task = new_task;
	return 0;
}

bool
thread_task_is_finished(const struct thread_task *task)
{
	return task->is_finished;
}

bool
thread_task_is_running(const struct thread_task *task)
{
	return task->is_running;
}

int
thread_task_join(struct thread_task *task, void **result)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)task;
	(void)result;
	return TPOOL_ERR_NOT_IMPLEMENTED;
}

#ifdef NEED_TIMED_JOIN

int
thread_task_timed_join(struct thread_task *task, double timeout, void **result)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)task;
	(void)timeout;
	(void)result;
	return TPOOL_ERR_NOT_IMPLEMENTED;
}

#endif

int
thread_task_delete(struct thread_task *task)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)task;
	return TPOOL_ERR_NOT_IMPLEMENTED;
}

#ifdef NEED_DETACH

int
thread_task_detach(struct thread_task *task)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)task;
	return TPOOL_ERR_NOT_IMPLEMENTED;
}

#endif
