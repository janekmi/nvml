/*
 * Copyright 2015-2020, Intel Corporation
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
 * obj_pmalloc_mt.c -- multithreaded test of allocator
 */
#include <stdint.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/syscall.h>

#include "unittest.h"

#define MAX_THREADS 32
#define MAX_OPS_PER_THREAD 1000

static unsigned Threads;
static unsigned Ops_per_thread;
static unsigned Tx_per_thread;

struct action {
	pthread_mutex_t lock;
	pthread_cond_t cond;
	unsigned val;
};

struct root {
	struct action actions[MAX_THREADS][MAX_OPS_PER_THREAD];
};

struct worker_args {
	struct root *r;
	unsigned idx;
};

FILE *dump;

static inline void
action_dump(int tid, unsigned thread, unsigned op,
		struct action *a, const char *comment)
{
	pthread_mutex_t *plock = (pthread_mutex_t *)&a->lock;
	struct __pthread_mutex_s *lock = &plock->__data;

	fprintf(dump, "%d -> actions[%u][%u] = {nusers: %u, owner: %d, kind: %d} (%s)\n",
			tid, thread, op,
			*((volatile unsigned *)&(lock->__nusers)),
			*((volatile int *)&(lock->__owner)),
			*((volatile int *)&(lock->__kind)),
			comment);
}

static inline int
gettid()
{
	return (int)syscall(SYS_gettid);
}

static inline void
util_mutex_init(pthread_mutex_t *mtx)
{
	int ret = pthread_mutex_init(mtx, NULL);
	if (ret) {
		errno = ret;
		exit(1);
	}
}

static inline void
util_mutex_destroy(pthread_mutex_t *mtx)
{
	int ret = pthread_mutex_destroy(mtx);
	if (ret) {
		errno = ret;
		exit(1);
	}
}

static inline void
util_mutex_lock(pthread_mutex_t *mtx)
{
	int ret = pthread_mutex_lock(mtx);
	if (ret) {
		errno = ret;
		exit(1);
	}
}

static inline void
util_mutex_unlock(pthread_mutex_t *mtx)
{
	int ret = pthread_mutex_unlock(mtx);
	if (ret) {
		errno = ret;
		exit(1);
	}
}

static inline void
util_cond_init(pthread_cond_t *cond)
{
	int ret = pthread_cond_init(cond, NULL);
	if (ret) {
		errno = ret;
		exit(1);
	}
}

static inline void
util_cond_destroy(pthread_cond_t *cond)
{
	int ret = pthread_cond_destroy(cond);
	if (ret) {
		errno = ret;
		exit(1);
	}
}

static inline void
util_cond_signal(pthread_cond_t *cond)
{
	int ret = pthread_cond_signal(cond);
	if (ret) {
		errno = ret;
		exit(1);
	}
}

static inline void
util_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mtx)
{
	int ret = pthread_cond_wait(cond, mtx);
	if (ret) {
		errno = ret;
		exit(1);
	}
}


static void *
action_cancel_worker(void *arg)
{
	struct worker_args *a = arg;
	int tid = gettid();

	for (unsigned i = 0; i < Ops_per_thread; ++i) {
		unsigned arr_id = a->idx / 2;
		struct action *act = &a->r->actions[arr_id][i];
		if (a->idx % 2 == 0) {
			util_mutex_lock(&act->lock);
			action_dump(tid, arr_id, i, act, "lock t0");

			act->val = 1;
			util_cond_signal(&act->cond);

			util_mutex_unlock(&act->lock);
			action_dump(tid, arr_id, i, act, "unlock t0");
		} else {
			util_mutex_lock(&act->lock);
			action_dump(tid, arr_id, i, act, "lock t1");

			while (act->val == 0)
				util_cond_wait(&act->cond, &act->lock);

			util_mutex_unlock(&act->lock);
			action_dump(tid, arr_id, i, act, "unlock t1");
		}
	}

	return NULL;
}

static void
actions_dump(struct root *r)
{
	int tid = gettid();

	for (unsigned i = 0; i < Threads; ++i) {
		for (unsigned j = 0; j < Ops_per_thread; ++j) {
			struct action *a = &r->actions[i][j];
			struct __pthread_mutex_s *lock =
					(struct __pthread_mutex_s *)&a->lock;
			if (lock->__nusers == 0)
				continue;
			action_dump(tid, i, j, a, "dump");
		}
	}
}

static void
run_worker(void *(worker_func)(void *arg), struct worker_args args[])
{
	os_thread_t t[MAX_THREADS];

	for (unsigned i = 0; i < Threads; ++i)
		THREAD_CREATE(&t[i], NULL, worker_func, &args[i]);

	for (unsigned i = 0; i < Threads; ++i)
		THREAD_JOIN(&t[i], NULL);
}

int
main(int argc, char *argv[])
{
	START(argc, argv, "obj_pmalloc_mt");

	if (argc != 5)
		UT_FATAL("usage: %s <threads> <ops/t> <tx/t> [file]", argv[0]);

	Threads = ATOU(argv[1]);
	if (Threads > MAX_THREADS)
		UT_FATAL("Threads %d > %d", Threads, MAX_THREADS);
	Ops_per_thread = ATOU(argv[2]);
	if (Ops_per_thread > MAX_OPS_PER_THREAD)
		UT_FATAL("Ops per thread %d > %d", Threads, MAX_THREADS);
	Tx_per_thread = ATOU(argv[3]);

	struct root *r = malloc(sizeof(*r));
	UT_ASSERTne(r, NULL);

	struct worker_args args[MAX_THREADS];

	for (unsigned i = 0; i < Threads; ++i) {
		args[i].r = r;
		args[i].idx = i;
		for (unsigned j = 0; j < Ops_per_thread; ++j) {
			struct action *a = &r->actions[i][j];
			a->val = 0;
			util_mutex_init(&a->lock);
			util_cond_init(&a->cond);
		}
	}

	dump = fopen("/dev/shm/obj_pmalloc_mt_dump", "w");
	run_worker(action_cancel_worker, args);
	actions_dump(r);
	fclose(dump);

	for (unsigned i = 0; i < Threads; ++i) {
		for (unsigned j = 0; j < Ops_per_thread; ++j) {
			struct action *a = &r->actions[i][j];
			util_mutex_destroy(&a->lock);
			util_cond_destroy(&a->cond);
		}
	}

	DONE(NULL);
}
