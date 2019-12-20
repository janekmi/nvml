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

#include "unittest.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "vgraph.h"
#include "pgraph.h"

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

int
main(int argc, char *argv[])
{
	START(argc, argv, "obj_defrag_advanced");

	const char *path = argv[1];
	PMEMobjpool *pop = NULL;

	pop = pmemobj_create(path, POBJ_LAYOUT_NAME(basic),
		0, S_IWUSR | S_IRUSR);
	if (pop == NULL) {
		UT_FATAL("!pmemobj_create: %s", path);
	}

	srand(0);

	struct vgraph *vgraph = vgraph_new();
	struct pgraph *pgraph = pgraph_new(pop, vgraph);
	vgraph_delete(vgraph);
	pgraph_print(pgraph);

	graph_defrag(pop, pgraph);

	/*
	 * mix
	 * add edge
	 * dump
	 * defrag
	 * dump
	 */

	pmemobj_close(pop);

	DONE(NULL);
}
