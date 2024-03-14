// SPDX-License-Identifier: BSD-3-Clause

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>

#include "os_threadpool.h"
#include "log/log.h"
#include "utils.h"

/* Create a task that would be executed by a thread. */
os_task_t *create_task(void (*action)(void *), void *arg, void (*destroy_arg)(void *))
{
	os_task_t *t;

	t = malloc(sizeof(*t));
	DIE(t == NULL, "malloc");

	t->action = action;		// the function
	t->argument = arg;		// arguments for the function
	t->destroy_arg = destroy_arg;	// destroy argument function

	return t;
}

/* Destroy task. */
void destroy_task(os_task_t *t)
{
	if (t->destroy_arg != NULL)
		t->destroy_arg(t->argument);
	free(t);
}

/* Put a new task to threadpool task queue. */
void enqueue_task(os_threadpool_t *tp, os_task_t *t)
{
	assert(tp != NULL);
	assert(t != NULL);


	/* TODO: Enqueue task to the shared task queue. Use synchronization. */
	// add the task to the queue
	pthread_mutex_lock(&tp->mutex);
	list_add_tail(&tp->head, &t->list);

	pthread_mutex_unlock(&tp->mutex);

	// signal the threads that there is a new task
	// wake up ONE thread. the thread will then dequeue the task and execute it.
	pthread_cond_signal(&tp->conditional);
}

/*
 * Check if queue is empty.
 * This function should be called in a synchronized manner.
 */
static int queue_is_empty(os_threadpool_t *tp)
{
	return list_empty(&tp->head);
}

/*
 * Get a task from threadpool task queue.
 * Block if no task is available.
 * Return NULL if work is complete, i.e. no task will become available,
 * i.e. all threads are going to block.
 */

os_task_t *dequeue_task(os_threadpool_t *tp)
{
	os_task_t *t;

	/* TODO: Dequeue task from the shared task queue. Use synchronization. */

	// wait for a task to be added to the queue
	pthread_mutex_lock(&tp->mutex);
	while (queue_is_empty(tp) && !tp->end_pool_on_empty) //loop exits when: 1. the queue has at least a task 2. the work is done (threads join)
		pthread_cond_wait(&tp->conditional, &tp->mutex); //this will wait for either: 1. a task to be added to the queue 2. the work to be done

	// if the queue is empty and the work is done, return NULL
	if (queue_is_empty(tp) && tp->end_pool_on_empty) {
		pthread_mutex_unlock(&tp->mutex);
		return NULL;
	}

	// get the first task from the queue
	t = list_entry(tp->head.next, os_task_t, list);
	list_del(tp->head.next);
	pthread_mutex_unlock(&tp->mutex);

	return t;
}

/* Loop function for threads */
static void *thread_loop_function(void *arg)
{
	os_threadpool_t *tp = (os_threadpool_t *) arg;

	while (1) {
		os_task_t *t;

		t = dequeue_task(tp);
		if (t == NULL)
			break;
		t->action(t->argument);
		destroy_task(t);
	}

	return NULL;
}

/* Wait completion of all threads. This is to be called by the main thread. */
void wait_for_completion(os_threadpool_t *tp)
{
	/* TODO: Wait for all worker threads. Use synchronization. */

	//wait for all the threads to finish their work
	pthread_mutex_lock(&tp->mutex);

	// set the flag to end the pool when the queue is empty
	tp->end_pool_on_empty = 1;

	// wake up ALL the threads
	// the while loop in dequeue_task will exit if it was waiting for a task to be added to the queue
	pthread_cond_broadcast(&tp->conditional); 
	pthread_mutex_unlock(&tp->mutex);

	/* Join all worker threads. */
	for (unsigned int i = 0; i < tp->num_threads; i++)
		pthread_join(tp->threads[i], NULL);
}

/* Create a new threadpool. */
os_threadpool_t *create_threadpool(unsigned int num_threads)
{
	os_threadpool_t *tp = NULL;
	int rc;

	tp = malloc(sizeof(*tp));
	DIE(tp == NULL, "malloc");

	list_init(&tp->head);

	/* TODO: Initialize synchronization data. */
	pthread_mutex_init(&tp->mutex, NULL);
	pthread_cond_init(&tp->conditional, NULL);
	tp->end_pool_on_empty = 0;

	// create the threads
	tp->num_threads = num_threads;
	tp->threads = malloc(num_threads * sizeof(*tp->threads));
	DIE(tp->threads == NULL, "malloc");
	for (unsigned int i = 0; i < num_threads; ++i) {
		rc = pthread_create(&tp->threads[i], NULL, &thread_loop_function, (void *) tp);
		DIE(rc < 0, "pthread_create");
	}

	return tp;
}

/* Destroy a threadpool. Assume all threads have been joined. */
void destroy_threadpool(os_threadpool_t *tp)
{
	os_list_node_t *n, *p;

	/* TODO: Cleanup synchronization data. */
	pthread_mutex_destroy(&tp->mutex);
	pthread_cond_destroy(&tp->conditional);

	list_for_each_safe(n, p, &tp->head) {
		list_del(n);
		destroy_task(list_entry(n, os_task_t, list));
	}

	free(tp->threads);
	free(tp);
}
