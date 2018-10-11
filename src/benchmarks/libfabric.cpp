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
 * libfabric.cpp -- libfabric benchmarks definition
 */

#include <cassert>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/param.h>

#include <rdma/fi_domain.h>

#include <libpmem.h>

#include "benchmark.hpp"

#define calc_idx(th, nops, op)\
	(th) * (nops) + (op)

#define MMAP_PROT (PROT_READ|PROT_WRITE)
#define MMAP_FLAGS (MAP_PRIVATE|MAP_ANONYMOUS)

#define PMEMBENCH_FIVERSION FI_VERSION(1, 4)

enum memory_source {
	from_malloc,
	from_file,
	memory_source_max
};

static const char *memory_source_str[] = {
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
 * libfabric_args -- benchmark specific command line options
 */
struct libfabric_args {
	bool no_warmup;		/* do not do warmup */
	bool hugepages;		/* use hugepages */
	size_t page_size;	/* page size */
	char *node;		/* node address */
	char *provider;		/* provider name */
	char *mr_src;		/* memory source */
};

/*
 * libfabric_bench -- benchmark context
 */
struct libfabric_bench {
	struct libfabric_args *pargs;	/* benchmark specific arguments */
	/* libfabric assets */
	struct fi_info *fi;		/* fabric interface information */
	struct fid_fabric *fabric;	/* fabric domain */
	struct fid_domain *domain;	/* fabric protection domain */
	struct fid_mr **mrs;		/* memory regions */

	/* parameters */
	int alignment;
	size_t size;
	enum memory_source mr_src;
	/* memory assets */
	void *addr;			/* base addr */
	size_t fsize;
};

/*
 * libfabric_op -- actual benchmark operation
 */
static int
libfabric_op(struct benchmark *bench, struct operation_info *info)
{
	auto *mb = (struct libfabric_bench *)pmembench_get_priv(bench);
	uint64_t idx = calc_idx(info->worker->index, info->args->n_ops_per_thread, info->index);
	void *addr = (char *)mb->addr + idx * info->args->dsize;

	int ret = fi_mr_reg(mb->domain, addr, info->args->dsize,
			FI_REMOTE_WRITE|FI_REMOTE_READ, 0, 0, 0,
			&mb->mrs[idx], NULL);
	if (ret < 0) {
		perror("fi_mr_reg");
	}
	return ret;
}

/*
 * libfabric_hints -- XXX
 */
struct fi_info *
libfabric_hints(struct libfabric_bench *mb)
{
	struct fi_info *hints = fi_allocinfo();
	if (!hints) {
		perror("!fi_allocinfo");
		return NULL;
	}

	/* connection-oriented endpoint */
	hints->ep_attr->type = FI_EP_MSG;

	/*
	 * Basic memory registration mode indicates that MR attributes
	 * (rkey, lkey) are selected by provider.
	 */
	hints->domain_attr->mr_mode = FI_MR_BASIC;

	/*
	 * FI_THREAD_SAFE indicates MT applications can access any
	 * resources through interface without any restrictions
	 */
	hints->domain_attr->threading = FI_THREAD_SAFE;

	/*
	 * FI_MSG - SEND and RECV
	 * FI_RMA - WRITE and READ
	 */
	hints->caps = FI_MSG | FI_RMA;

	/* must register locally accessed buffers */
	hints->mode = FI_CONTEXT | FI_LOCAL_MR | FI_RX_CQ_DATA;

	/* READ-after-WRITE and SEND-after-WRITE message ordering required */
	hints->tx_attr->msg_order = FI_ORDER_RAW | FI_ORDER_SAW;

	hints->addr_format = FI_SOCKADDR;

	hints->fabric_attr->prov_name = strdup(mb->pargs->provider);
	if (!hints->fabric_attr->prov_name) {
		perror("strdup");
		return NULL;
	}

	return hints;
}

/*
 * libfabric_open -- XXX
 */
static int
libfabric_open(struct libfabric_bench *mb)
{
	struct fi_info *hints = libfabric_hints(mb);

	int ret = fi_getinfo(PMEMBENCH_FIVERSION, mb->pargs->node,
			NULL /* service */, FI_SOURCE, hints, &mb->fi);
	if (ret < 0) {
		perror("fi_getinfo");
		goto err_hints;
	}
	ret = fi_fabric(mb->fi->fabric_attr, &mb->fabric, NULL);
	if (ret < 0) {
		perror("fi_fabric");
		goto err_hints;
	}
	ret = fi_domain(mb->fabric, mb->fi, &mb->domain, NULL);
	if (ret < 0) {
		perror("fi_domain");
		goto err_hints;
	}

err_hints:
	fi_freeinfo(hints);
	return ret;
}

/*
 * libfabric_dereg_mrs -- XXX
 */
static void
libfabric_dereg_mrs(struct libfabric_bench *mb, struct benchmark_args *args)
{
	for(unsigned th = 0; th < args->n_threads; ++th) {
		for (size_t op = 0; op < args->n_ops_per_thread; ++op) {
			uint64_t idx = calc_idx(th, args->n_ops_per_thread, op);
			if (!mb->mrs[idx]) {
				continue;
			}
			int ret = fi_close(&mb->mrs[idx]->fid);
			if (ret) {
				perror("fi_close");
			}
		}
	}
}

/*
 * libfabric_close -- XXX
 */
static void
libfabric_close(struct libfabric_bench *mb, struct benchmark_args *args)
{
	/* XXX */
}

/*
 * memory_malloc -- XXX
 */
static int
memory_malloc(struct libfabric_bench *mb)
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
memory_from_file(struct libfabric_bench *mb, const char *fname) {
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
prepare_memory(struct libfabric_bench *mb, struct benchmark_args *args) {
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
cleanup_memory(struct libfabric_bench *mb, struct benchmark_args *args) {
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
prepare_assets(struct libfabric_bench *mb, struct benchmark_args *args) {
	const uint64_t n_ops = args->n_threads * args->n_ops_per_thread;
	mb->size = n_ops * args->dsize;
	mb->alignment = roundup(args->dsize, mb->pargs->page_size);
	
	// prepare memory regions array
	mb->mrs = (struct fid_mr **)calloc(n_ops, sizeof(struct fid_mr *));
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
do_warmup(struct libfabric_bench *mb, unsigned int seed) {
	char *buff = (char *)mb->addr;
	srand(seed);
	for (size_t off = 0; off < mb->size; off += mb->pargs->page_size) {
		buff[off] = rand() % CHAR_MAX;
	}
}

/*
 * libfabric_init -- initialization function
 */
static int
libfabric_init(struct benchmark *bench, struct benchmark_args *args)
{
	auto *mb = (struct libfabric_bench *)malloc(sizeof(struct libfabric_bench));
	if (!mb) {
		perror("malloc");
		return -1;
	}

	mb->pargs = (struct libfabric_args *)args->opts;

	if (libfabric_open(mb)) {
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
	libfabric_close(mb, args);
err_open:
	free(mb);
	return -1;
}

/*
 * libfabric_exit -- benchmark cleanup function
 */
static int
libfabric_exit(struct benchmark *bench, struct benchmark_args *args)
{
	auto *mb = (struct libfabric_bench *)pmembench_get_priv(bench);
	
	libfabric_dereg_mrs(mb, args);
	free(mb->mrs);
	cleanup_memory(mb, args);
	libfabric_close(mb, args);
	free(mb);
	return 0;
}

static struct benchmark_clo ibverbs_clo[6];
/* Stores information about benchmark. */
static struct benchmark_info rpmem_info;
CONSTRUCTOR(rpmem_persist_constructor)
void
pmem_libfabric(void)
{
	ibverbs_clo[0].opt_short = 'w';
	ibverbs_clo[0].opt_long = "no-warmup";
	ibverbs_clo[0].descr = "Don't do warmup";
	ibverbs_clo[0].def = "false";
	ibverbs_clo[0].type = CLO_TYPE_FLAG;
	ibverbs_clo[0].off = clo_field_offset(struct libfabric_args, no_warmup);

	ibverbs_clo[1].opt_short = 'm';
	ibverbs_clo[1].opt_long = "node";
	ibverbs_clo[1].descr = "node address";
	ibverbs_clo[1].def = "127.0.0.1";
	ibverbs_clo[1].off = clo_field_offset(struct libfabric_args, node);
	ibverbs_clo[1].type = CLO_TYPE_STR;

	ibverbs_clo[2].opt_short = 'm';
	ibverbs_clo[2].opt_long = "node";
	ibverbs_clo[2].descr = "provider name";
	ibverbs_clo[2].def = "verbs";
	ibverbs_clo[2].off = clo_field_offset(struct libfabric_args, provider);
	ibverbs_clo[2].type = CLO_TYPE_STR;

	ibverbs_clo[3].opt_short = 'd';
	ibverbs_clo[3].opt_long = "memory-source";
	ibverbs_clo[3].descr = "Source of memory regions (malloc, file)";
	ibverbs_clo[3].def = "malloc";
	ibverbs_clo[3].off = clo_field_offset(struct libfabric_args, mr_src);
	ibverbs_clo[3].type = CLO_TYPE_STR;

	ibverbs_clo[4].opt_short = 'p';
	ibverbs_clo[4].opt_long = "page_size";
	ibverbs_clo[4].descr = "Page size";
	ibverbs_clo[4].def = "2097152"; /* 2 MB*/
	ibverbs_clo[4].off = clo_field_offset(struct libfabric_args, page_size);
	ibverbs_clo[4].type = CLO_TYPE_UINT;
	ibverbs_clo[4].type_uint.size =
		clo_field_size(struct libfabric_args, page_size);
	ibverbs_clo[4].type_uint.base = CLO_INT_BASE_DEC;
	ibverbs_clo[4].type_uint.min = 0;
	ibverbs_clo[4].type_uint.max = UINT_MAX;

	ibverbs_clo[5].opt_short = 'h';
	ibverbs_clo[5].opt_long = "hugepages";
	ibverbs_clo[5].descr = "Use hugepages";
	ibverbs_clo[5].def = "true";
	ibverbs_clo[5].type = CLO_TYPE_FLAG;
	ibverbs_clo[5].off = clo_field_offset(struct libfabric_args, hugepages);

	rpmem_info.name = "libfabric";
	rpmem_info.brief = "Benchmark for libfabric operations";
	rpmem_info.init = libfabric_init;
	rpmem_info.exit = libfabric_exit;
	rpmem_info.multithread = true;
	rpmem_info.multiops = true;
	rpmem_info.operation = libfabric_op;
	rpmem_info.measure_time = true;
	rpmem_info.clos = ibverbs_clo;
	rpmem_info.nclos = ARRAY_SIZE(ibverbs_clo);
	rpmem_info.opts_size = sizeof(struct libfabric_args);
	rpmem_info.rm_file = true;
	rpmem_info.allow_poolset = true;
	rpmem_info.print_bandwidth = true;
	REGISTER_BENCHMARK(rpmem_info);
};
