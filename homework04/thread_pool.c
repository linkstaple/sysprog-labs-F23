#include "thread_pool.h"
#include <pthread.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

struct thread_pool;
struct thread_task;
struct thread_pool_task;

struct thread_task {
	thread_task_f function;
	void *arg;
	
	struct thread_pool_task *tp_task;
};

struct thread_pool_task {
	struct thread_task *thread_task;

	pthread_cond_t end_cond;
	void *result;

	bool is_finished;
	bool is_running;
	bool is_pushed;
	bool is_joined;
	struct thread_pool_task *next;
	struct thread_pool *pool;
};

struct thread_pool {
	pthread_t *threads;
	int max_threads_cnt;
	int threads_cnt;
	int active_threads_cnt;

	bool is_active;

	struct thread_pool_task *task;
	int tasks_cnt;

	pthread_mutex_t mutex;
	pthread_cond_t work_cond;
	pthread_cond_t thread_finish_cond;
};

void*
thread_pool_worker(void *arg);

struct thread_pool_task*
get_task(struct thread_pool *pool);

struct thread_pool_task*
make_thread_pool_task(struct thread_task *thread_task)
{
	struct thread_pool_task *task = (struct thread_pool_task*) malloc(sizeof(struct thread_pool_task));
	task->thread_task = thread_task;
	task->is_finished = false;
	task->is_running = false;
	task->is_pushed = false;
	task->is_joined = false;
	task->next = NULL;
	task->pool = NULL;
	
	pthread_cond_init(&task->end_cond, NULL);

	thread_task->tp_task = task;

	return task;
}

int
thread_pool_new(int max_thread_count, struct thread_pool **pool)
{
	if (max_thread_count > TPOOL_MAX_THREADS || max_thread_count <= 0) {
		return TPOOL_ERR_INVALID_ARGUMENT;
	}

	struct thread_pool *new_pool = (struct thread_pool*) malloc(sizeof(struct thread_pool));
	new_pool->threads = (pthread_t*) malloc(max_thread_count * sizeof(pthread_t));
	new_pool->max_threads_cnt = max_thread_count;
	new_pool->threads_cnt = 0;
	new_pool->tasks_cnt = 0;
	new_pool->is_active = true;
	new_pool->active_threads_cnt = 0;

	pthread_mutex_init(&new_pool->mutex, NULL);
	pthread_cond_init(&new_pool->work_cond, NULL);
	pthread_cond_init(&new_pool->thread_finish_cond, NULL);

	*pool = new_pool;
	return 0;
}

int
thread_pool_thread_count(const struct thread_pool *pool)
{
	return pool->threads_cnt;
}

int
thread_pool_delete(struct thread_pool *pool)
{
	pthread_mutex_lock(&pool->mutex);

	if (pool->tasks_cnt > 0) {
		pthread_mutex_unlock(&pool->mutex);
		return TPOOL_ERR_HAS_TASKS;
	}

	pool->is_active = false;
	pthread_cond_broadcast(&pool->work_cond);
	while (pool->threads_cnt != 0)
	{
		pthread_cond_wait(&pool->thread_finish_cond, &pool->mutex);
	}

	pthread_mutex_unlock(&pool->mutex);

	pthread_mutex_destroy(&pool->mutex);
	pthread_cond_destroy(&pool->work_cond);
	pthread_cond_destroy(&pool->thread_finish_cond);

	free(pool->threads);
	free(pool);

	return 0;
}

int
thread_pool_push_task(struct thread_pool *pool, struct thread_task *task)
{
	pthread_mutex_lock(&pool->mutex);

	if (pool->tasks_cnt == TPOOL_MAX_TASKS)
	{
		pthread_mutex_unlock(&pool->mutex);
		return TPOOL_ERR_TOO_MANY_TASKS;
	}

	if (pool->active_threads_cnt == pool->threads_cnt && pool->threads_cnt < pool->max_threads_cnt)
	{
		pthread_create(&pool->threads[pool->threads_cnt], NULL, thread_pool_worker, pool);
		pool->threads_cnt++;
	}

	struct thread_pool_task *tp_task = make_thread_pool_task(task);

	pool->tasks_cnt++;
	tp_task->is_pushed = true;
	tp_task->pool = pool;
	tp_task->next = pool->task;
	pool->task = tp_task;

	pthread_cond_broadcast(&pool->work_cond);
	pthread_mutex_unlock(&pool->mutex);

	return 0;
}

int
thread_task_new(struct thread_task **task, thread_task_f function, void *arg)
{
	struct thread_task *new_task = (struct thread_task*) malloc(sizeof(struct thread_task));

	new_task->function = function;
	new_task->arg = arg;
	new_task->tp_task = NULL;

	*task = new_task;
	return 0;
}

bool
thread_task_is_finished(const struct thread_task *task)
{
	return task->tp_task == NULL ? false : task->tp_task->is_finished;
}

bool
thread_task_is_running(const struct thread_task *task)
{
	return task->tp_task == NULL ? false : task->tp_task->is_running;
}

int
thread_task_join(struct thread_task *task, void **result)
{
	if (task->tp_task == NULL)
	{
		return TPOOL_ERR_TASK_NOT_PUSHED;
	}
	
	struct thread_pool *pool = task->tp_task->pool;
	pthread_mutex_lock(&pool->mutex);
	struct thread_pool_task *tp_task = task->tp_task;

	tp_task->is_joined = true;

	if (!tp_task->is_finished)
	{
		pthread_cond_wait(&(tp_task->end_cond), &pool->mutex);
	}

	*result = tp_task->result;
	pthread_mutex_unlock(&pool->mutex);

	return 0;
}

void *
thread_pool_worker(void *arg)
{
	struct thread_pool *pool = (struct thread_pool*) arg;

	while (1)
	{
		pthread_mutex_lock(&pool->mutex);

		struct thread_pool_task *tp_task;

		while ((tp_task = get_task(pool)) == NULL && pool->is_active)
		{
			pthread_cond_wait(&pool->work_cond, &pool->mutex);
		}

		pool->active_threads_cnt++;
		if (!pool->is_active) break;

		tp_task->is_running = true;
		pthread_mutex_unlock(&pool->mutex);
		tp_task->result = tp_task->thread_task->function(tp_task->thread_task->arg);

		pthread_mutex_lock(&pool->mutex);
		tp_task->is_finished = true;
		tp_task->is_running = false;
		pthread_cond_signal(&tp_task->end_cond);
		pool->active_threads_cnt--;
		pthread_mutex_unlock(&pool->mutex);
	}

	pool->threads_cnt--;
	pool->active_threads_cnt--;
	pthread_cond_signal(&pool->thread_finish_cond);
	pthread_mutex_unlock(&pool->mutex);

	return NULL;
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
	struct thread_pool_task *tp_task = task->tp_task;
	if (tp_task == NULL)
	{
		free(task);
		return 0;
	}

	struct thread_pool *pool = tp_task->pool;
	pthread_mutex_lock(&pool->mutex);

	struct thread_pool_task *tp_it = pool->task;
	while (tp_it != NULL)
	{
		if (tp_it->thread_task == task && !tp_it->is_joined)
		{
			pthread_mutex_unlock(&pool->mutex);
			return TPOOL_ERR_TASK_IN_POOL;
		}

		tp_it = tp_it->next;
	}
	int deleted_tasks_cnt = 0;

	tp_it = pool->task;
	while (tp_it != NULL && tp_it->thread_task == task)
	{
		struct thread_pool_task *tp_next = tp_it->next;
		struct thread_pool_task *tp_copy = tp_it;

		pool->task = tp_next;
		tp_it = tp_next;
		pthread_cond_destroy(&tp_copy->end_cond);
		free(tp_copy);
		deleted_tasks_cnt++;
	}

	while (tp_it != NULL)
	{
		struct thread_pool_task *tp_next = tp_it->next;

		if (tp_next != NULL && tp_next->thread_task == task)
		{
			tp_it->next = tp_next->next;
			pthread_cond_destroy(&tp_next->end_cond);
			free(tp_next);
			deleted_tasks_cnt++;
		}
		
		tp_it = tp_it->next;
	}

	free(task);
	pool->tasks_cnt -= deleted_tasks_cnt;
	pthread_mutex_unlock(&pool->mutex);

	return 0;
}

struct thread_pool_task*
get_task(struct thread_pool *pool)
{
	struct thread_pool_task *task = pool->task;
	while (task != NULL && (task->is_running || task->is_finished))
	{
		task = task->next;
	}

	return task;
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
