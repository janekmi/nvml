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

#include "file.h"
#include "obj.h"
#include "pmalloc.h"
#include "sys_util.h"
#include "unittest.h"

#define MAX_THREADS 32
#define MAX_OPS_PER_THREAD 1000
#define ALLOC_SIZE 104

#define CHUNKSIZE (1 << 20)
#define CHUNKS_PER_THREAD 3

static unsigned Threads;
static unsigned Ops_per_thread;
static unsigned Tx_per_thread;

#define PAD_SIZE 100

struct action {
	struct pobj_action pact;
	unsigned padding0[PAD_SIZE];
	os_mutex_t lock;
	unsigned padding1[PAD_SIZE];
	os_cond_t cond;
	unsigned padding2[PAD_SIZE];
};

struct root {
	uint64_t offs[MAX_THREADS][MAX_OPS_PER_THREAD];
	struct action actions[MAX_THREADS][MAX_OPS_PER_THREAD];
};

struct worker_args {
	PMEMobjpool *pop;
	struct root *r;
	unsigned idx;
};

static void *
action_cancel_worker(void *arg)
{
	struct worker_args *a = arg;

	PMEMoid oid;
	for (unsigned i = 0; i < Ops_per_thread; ++i) {
		unsigned arr_id = a->idx / 2;
		struct action *act = &a->r->actions[arr_id][i];
		if (a->idx % 2 == 0) {
			util_mutex_lock(&act->lock);
			oid = pmemobj_reserve(a->pop,
				&act->pact, ALLOC_SIZE, 0);
			UT_ASSERT(!OID_IS_NULL(oid));
			util_cond_signal(&act->cond);
			util_mutex_unlock(&act->lock);
		} else {
			util_mutex_lock(&act->lock);
			while (act->pact.heap.offset == 0)
				util_cond_wait(&act->cond, &act->lock);
			pmemobj_cancel(a->pop, &act->pact, 1);
			util_mutex_unlock(&act->lock);
		}
	}

	return NULL;
}

static void
actions_dump(struct root *r)
{
	for (unsigned i = 0; i < Threads; ++i) {
		for (unsigned j = 0; j < Ops_per_thread; ++j) {
			struct action *a = &r->actions[i][j];
			pthread_mutex_t *lock = (pthread_mutex_t *)&a->lock;
			if (lock->__data.__nusers == 0)
				continue;
			printf("actions[%u][%u] = {nusers: %u}\n", i, j, lock->__data.__nusers);
		}
	}
}

static void
actions_clear(PMEMobjpool *pop, struct root *r)
{
	for (unsigned i = 0; i < Threads; ++i) {
		for (unsigned j = 0; j < Ops_per_thread; ++j) {
			struct action *a = &r->actions[i][j];
			util_mutex_destroy(&a->lock);
			util_mutex_init(&a->lock);
			util_cond_destroy(&a->cond);
			util_cond_init(&a->cond);
			memset(&a->pact, 0, sizeof(a->pact));
			pmemobj_persist(pop, a, sizeof(*a));
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

	PMEMobjpool *pop;

	Threads = ATOU(argv[1]);
	if (Threads > MAX_THREADS)
		UT_FATAL("Threads %d > %d", Threads, MAX_THREADS);
	Ops_per_thread = ATOU(argv[2]);
	if (Ops_per_thread > MAX_OPS_PER_THREAD)
		UT_FATAL("Ops per thread %d > %d", Threads, MAX_THREADS);
	Tx_per_thread = ATOU(argv[3]);

	int exists = util_file_exists(argv[4]);
	if (exists < 0)
		UT_FATAL("!util_file_exists");

	if (!exists) {
		pop = pmemobj_create(argv[4], "TEST", (PMEMOBJ_MIN_POOL) +
			(MAX_THREADS * CHUNKSIZE * CHUNKS_PER_THREAD),
		0666);

		if (pop == NULL)
			UT_FATAL("!pmemobj_create");
	} else {
		pop = pmemobj_open(argv[4], "TEST");

		if (pop == NULL)
			UT_FATAL("!pmemobj_open");
	}

	PMEMoid oid = pmemobj_root(pop, sizeof(struct root));
	struct root *r = pmemobj_direct(oid);
	UT_ASSERTne(r, NULL);

	struct worker_args args[MAX_THREADS];

	for (unsigned i = 0; i < Threads; ++i) {
		args[i].pop = pop;
		args[i].r = r;
		args[i].idx = i;
		for (unsigned j = 0; j < Ops_per_thread; ++j) {
			struct action *a = &r->actions[i][j];
			util_mutex_init(&a->lock);
			util_cond_init(&a->cond);
		}
	}

	run_worker(action_cancel_worker, args);
	sleep(5);
	actions_dump(r);
	actions_clear(pop, r);

	pmemobj_close(pop);

	DONE(NULL);
}

#ifdef _MSC_VER
/*
 * Since libpmemobj is linked statically, we need to invoke its ctor/dtor.
 */
MSVC_CONSTR(libpmemobj_init)
MSVC_DESTR(libpmemobj_fini)
#endif
