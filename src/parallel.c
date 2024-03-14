// SPDX-License-Identifier: BSD-3-Clause

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>

#include "os_graph.h"
#include "os_threadpool.h"
#include "log/log.h"
#include "utils.h"

// mutexes
#include <pthread.h>

// from serial implementation
#include "os_graph.h"
#include "log/log.h"
#include "utils.h"

#define NUM_THREADS 4

static int sum;
static os_graph_t *graph;
static os_threadpool_t *tp;
/* TODO: Define graph synchronization mechanisms. */

// use 2 mutexes, one for the sum and one for the graph
pthread_mutex_t graph_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t sum_mutex = PTHREAD_MUTEX_INITIALIZER;

/* TODO: Define graph task argument. */
struct graph_task_arg {
	unsigned int node_idx;
};

static void process_node(unsigned int idx);

static void process_node_voidptr(void *arg)
{
	struct graph_task_arg *graph_arg = (struct graph_task_arg *)arg;
	process_node(graph_arg->node_idx);
}

static void process_node(unsigned int idx)
{
	/* TODO: Implement thread-pool based processing of graph. */
	// use mutexes
	os_node_t *current_node = graph->nodes[idx];
	// 
	struct graph_task_arg *arg;
	os_task_t *task;

	bool is_not_visited;

	// more threads would blindly assume that the node is not visited and would both try to process it

	// so we need to lock the mutex grapg_mutex
	pthread_mutex_lock(&graph_mutex);
	if(graph->visited[idx] != NOT_VISITED) {
		// unlock the mutex
		pthread_mutex_unlock(&graph_mutex);
		return;
	}
	graph->visited[idx] = PROCESSING;

	// keep the mutex locked for as little as possible
	pthread_mutex_unlock(&graph_mutex);

	// lock the mutex so that the sum is not modified by multiple threads
	pthread_mutex_lock(&sum_mutex);
	sum += current_node->info;
	// keep the mutex locked for as little as possible
	pthread_mutex_unlock(&sum_mutex);

	// BFS
	for (unsigned int i = 0; i < current_node->num_neighbours; i++)
	{
		// before any thread can process a node, it must check if it is visited
		// lock the mutex before checking if the node is visited
		pthread_mutex_lock(&graph_mutex);
		is_not_visited = graph->visited[current_node->neighbours[i]] == NOT_VISITED;

		// unlock the mutex after checking if the node is visited
		pthread_mutex_unlock(&graph_mutex);
		if (is_not_visited)
		{
			// create the task
			arg = malloc(sizeof(*arg));
			DIE(arg == NULL, "malloc");

			// set the index in the argument
			arg->node_idx = current_node->neighbours[i];

			// create the task
			task = create_task((void (*)(void *))process_node_voidptr, arg, free);

			// enqueue the task
			enqueue_task(tp, task);
		}
	}

	// lock the mutex before marking the node as done
	pthread_mutex_lock(&graph_mutex);
	graph->visited[idx] = DONE;

	// unlock the mutex after marking the node as done
	pthread_mutex_unlock(&graph_mutex); 
}

int main(int argc, char *argv[])
{
	FILE *input_file;

	if (argc != 2)
	{
		fprintf(stderr, "Usage: %s input_file\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	input_file = fopen(argv[1], "r");
	DIE(input_file == NULL, "fopen");

	graph = create_graph_from_file(input_file);

	/* TODO: Initialize graph synchronization mechanisms. */
	pthread_mutex_init(&graph_mutex, NULL);
	pthread_mutex_init(&sum_mutex, NULL);

	tp = create_threadpool(NUM_THREADS);
	process_node(0);
	wait_for_completion(tp);
	destroy_threadpool(tp);

	printf("%d", sum);

	return 0;
}
