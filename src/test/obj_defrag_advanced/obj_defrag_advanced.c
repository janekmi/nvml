/*
 * Copyright 2019, Intel Corporation
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
 * obj_defrag_advanced.c -- test for defragmentation feature
 */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "vgraph.h"
#include "pgraph.h"
#include "unittest.h"

enum op {
	OP_CREATE,
	OP_DUMP,
	OP_DEFRAG,
	OP_MAX
};

struct task {
	enum op op;
	const char *path;
	const char *dump;
	unsigned seed;

	struct vgraph_params vgraph_params;
	struct pgraph_params pgraph_params;
};

static void
graph_defrag(PMEMobjpool *pop, struct pgraph *pgraph)
{
	/* count number of oids */
	unsigned oidcnt = pgraph->nodes_num;
	for (unsigned i = 0; i < pgraph->nodes_num; ++i) {
		struct pnode *pnode = pmemobj_direct(pgraph->nodes[i]);
		oidcnt += pnode->edges_num;
	}

	/* create array of oid pointers */
	PMEMoid **oidv = malloc(sizeof(PMEMoid *) * oidcnt);
	unsigned oidi = 0;
	for (unsigned i = 0; i < pgraph->nodes_num; ++i) {
		oidv[oidi++] = &pgraph->nodes[i];

		struct pnode *pnode = pmemobj_direct(pgraph->nodes[i]);
		for (unsigned j = 0; j < pnode->edges_num; ++j) {
			oidv[oidi++] = &pnode->edges[j];
		}
	}

	UT_ASSERTeq(oidi, oidcnt);

	/* check if all oids are valid */
	for (unsigned i = 0; i < oidcnt; ++i) {
		void *ptr = pmemobj_direct(*oidv[i]);
		UT_ASSERTne(ptr, NULL);
	}

	/* check if all oids appear only once */
	for (unsigned i = 0; i < oidcnt - 1; ++i) {
		for (unsigned j = i + 1; j < oidcnt; ++j) {
			UT_ASSERTne(oidv[i], oidv[j]);
		}
	}

	struct pobj_defrag_result result;
	int ret = pmemobj_defrag(pop, oidv, oidcnt, &result);
	UT_ASSERTeq(ret, 0);
}

#define TEST_NAME "obj_defrag_advanced"

/*
 * print_usage -- XXX
 */
static void
print_usage()
{
	printf(TEST_NAME "\n");
}

#define GRAPH_LAYOUT POBJ_LAYOUT_NAME(graph)

/*
 * create_op -- XXX
 */
static void
create_op(struct task *task)
{
	if (task->path == NULL) {
		print_usage();
		exit(EXIT_FAILURE);
	}

	PMEMobjpool *pop = NULL;

	pop = pmemobj_create(task->path, GRAPH_LAYOUT, 0, S_IWUSR | S_IRUSR);
	if (pop == NULL) {
		UT_FATAL("!pmemobj_create: %s", task->path);
	}

	srand(task->seed);

	struct vgraph *vgraph = vgraph_new(&task->vgraph_params);
	(void)pgraph_new(pop, vgraph, &task->pgraph_params);
	vgraph_delete(vgraph);

	pmemobj_close(pop);
}

/*
 * dump_op -- XXX
 */
static void
dump_op(struct task *task)
{
	if (task->path == NULL || task->dump == NULL) {
		print_usage();
		exit(EXIT_FAILURE);
	}

	PMEMobjpool *pop = NULL;

	pop = pmemobj_open(task->path, GRAPH_LAYOUT);
	if (pop == NULL) {
		UT_FATAL("!pmemobj_open: %s", task->path);
	}

	struct pgraph *pgraph = pgraph_open(pop);

	pgraph_print(pgraph, task->dump);

	pmemobj_close(pop);
}

/*
 * defrag_op -- XXX
 */
static void
defrag_op(struct task *task)
{
	if (task->path == NULL) {
		print_usage();
		exit(EXIT_FAILURE);
	}

	PMEMobjpool *pop = NULL;

	pop = pmemobj_open(task->path, GRAPH_LAYOUT);
	if (pop == NULL) {
		UT_FATAL("!pmemobj_open: %s", task->path);
	}

	struct pgraph *pgraph = pgraph_open(pop);

	graph_defrag(pop, pgraph);

	pmemobj_close(pop);
}

/*
 * long_options -- command line options
 */
static const struct option long_options[] = {
	{"create",		no_argument,		NULL,	'c'},
	{"dump",		required_argument,	NULL,	'd'},
	{"defrag",		no_argument,		NULL,	'f'},
	{"path",		required_argument,	NULL,	'p'},
	{"seed",		required_argument,	NULL,	's'},
	{"max-nodes",	required_argument,	NULL,	'n'},
	{"max-edges",	required_argument,	NULL,	'e'},
	{"help",		no_argument,		NULL,	'h'},
	{NULL,			0,			NULL,	 0 },
};

#define OPT_STR "cdfp:q:s:n:e:h"

/*
 * parse_args -- parse command line arguments
 */
static void
parse_args(int argc, char *argv[], struct task *task)
{
	int opt;
	while ((opt = getopt_long(argc, argv, OPT_STR,
			long_options, NULL)) != -1) {
		switch (opt) {
		case 'c':
				task->op = OP_CREATE;
			break;
		case 'd':
				task->op = OP_DUMP;
				task->dump = optarg;
			break;
		case 'f':
				task->op = OP_DEFRAG;
			break;
		case 'p':
				task->path = optarg;
			break;
		case 's':
				task->seed = strtoul(optarg, NULL, 10);
			break;
		case 'n':
				task->vgraph_params.max_nodes =
						strtoul(optarg, NULL, 10);
			break;
		case 'e':
				task->vgraph_params.max_edges =
						strtoul(optarg, NULL, 10);
			break;
		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);
		default:
			print_usage();
			exit(EXIT_FAILURE);
		}
	}

	if (optind > argc) {
		print_usage();
		exit(EXIT_FAILURE);
	}
}

/*
 * task_init -- XXX
 */
static void
task_init(struct task *task)
{
	task->op = OP_MAX;
	task->path = NULL;
	task->dump = NULL;
	task->seed = 0;

	task->vgraph_params.max_nodes = 50;
	task->vgraph_params.max_edges = 10;
	task->vgraph_params.min_pattern_size = 8;
	task->vgraph_params.max_pattern_size = 1024;

	task->pgraph_params.max_graph_copies = 10;
}

int
main(int argc, char *argv[])
{
	START(argc, argv, TEST_NAME);

	struct task task;
	task_init(&task);

	parse_args(argc, argv, &task);

	switch (task.op) {
	case OP_CREATE:
		create_op(&task);
		break;
	case OP_DUMP:
		dump_op(&task);
		break;
	case OP_DEFRAG:
		defrag_op(&task);
		break;
	case OP_MAX:
	default:
		print_usage(argv[0]);
		exit(EXIT_FAILURE);
	}

	/*
	 * mix
	 * add edge
	 * dump
	 * defrag
	 * dump
	 */

	DONE(NULL);
}
