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
 * pgraph.h -- persistent graph representation
 */

#ifndef OBJ_DEFRAG_ADV_PGRAPH
#define OBJ_DEFRAG_ADV_PGRAPH

#include <libpmemobj/base.h>

struct pgraph_params
{
	unsigned max_graph_copies;
};

struct pnode
{
	unsigned node_id;
	unsigned edges_num;
	size_t pattern_size;
	size_t size;
	PMEMoid edges[];
};

struct pgraph
{
	unsigned nodes_num;
	PMEMoid nodes[];
};

size_t pgraph_size_estimate(struct vgraph *vgraph,
		struct pgraph_params *params);

struct pgraph *pgraph_new(PMEMobjpool *pop, struct vgraph *vgraph,
		struct pgraph_params *params);
void pgraph_delete(struct pgraph *graph);
struct pgraph *pgraph_open(PMEMobjpool *pop);

void pgraph_print(struct pgraph *graph, const char *dump);

#endif /* pgraph.h */
