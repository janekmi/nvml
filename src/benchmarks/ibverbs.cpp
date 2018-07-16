/*
 * Copyright 2018, Intel Corporation
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
 * ibverbs.cpp -- ibverbs benchmarks definition
 */

#include <cassert>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <infiniband/verbs.h>

#include <libpmem.h>

#include "benchmark.hpp"

#define calc_idx(th, nops, op)\
	(th) * (nops) + (op)

#define NULL_STR "null"

#define MMAP_PROT (PROT_READ|PROT_WRITE)
#define MMAP_FLAGS (MAP_PRIVATE|MAP_ANONYMOUS)

#define VERBS_ACCESS (IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE)

enum memory_source {
	from_malloc,
	from_file,
	memory_source_max
};

const char *memory_source_str[] = {
	"malloc",
	"file"
};

/*
 * parse_memory_source --
 */
static enum memory_source
parse_memory_source(const char *str) {
	for (int i = 0; i < memory_source_max; ++i) {
		if (!strcmp(str, memory_source_str[i])) {
			return (enum memory_source)i;
		}
	}
	return memory_source_max;
}

/*
 * ibverbs_args -- benchmark specific command line options
 */
struct ibverbs_args {
	bool no_warmup;    /* do not do warmup */
	bool hugepages;	   /* use hugepages */
	size_t page_size;  /* page size */
	char *device;	   /* IB device name */
	char *mr_src;	   /* memory source */
};

/*
 * ibverbs_bench -- benchmark context
 */
struct ibverbs_bench {
	struct ibverbs_args *pargs;  /* benchmark specific arguments */
	/* ibverbs assets */
	struct ibv_context *context; /* ibverbs context */
	struct ibv_pd *pd;	     /* protection domain */
	struct ibv_mr **mrs;	     /* memory regions */
	/* parameters */
	int alignment;
	size_t size;
	enum memory_source mr_src;
	/* memory assets */
	void *addr;		     /* base addr */
	size_t fsize;
};

/*
 * ibverbs_op -- actual benchmark operation
 */
static int
ibverbs_op(struct benchmark *bench, struct operation_info *info)
{
	auto *mb = (struct ibverbs_bench *)pmembench_get_priv(bench);
	uint64_t idx = calc_idx(info->worker->index, info->args->n_ops_per_thread, info->index);
	void *addr = (char *)mb->addr + idx * info->args->dsize;
	mb->mrs[idx] = ibv_reg_mr(mb->pd, addr, info->args->dsize, VERBS_ACCESS);
	if (!mb->mrs[idx]) {
		perror("ibv_reg_mr");
	}
	return mb->mrs[idx] == NULL ? -1 : 0;
}

/*
 * ibverbs_find_device -- XXX
 */
static int
ibverbs_find_device(struct ibv_device **devices, int num_devices, const char *name)
{
	if (devices == NULL) {
		fprintf(stderr, "IB device list is empty");
		return -1;
	}
	if (!strcmp(name, NULL_STR)) {
		/* pick the first one */
		return 0;
	}
	for(int i = 0; i < num_devices; ++i) {
		if (!strcmp(devices[i]->name, name)) {
			return i;
		}
	}
	fprintf(stderr, "cannot find IB device: %s", name);
	return -1;
}

/*
 * ibverbs_open -- XXX
 */
static int
ibverbs_open(struct ibverbs_bench *mb)
{
	int num_devices;

	struct ibv_device **devices = ibv_get_device_list(&num_devices);
	const int device_idx = ibverbs_find_device(devices, num_devices, mb->pargs->device);
	if (device_idx == -1) {
		goto err_device;
	}
	mb->context = ibv_open_device(devices[device_idx]);
	if (mb->context == NULL) {
		perror("ibv_open_device");
		goto err_device;
	}
	mb->pd = ibv_alloc_pd(mb->context);
	if (mb->pd == NULL) {
		perror("ibv_alloc_pd");
		goto err_domain;
	}
	ibv_free_device_list(devices);
	return 0;

err_domain:
	ibv_close_device(mb->context);
err_device:
	ibv_free_device_list(devices);
	return -1;
}

static void
ibverbs_dereg_mrs(struct ibverbs_bench *mb, struct benchmark_args *args)
{
	for(unsigned th = 0; th < args->n_threads; ++th) {
		for (size_t op = 0; op < args->n_ops_per_thread; ++op) {
			uint64_t idx = calc_idx(th, args->n_ops_per_thread, op);
			if (!mb->mrs[idx]) {
				continue;
			}
			errno = ibv_dereg_mr(mb->mrs[idx]);
			if (errno) {
				perror("ibv_dereg_mr");
			}
		}
	}
}

/*
 * ibverbs_close -- XXX
 */
static void
ibverbs_close(struct ibverbs_bench *mb, struct benchmark_args *args)
{
	ibv_dealloc_pd(mb->pd);
	ibv_close_device(mb->context);
}

/*
 * memory_malloc -- XXX
 */
static int
memory_malloc(struct ibverbs_bench *mb)
{
	int flags = MMAP_FLAGS;
	if (mb->pargs->hugepages) {
		flags |= MAP_HUGETLB;
	}
	/* errno = posix_memalign(&mb->addr, mb->alignment, mb->size); */
	mb->addr = mmap(NULL, mb->size, MMAP_PROT, flags, 0, 0);
	if (mb->addr == MAP_FAILED) {
		perror("mmap");
		return -1;
	}
	return 0;
}

/*
 * memory_from_file -- XXX
 */
static int
memory_from_file(struct ibverbs_bench *mb, const char *fname) {
	int is_pmem;
	mb->addr = pmem_map_file(fname, 0, 0, 0, &mb->fsize, &is_pmem);
	if (!mb->addr) {
		perror("pmem_map_file");
		return -1;
	}
	if (mb->fsize < mb->size) {
		fprintf(stderr, "file is too small (%zu < %zu): %s",
			mb->fsize, mb->size, fname);
		pmem_unmap(mb->addr, mb->fsize);
		return -1;
	}
	return 0;
}

/*
 * prepare_memory -- XXX
 */
static int
prepare_memory(struct ibverbs_bench *mb, struct benchmark_args *args) {
	mb->mr_src = parse_memory_source(mb->pargs->mr_src);
	switch (mb->mr_src) {
	case from_malloc:
		if (memory_malloc(mb)) {
			return -1;
		}
		break;
	case from_file:
		if (memory_from_file(mb, args->fname)) {
			return -1;
		}
		break;
	default:
		assert(0);
	}
	return 0;
}

/*
 * cleanup_memory -- XXX
 */
static void
cleanup_memory(struct ibverbs_bench *mb, struct benchmark_args *args) {
	switch(mb->mr_src) {
	case from_malloc:
		munmap(mb->addr, mb->size);
		break;
	case from_file:
		pmem_unmap(mb->addr, mb->fsize);
		break;
	default:
		;
	}
}

/*
 * prepare_assets -- XXX
 */
static int
prepare_assets(struct ibverbs_bench *mb, struct benchmark_args *args) {
	const uint64_t n_ops = args->n_threads * args->n_ops_per_thread;
	mb->size = n_ops * args->dsize;
	mb->alignment = roundup(args->dsize, mb->pargs->page_size);
	
	// prepare memory regions array
	mb->mrs = (struct ibv_mr **)calloc(n_ops, sizeof(struct ibv_mr *));
	if (mb->mrs == NULL) {
		perror("calloc");
		return -1;
	}
	// allocate memory region from specified source
	if (prepare_memory(mb, args)) {
		goto err_mr;
	}
	return 0;
err_mr:
	free(mb->mrs);
	return -1;
}

/*
 * do_warmup -- XXX
 */
static void
do_warmup(struct ibverbs_bench *mb, unsigned int seed) {
	char *buff = (char *)mb->addr;
	srand(seed);
	for (size_t off = 0; off < mb->size; off += mb->pargs->page_size) {
		buff[off] = rand() % CHAR_MAX;
	}
}

/*
 * ibverbs_init -- initialization function
 */
static int
ibverbs_init(struct benchmark *bench, struct benchmark_args *args)
{
	auto *mb = (struct ibverbs_bench *)malloc(sizeof(struct ibverbs_bench));
	if (!mb) {
		perror("malloc");
		return -1;
	}

	mb->pargs = (struct ibverbs_args *)args->opts;

	if (ibverbs_open(mb)) {
		goto err_open;
	}

	if (prepare_assets(mb, args)) {
		goto err_assets;
	}

	if (!mb->pargs->no_warmup) {
		do_warmup(mb, args->seed);
	}

	pmembench_set_priv(bench, mb);

	return 0;
err_assets:
	ibverbs_close(mb, args);
err_open:
	free(mb);
	return -1;
}

/*
 * ibverbs_exit -- benchmark cleanup function
 */
static int
ibverbs_exit(struct benchmark *bench, struct benchmark_args *args)
{
	auto *mb = (struct ibverbs_bench *)pmembench_get_priv(bench);
	
	ibverbs_dereg_mrs(mb, args);
	free(mb->mrs);
	cleanup_memory(mb, args);
	ibverbs_close(mb, args);
	free(mb);
	return 0;
}

static struct benchmark_clo ibverbs_clo[5];
/* Stores information about benchmark. */
static struct benchmark_info rpmem_info;
CONSTRUCTOR(rpmem_persist_constructor)
void
pmem_ibverbs(void)
{
	ibverbs_clo[0].opt_short = 'w';
	ibverbs_clo[0].opt_long = "no-warmup";
	ibverbs_clo[0].descr = "Don't do warmup";
	ibverbs_clo[0].def = "false";
	ibverbs_clo[0].type = CLO_TYPE_FLAG;
	ibverbs_clo[0].off = clo_field_offset(struct ibverbs_args, no_warmup);

	ibverbs_clo[1].opt_short = 'm';
	ibverbs_clo[1].opt_long = "device-name";
	ibverbs_clo[1].descr = "IB device name";
	ibverbs_clo[1].def = "null";
	ibverbs_clo[1].off = clo_field_offset(struct ibverbs_args, device);
	ibverbs_clo[1].type = CLO_TYPE_STR;

	ibverbs_clo[2].opt_short = 'd';
	ibverbs_clo[2].opt_long = "memory-source";
	ibverbs_clo[2].descr = "Source of memory regions (malloc, file)";
	ibverbs_clo[2].def = "malloc";
	ibverbs_clo[2].off = clo_field_offset(struct ibverbs_args, mr_src);
	ibverbs_clo[2].type = CLO_TYPE_STR;

	ibverbs_clo[3].opt_short = 'p';
	ibverbs_clo[3].opt_long = "page_size";
	ibverbs_clo[3].descr = "Page size";
	ibverbs_clo[3].def = "2097152"; /* 2 MB*/
	ibverbs_clo[3].off = clo_field_offset(struct ibverbs_args, page_size);
	ibverbs_clo[3].type = CLO_TYPE_UINT;
	ibverbs_clo[3].type_uint.size =
		clo_field_size(struct ibverbs_args, page_size);
	ibverbs_clo[3].type_uint.base = CLO_INT_BASE_DEC;
	ibverbs_clo[3].type_uint.min = 0;
	ibverbs_clo[3].type_uint.max = UINT_MAX;

	ibverbs_clo[4].opt_short = 'h';
	ibverbs_clo[4].opt_long = "hugepages";
	ibverbs_clo[4].descr = "Use hugepages";
	ibverbs_clo[4].def = "true";
	ibverbs_clo[4].type = CLO_TYPE_FLAG;
	ibverbs_clo[4].off = clo_field_offset(struct ibverbs_args, hugepages);

	rpmem_info.name = "ibverbs";
	rpmem_info.brief = "Benchmark for ibverbs operations";
	rpmem_info.init = ibverbs_init;
	rpmem_info.exit = ibverbs_exit;
	rpmem_info.multithread = true;
	rpmem_info.multiops = true;
	rpmem_info.operation = ibverbs_op;
	rpmem_info.measure_time = true;
	rpmem_info.clos = ibverbs_clo;
	rpmem_info.nclos = ARRAY_SIZE(ibverbs_clo);
	rpmem_info.opts_size = sizeof(struct ibverbs_args);
	rpmem_info.rm_file = true;
	rpmem_info.allow_poolset = true;
	rpmem_info.print_bandwidth = true;
	REGISTER_BENCHMARK(rpmem_info);
};
