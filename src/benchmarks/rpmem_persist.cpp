/*
 * Copyright 2016-2018, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *      * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *      * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *      * Neither the name of the copyright holder nor the names of its
 *        contributors may be used to endorse or promote products derived
 *        from this software without specific prior written permission.
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
 * rpmem_persist.cpp -- rpmem persist benchmarks definition
 */

#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <unistd.h>

#include "benchmark.hpp"
#include "libpmem.h"
#include "librpmem.h"
#include "os.h"
#include "set.h"
#include "util.h"

#define CL_ALIGNMENT 64
#define MAX_OFFSET (CL_ALIGNMENT - 1)

#define ALIGN_CL(x) (((x) + CL_ALIGNMENT - 1) & ~(CL_ALIGNMENT - 1))

/*
 * rpmem_args -- benchmark specific command line options
 */
struct rpmem_args {
	char *mode;		/* operation mode: stat, seq, rand */
	bool no_warmup;		/* do not do warmup */
	bool no_memset;		/* do not call memset before each persist */
	bool no_replication;	/* do not call rpmem_persist */
	size_t chunk_size;	/* elementary chunk size */
	size_t dest_off;	/* destination address offset */
	unsigned max_memset_th;	/* maximum number of threads doing memset */
	char *master_source;	/* master source: from-file, from-memory */
};

/*
 * replica_source -- replica source
 */
enum replica_source {
	REPLICA_SOURCE_UNKNOWN,
	REPLICA_FROM_FILE,	/* as described in poolset file */
	REPLICA_FROM_MEMORY	/* allocated aligned memory */
};

/*
 * local_replica -- replica description
 */
struct local_replica {
	enum replica_source source;
	void *addrp;		/* memory file address */
	void *pool;		/* memory pool address */
	size_t mapped_len;	/* mapped len */
};

/*
 * rpmem_bench -- benchmark context
 */
struct rpmem_bench {
	struct rpmem_args *pargs; /* benchmark specific arguments */
	size_t *offsets;	  /* random/sequential address offsets */
	size_t n_offsets;	 /* number of random elements */
	int const_b;		  /* memset() value */
	size_t min_size;	  /* minimum file size */
	struct local_replica master; /* master replica */
	size_t pool_size;	 /* size of memory pool */
	RPMEMpool **rpp;	  /* rpmem pool pointers */
	unsigned *nlanes;	 /* number of lanes for each remote replica */
	unsigned remote_num;       /* number of remote replicas */
	size_t csize_align;       /* aligned elementary chunk size */
	sem_t memset_sem;	/* limit number of threads doing memset*/
	unsigned local_num;	/* number of local replicas */
	struct local_replica *local; /* local replicas */
};

/*
 * operation_mode -- mode of operation
 */
enum operation_mode {
	OP_MODE_UNKNOWN,
	OP_MODE_STAT,      /* always use the same chunk */
	OP_MODE_SEQ,       /* use consecutive chunks */
	OP_MODE_RAND,      /* use random chunks */
	OP_MODE_SEQ_WRAP,  /* use consequtive chunks, but use file size */
	OP_MODE_RAND_WRAP, /* use random chunks, but use file size */
	/* use random interlaced chunks, but use file size */
	OP_MODE_RAND_INTERLACED_WRAP,
};

/*
 * parse_op_mode -- parse operation mode from string
 */
static enum operation_mode
parse_op_mode(const char *arg)
{
	if (strcmp(arg, "stat") == 0)
		return OP_MODE_STAT;
	else if (strcmp(arg, "seq") == 0)
		return OP_MODE_SEQ;
	else if (strcmp(arg, "rand") == 0)
		return OP_MODE_RAND;
	else if (strcmp(arg, "seq-wrap") == 0)
		return OP_MODE_SEQ_WRAP;
	else if (strcmp(arg, "rand-wrap") == 0)
		return OP_MODE_RAND_WRAP;
	else if (strcmp(arg, "rand-int-wrap") == 0)
		return OP_MODE_RAND_INTERLACED_WRAP;
	else
		return OP_MODE_UNKNOWN;
}

/*
 * parse_replica_source -- parse replica source
 */
static enum replica_source
parse_replica_source(const char *arg)
{
	if (strcmp(arg, "from-file") == 0)
		return REPLICA_FROM_FILE;
	else if (strcmp(arg, "from-memory") == 0)
		return REPLICA_FROM_MEMORY;
	else
		return REPLICA_SOURCE_UNKNOWN;
}

/*
 * init_offsets -- initialize offsets[] array depending on the selected mode
 */
static int
init_offsets(struct benchmark_args *args, struct rpmem_bench *mb,
	     enum operation_mode op_mode)
{
	size_t n_ops_by_size = (mb->pool_size) /
			(args->n_threads * mb->csize_align);

	mb->n_offsets = args->n_ops_per_thread * args->n_threads;
	mb->offsets = (size_t *)malloc(mb->n_offsets * sizeof(*mb->offsets));
	if (!mb->offsets) {
		perror("malloc");
		return -1;
	}

	unsigned seed = args->seed;

	for (size_t i = 0; i < args->n_threads; i++) {
		for (size_t j = 0; j < args->n_ops_per_thread; j++) {
			size_t off_idx = i * args->n_ops_per_thread + j;
			size_t chunk_idx;
			switch (op_mode) {
				case OP_MODE_STAT:
					chunk_idx = i;
					break;
				case OP_MODE_SEQ:
					chunk_idx =
						i * args->n_ops_per_thread + j;
					break;
				case OP_MODE_RAND:
					chunk_idx = i * args->n_ops_per_thread +
						os_rand_r(&seed) %
							args->n_ops_per_thread;
					break;
				case OP_MODE_SEQ_WRAP:
					chunk_idx = i * n_ops_by_size +
						j % n_ops_by_size;
					break;
				case OP_MODE_RAND_WRAP:
					chunk_idx = i * n_ops_by_size +
						os_rand_r(&seed) %
							n_ops_by_size;
					break;
				case OP_MODE_RAND_INTERLACED_WRAP:
					chunk_idx = i * n_ops_by_size +
						(os_rand_r(&seed) * 2 + j % 2) %
						n_ops_by_size;
					break;
				default:
					assert(0);
					return -1;
			}

			mb->offsets[off_idx] = POOL_HDR_SIZE +
				chunk_idx * mb->csize_align +
				mb->pargs->dest_off;
		}
	}

	return 0;
}

/*
 * do_warmup -- does the warmup by writing the whole pool area
 */
static int
do_warmup(struct rpmem_bench *mb)
{
	/* clear the entire pool */
	memset(mb->master.pool, 0, mb->pool_size);

	for (unsigned i = 0; i < mb->local_num; ++i) {
		memset(mb->local[i].pool, 0, mb->pool_size);
	}

	for (unsigned r = 0; r < mb->remote_num; ++r) {
		int ret = rpmem_persist(mb->rpp[r], POOL_HDR_SIZE, mb->pool_size, 0);
		if (ret)
			return ret;
	}

	/* if no memset for each operation, do one big memset */
	if (mb->pargs->no_memset) {
		memset(mb->master.pool, 0xFF, mb->pool_size);

		for (unsigned i = 0; i < mb->local_num; ++i) {
			memset(mb->local[i].pool, 0xFF, mb->pool_size);
		}
	}

	return 0;
}

/*
 * rpmem_memset_persist -- memset and persist master and local replicas
 */
static void
rpmem_memset_persist(struct rpmem_bench *mb, size_t offset, int c,
		size_t len)
{
	struct local_replica *replica;
	void *src, *dest;

	/* memset master replica */
	dest = (char *)mb->master.pool + offset;
	pmem_memset_persist(dest, c, len);

	/* memcpy to local replicas */
	src = dest;
	for (unsigned i = 0; i < mb->local_num; ++i) {
		replica = &mb->local[i];
		dest = (char *)replica->pool + offset;
		pmem_memcpy_persist(dest, src, len);
	}
}

/*
 * rpmem_op -- actual benchmark operation
 */
static int
rpmem_op(struct benchmark *bench, struct operation_info *info)
{
	struct rpmem_bench *mb =
		(struct rpmem_bench *)pmembench_get_priv(bench);

	assert(info->index < mb->n_offsets);

	uint64_t idx = info->worker->index * info->args->n_ops_per_thread +
		info->index;
	size_t offset = mb->offsets[idx];
	size_t len = mb->pargs->chunk_size;

	if (!mb->pargs->no_memset) {
		/* thread id on MS 4 bits and operation id on LS 4 bits */
		int c = ((info->worker->index & 0xf) << 4) +
			((0xf & info->index));

		if (mb->pargs->max_memset_th == 0) {
			rpmem_memset_persist(mb, offset, c, len);
		} else {
			int ret;
			do
			{
				ret = sem_trywait(&mb->memset_sem);
			} while(ret == -1 && errno == EAGAIN);

			if (!ret) {
				rpmem_memset_persist(mb, offset, c, len);
				sem_post(&mb->memset_sem);
			} else {
				fprintf(stderr, "rpmem_persist sem_trywait: "
					"%s\n", strerror(errno));
			}
		}
	}

	if (!mb->pargs->no_replication) {
		int ret = 0;
		for (unsigned r = 0; r < mb->remote_num; ++r) {
			assert(info->worker->index < mb->nlanes[r]);

			ret = rpmem_persist(mb->rpp[r], offset, len,
					    info->worker->index);
			if (ret) {
				fprintf(stderr, "rpmem_persist replica #%u: "
					"%s\n", r, rpmem_errormsg());
				return ret;
			}
		}
	}

	return 0;
}

/*
 * rpmem_map_file -- map local file
 */
static int
rpmem_map_file(const char *path, struct local_replica *file, size_t size)
{
	int mode;
#ifndef _WIN32
	mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
#else
	mode = S_IWRITE | S_IREAD;
#endif

	file->addrp = pmem_map_file(path, size, PMEM_FILE_CREATE, mode,
			&file->mapped_len, NULL);

	if (!file->addrp)
		return -1;

	return 0;
}

/*
 * rpmem_unmap_file -- unmap local file
 */
static int
rpmem_unmap_file(struct local_replica *file)
{
	return pmem_unmap(file->addrp, file->mapped_len);
}

/*
 * rpmem_replica_init -- initialize replica
 */
static int
rpmem_replica_init(struct pool_replica *rep, enum replica_source source,
		struct local_replica *replica)
{
	assert(rep->nparts == 1);

	struct pool_set_part *part;
	long long sc;
	unsigned long alignment;

	replica->source = REPLICA_SOURCE_UNKNOWN;

	switch(source) {
	case REPLICA_FROM_FILE:
		part = (struct pool_set_part *)&rep->part[0];
		if (rpmem_map_file(part->path, replica, rep->repsize)) {
			perror(part->path);
			return -1;
		}
		replica->pool = (void *)((uintptr_t)replica->addrp +
				POOL_HDR_SIZE);
		break;
	case REPLICA_FROM_MEMORY:
		/* obtain memory alignment */
		sc = sysconf(_SC_PAGESIZE);
		if (sc < 0) {
			fprintf(stderr, "Cannot obtain sysconf(_SC_PAGESIZE): "
					"%s\n", strerror(errno));
			return -1;
		}
		alignment = (unsigned long)sc;
		/* allocate aligned memory */
		errno = posix_memalign(&replica->addrp, alignment, rep->repsize);
		if (errno != 0) {
			fprintf(stderr, "Cannot posix_memalign: %s\n",
					strerror(errno));
			return -1;
		}
		replica->pool = (void *)((uintptr_t)replica->addrp +
				POOL_HDR_SIZE);
		break;
	case REPLICA_SOURCE_UNKNOWN:
	default:
		assert(0);
	}

	replica->source = source;
	return 0;
}

/*
 * rpmem_replica_fini -- finalize replica
 */
static int
rpmem_replica_fini(struct local_replica *replica)
{
	int ret = 0;

	switch(replica->source) {
	case REPLICA_FROM_FILE:
		ret = rpmem_unmap_file(replica);
		break;
	case REPLICA_FROM_MEMORY:
		free(replica->addrp);
	case REPLICA_SOURCE_UNKNOWN:
	default:
		;
	}

	replica->source = REPLICA_SOURCE_UNKNOWN;
	return ret;
}

/*
 * rpmem_poolset_init -- read poolset file and initialize benchmark accordingly
 */
static int
rpmem_poolset_init(const char *path, struct rpmem_bench *mb,
		   struct benchmark_args *args)
{
	struct pool_set *set;
	struct pool_replica *rep;
	struct remote_replica *remote;

	struct rpmem_pool_attr attr;
	memset(&attr, 0, sizeof(attr));
	memcpy(attr.signature, "PMEMBNCH", sizeof(attr.signature));

	/* read and validate poolset */
	if (util_poolset_read(&set, path)) {
		fprintf(stderr, "Invalid poolset file '%s'\n", path);
		return -1;
	}

	assert(set);
	if (set->nreplicas < 2) {
		fprintf(stderr, "No replicas defined\n");
		goto err_poolset_free;
	}

	if (set->remote == 0) {
		fprintf(stderr, "No remote replicas defined\n");
		goto err_poolset_free;
	}

	if (set->poolsize < mb->min_size) {
		fprintf(stderr, "Poolset effective size is too small "
				"(%zu < %zu)\n", set->poolsize, mb->min_size);
		goto err_poolset_free;
	}

	mb->local_num = 0;
	mb->remote_num = 0;
	for (unsigned i = 1; i < set->nreplicas; ++i) {
		rep = set->replica[i];
		assert(rep);

		if (!rep->remote) {
			if (rep->nparts != 1) {
				fprintf(stderr, "replica %u: Multipart "
					"replicas are not supported\n", i);
				goto err_poolset_free;
			}
			++mb->local_num;
		} else {
			++mb->remote_num;
		}
	}

	mb->pool_size = set->poolsize - POOL_HDR_SIZE;

	/* read and validate master replica */
	rep = set->replica[0];

	assert(rep);
	assert(rep->remote == NULL);
	if (rep->nparts != 1) {
		fprintf(stderr, "Multipart master replicas are not supported\n");
		goto err_poolset_free;
	}

	if (rpmem_replica_init(rep, mb->master.source, &mb->master)) {
		goto err_poolset_free;
	}

	/* prepare local replicas */
	if (mb->local_num > 0) {
		mb->local = (struct local_replica *)calloc(mb->local_num,
				sizeof(struct local_replica));
		for (unsigned i = 1, idx = 0; i < set->nreplicas; ++i) {
			rep = set->replica[i];
			if (rep->remote != 0) {
				continue;
			}

			if (rpmem_replica_init(rep, REPLICA_FROM_FILE,
					&mb->local[idx])) {
				goto err_free_local;
			}

			++idx;
		}
	}

	/* prepare remote replicas */
	mb->nlanes = (unsigned *)malloc(mb->remote_num * sizeof(unsigned));
	if (mb->nlanes == NULL) {
		perror("malloc");
		goto err_free_local;
	}

	mb->rpp = (RPMEMpool **)malloc(mb->remote_num * sizeof(RPMEMpool *));
	if (mb->rpp == NULL) {
		perror("malloc");
		goto err_free_lanes;
	}

	unsigned r, idx;
	for (r = 1, idx = 0; r < set->nreplicas; ++r) {
		remote = set->replica[r]->remote;
		if (remote == 0) {
			continue;
		}

		mb->nlanes[idx] = args->n_threads;
		/* Temporary WA for librpmem issue */
		++mb->nlanes[idx];

		mb->rpp[idx] = rpmem_create(remote->node_addr, remote->pool_desc,
					mb->master.addrp, set->poolsize, &mb->nlanes[idx], &attr);
		if (!mb->rpp[idx]) {
			perror("rpmem_create");
			goto err_rpmem_close;
		}

		if (mb->nlanes[idx] < args->n_threads) {
			fprintf(stderr, "Number of threads too large for "
					"replica #%u (max: %u)\n",
				r, mb->nlanes[idx]);
			r++; /* close current replica */
			goto err_rpmem_close;
		}

		++idx;
	}

	util_poolset_free(set);
	return 0;

err_rpmem_close:
	for (unsigned i = 0; i < idx; i++)
		rpmem_close(mb->rpp[i]);
	free(mb->rpp);

err_free_lanes:
	free(mb->nlanes);

err_free_local:
	for (unsigned i = 0; i < mb->local_num; ++i) {
		rpmem_replica_fini(&mb->local[i]);
	}

	rpmem_replica_fini(&mb->master);

err_poolset_free:
	util_poolset_free(set);
	return -1;
}

/*
 * rpmem_poolset_fini -- close opened local and remote replicas
 */
static void
rpmem_poolset_fini(struct rpmem_bench *mb)
{
	for (unsigned r = 0; r < mb->remote_num; ++r) {
		rpmem_close(mb->rpp[r]);
	}

	rpmem_replica_fini(&mb->master);
}

/*
 * rpmem_set_min_size -- compute minimal file size based on benchmark arguments
 */
static void
rpmem_set_min_size(struct rpmem_bench *mb, enum operation_mode op_mode,
		   struct benchmark_args *args)
{
	mb->csize_align = ALIGN_CL(mb->pargs->chunk_size);

	switch (op_mode) {
		case OP_MODE_STAT:
			mb->min_size = mb->csize_align * args->n_threads;
			break;
		case OP_MODE_SEQ:
		case OP_MODE_RAND:
			mb->min_size = mb->csize_align *
				args->n_ops_per_thread * args->n_threads;
			break;
		case OP_MODE_SEQ_WRAP:
		case OP_MODE_RAND_WRAP:
		case OP_MODE_RAND_INTERLACED_WRAP:
			/*
			 * at least one chunk per thread to avoid false sharing
			 */
			mb->min_size = mb->csize_align * args->n_threads;
			break;
		default:
			assert(0);
	}

	mb->min_size += POOL_HDR_SIZE;
}

/*
 * rpmem_init -- initialization function
 */
static int
rpmem_init(struct benchmark *bench, struct benchmark_args *args)
{
	assert(bench != NULL);
	assert(args != NULL);
	assert(args->opts != NULL);

	struct rpmem_bench *mb =
		(struct rpmem_bench *)malloc(sizeof(struct rpmem_bench));
	if (!mb) {
		perror("malloc");
		return -1;
	}

	mb->pargs = (struct rpmem_args *)args->opts;
	mb->pargs->chunk_size = args->dsize;

	enum operation_mode op_mode = parse_op_mode(mb->pargs->mode);
	if (op_mode == OP_MODE_UNKNOWN) {
		fprintf(stderr, "Invalid operation mode argument '%s'\n",
			mb->pargs->mode);
		goto err_parse_args;
	}

	mb->master.source =
		parse_replica_source(mb->pargs->master_source);
	if (mb->master.source == REPLICA_SOURCE_UNKNOWN) {
		fprintf(stderr, "Invalid master replica source argument '%s'\n",
			mb->pargs->mode);
		goto err_parse_args;
	}

	rpmem_set_min_size(mb, op_mode, args);

	if (rpmem_poolset_init(args->fname, mb, args)) {
		goto err_poolset_init;
	}

	/* initialize offsets[] array depending on benchmark args */
	if (init_offsets(args, mb, op_mode) < 0) {
		goto err_init_offsets;
	}

	if (!mb->pargs->no_warmup) {
		if (do_warmup(mb) != 0) {
			fprintf(stderr, "do_warmup() function failed.\n");
			goto err_warmup;
		}
	}

	if (mb->pargs->max_memset_th > 0) {
		sem_init(&mb->memset_sem, 0, mb->pargs->max_memset_th);
	}

	pmembench_set_priv(bench, mb);

	return 0;
err_warmup:
	free(mb->offsets);
err_init_offsets:
	rpmem_poolset_fini(mb);
err_poolset_init:
err_parse_args:
	free(mb);
	return -1;
}

/*
 * rpmem_exit -- benchmark cleanup function
 */
static int
rpmem_exit(struct benchmark *bench, struct benchmark_args *args)
{
	struct rpmem_bench *mb =
		(struct rpmem_bench *)pmembench_get_priv(bench);
	rpmem_poolset_fini(mb);

	if (mb->pargs->max_memset_th > 0) {
		sem_destroy(&mb->memset_sem);
	}

	free(mb->offsets);
	free(mb);
	return 0;
}

static struct benchmark_clo rpmem_clo[7];
/* Stores information about benchmark. */
static struct benchmark_info rpmem_info;
CONSTRUCTOR(rpmem_persist_costructor)
void
pmem_rpmem_persist(void)
{

	rpmem_clo[0].opt_short = 'M';
	rpmem_clo[0].opt_long = "mem-mode";
	rpmem_clo[0].descr = "Memory writing mode :"
			     " stat, seq[-wrap], rand[-wrap]";
	rpmem_clo[0].def = "seq";
	rpmem_clo[0].off = clo_field_offset(struct rpmem_args, mode);
	rpmem_clo[0].type = CLO_TYPE_STR;

	rpmem_clo[1].opt_short = 'D';
	rpmem_clo[1].opt_long = "dest-offset";
	rpmem_clo[1].descr = "Destination cache line "
			     "alignment offset";
	rpmem_clo[1].def = "0";
	rpmem_clo[1].off = clo_field_offset(struct rpmem_args, dest_off);
	rpmem_clo[1].type = CLO_TYPE_UINT;
	rpmem_clo[1].type_uint.size =
		clo_field_size(struct rpmem_args, dest_off);
	rpmem_clo[1].type_uint.base = CLO_INT_BASE_DEC;
	rpmem_clo[1].type_uint.min = 0;
	rpmem_clo[1].type_uint.max = MAX_OFFSET;

	rpmem_clo[2].opt_short = 'w';
	rpmem_clo[2].opt_long = "no-warmup";
	rpmem_clo[2].descr = "Don't do warmup";
	rpmem_clo[2].def = "false";
	rpmem_clo[2].type = CLO_TYPE_FLAG;
	rpmem_clo[2].off = clo_field_offset(struct rpmem_args, no_warmup);

	rpmem_clo[3].opt_short = 'T';
	rpmem_clo[3].opt_long = "no-memset";
	rpmem_clo[3].descr = "Don't call memset for all rpmem_persist";
	rpmem_clo[3].def = "false";
	rpmem_clo[3].off = clo_field_offset(struct rpmem_args, no_memset);
	rpmem_clo[3].type = CLO_TYPE_FLAG;

	rpmem_clo[4].opt_short = 'R';
	rpmem_clo[4].opt_long = "no-replication";
	rpmem_clo[4].descr = "Don't call rpmem_persist";
	rpmem_clo[4].def = "false";
	rpmem_clo[4].off = clo_field_offset(struct rpmem_args, no_replication);
	rpmem_clo[4].type = CLO_TYPE_FLAG;

	rpmem_clo[5].opt_short = 0;
	rpmem_clo[5].opt_long = "max-memset-threads";
	rpmem_clo[5].descr = "Maximum number of threads doing memset";
	rpmem_clo[5].def = "0";
	rpmem_clo[5].off = clo_field_offset(struct rpmem_args, max_memset_th);
	rpmem_clo[5].type = CLO_TYPE_UINT;
	rpmem_clo[5].type_uint.size =
		clo_field_size(struct rpmem_args, max_memset_th);
	rpmem_clo[5].type_uint.base = CLO_INT_BASE_DEC;
	rpmem_clo[5].type_uint.min = 0;
	rpmem_clo[5].type_uint.max = UINT_MAX;

	rpmem_clo[6].opt_short = 0;
	rpmem_clo[6].opt_long = "master-replica-source";
	rpmem_clo[6].descr = "Master replica: from-file, from-memory";
	rpmem_clo[6].def = "from-file";
	rpmem_clo[6].off = clo_field_offset(struct rpmem_args, master_source);
	rpmem_clo[6].type = CLO_TYPE_STR;

	rpmem_info.name = "rpmem_persist";
	rpmem_info.brief = "Benchmark for rpmem_persist() "
			   "operation";
	rpmem_info.init = rpmem_init;
	rpmem_info.exit = rpmem_exit;
	rpmem_info.multithread = true;
	rpmem_info.multiops = true;
	rpmem_info.operation = rpmem_op;
	rpmem_info.measure_time = true;
	rpmem_info.clos = rpmem_clo;
	rpmem_info.nclos = ARRAY_SIZE(rpmem_clo);
	rpmem_info.opts_size = sizeof(struct rpmem_args);
	rpmem_info.rm_file = true;
	rpmem_info.allow_poolset = true;
	REGISTER_BENCHMARK(rpmem_info);
};
