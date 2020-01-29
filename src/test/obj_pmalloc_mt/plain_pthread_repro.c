/*
 * Copyright 2020, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *     * Neither the name of the copyright holder nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * plain_pthread_repro.c -- multithreaded issue repro code
 */

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_THREADS 32
#define MAX_OPS_PER_THREAD 1000

static unsigned Threads;
static unsigned Ops_per_thread;
static unsigned Ops_per_sleep;

struct action {
	volatile uint64_t val;
	pthread_mutex_t lock;
	pthread_cond_t cond;
};

struct root {
	struct action actions[MAX_THREADS][MAX_OPS_PER_THREAD];
};

struct worker_args {
	struct root *r;
	unsigned idx;
};

static inline void
action_sleep()
{
	struct timespec t;
	for (unsigned i = 0; i < Ops_per_sleep; ++i) {
		if (clock_gettime(CLOCK_REALTIME, &t))
			abort();
	}
}

static void *
action_cancel_worker(void *arg)
{
	struct worker_args *a = arg;

	for (unsigned i = 0; i < Ops_per_thread; ++i) {
		unsigned arr_id = a->idx / 2;
		struct action *act = &a->r->actions[arr_id][i];
		if (a->idx % 2 == 0) {
			pthread_mutex_lock(&act->lock);
			action_sleep();
			act->val = 1;
			pthread_cond_signal(&act->cond);
			pthread_mutex_unlock(&act->lock);
		} else {
			pthread_mutex_lock(&act->lock);
			while (act->val == 0)
				pthread_cond_wait(&act->cond, &act->lock);
			act->val = 0;
			pthread_mutex_unlock(&act->lock);
		}
	}

	return NULL;
}

static void
actions_clear(struct root *r)
{
	for (unsigned i = 0; i < Threads; ++i) {
		for (unsigned j = 0; j < Ops_per_thread; ++j) {
			struct action *a = &r->actions[i][j];
			pthread_mutex_destroy(&a->lock);
			pthread_cond_destroy(&a->cond);
			a->val = 0;
		}
	}
}

static void
run_worker(void *(worker_func)(void *arg), struct worker_args args[])
{
	pthread_t t[MAX_THREADS];

	for (unsigned i = 0; i < Threads; ++i)
		pthread_create(&t[i], NULL, worker_func, &args[i]);

	for (unsigned i = 0; i < Threads; ++i)
		pthread_join(t[i], NULL);
}

static unsigned
parse_unsigned(const char *str)
{
	unsigned long long val = strtoull(str, NULL, 10);
	if (val == ULLONG_MAX) {
		assert(errno == ERANGE);
		fprintf(stderr, "too big: %s", str);
		abort();
	}
	if (val > UINT_MAX) {
		fprintf(stderr, "too big: %s", str);
		abort();
	}
	return (unsigned)val;
}

int
main(int argc, char *argv[])
{
	if (argc != 4) {
		fprintf(stderr, "usage: %s <threads> <ops/t> <sleep>\n", argv[0]);
		exit(1);
	}

	Threads = parse_unsigned(argv[1]);
	if (Threads > MAX_THREADS) {
		fprintf(stderr, "Threads %d > %d\n", Threads, MAX_THREADS);
		exit(1);
	}
	Ops_per_thread = parse_unsigned(argv[2]);
	if (Ops_per_thread > MAX_OPS_PER_THREAD) {
		fprintf(stderr, "Ops per thread %d > %d\n", Threads, MAX_THREADS);
		exit(1);
	}
	Ops_per_sleep = parse_unsigned(argv[3]);

	struct root *r = malloc(sizeof(*r));
	memset(r, 0, sizeof(*r));

	struct worker_args args[MAX_THREADS];

	for (unsigned i = 0; i < Threads; ++i) {
		args[i].r = r;
		args[i].idx = i;
		for (unsigned j = 0; j < Ops_per_thread; ++j) {
			struct action *a = &r->actions[i][j];
			pthread_mutex_init(&a->lock, NULL);
			pthread_cond_init(&a->cond, NULL);
		}
	}

	run_worker(action_cancel_worker, args);
	actions_clear(r);

	free(r);

	return 0;
}
