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
 * obj_defrag.c -- test for defrag feature
 */

#include<stdio.h>
#include<stdlib.h>
#include <time.h>

#define MAX_VERTICES 50
#define MAX_EDGES 10

struct node
{
	unsigned node_id;
	unsigned number_of_edges;
	unsigned *edges;
};

struct Graph
{
	unsigned numberV;
	struct node node[];
};

static unsigned
rand_nonzero(int max)
{
	int ret;
	do {
		ret = rand() % max;
	} while(ret == 0);

	return (unsigned)ret;
}

static void create_node(struct node *node, unsigned v)
{
	unsigned number_of_edges = rand_nonzero(MAX_EDGES);
	node->node_id = v;
	node->number_of_edges = number_of_edges;
	node->edges = malloc(sizeof(int) * number_of_edges);
}

static struct Graph* create_graph(unsigned numberOfVertices)
{
	struct Graph* graph = malloc(sizeof(struct Graph) + sizeof(struct node) * numberOfVertices);
	graph->numberV = numberOfVertices;

	for (unsigned i = 0; i < numberOfVertices; i++) {
		create_node(&graph->node[i], i);
	}
	return graph;
}

static struct node* get_node(struct Graph* graph, unsigned id_node)
{
	struct node *node;

	node = &graph->node[id_node];
	return node;
}

static void add_edge(struct Graph* graph)
{
	unsigned vertexCounter = 0;
	unsigned edgeCounter = 0;
	struct node* node;
	for (vertexCounter = 0; vertexCounter < graph->numberV; vertexCounter++) {
		node = get_node(graph, vertexCounter);
		unsigned number_of_edges = node->number_of_edges;
		//printf("Vertices: %d", node->node_id);
		for (edgeCounter = 0; edgeCounter < number_of_edges; edgeCounter++) {
			unsigned linkedVertex = (unsigned)rand() % graph->numberV;
			node->edges[edgeCounter] = linkedVertex;
			//printf("%d, ", linkedVertex);
		}
	}
}

static void print_graph(struct Graph* graph)
{
	struct node* node;
	for (unsigned i = 0; i < graph->numberV; i++) {
		node = get_node(graph, i);
		unsigned edges_num = node ->number_of_edges;
		printf("\nVertex: %d\n", node->node_id);
		for(unsigned i = 0; i < edges_num; i++){
			printf("%d, ", node->edges[i]);
		}
		printf("\n");
	}
}

int main(){
	/*number of nodes in a graph*/
	srand ( time(NULL) );
	unsigned numberOfVertices = rand_nonzero(MAX_VERTICES);
	printf("numberOfVertices: %d \n", numberOfVertices);

	/*graphs is 2 dimensional array of pointers*/
	struct Graph* graph = create_graph(numberOfVertices);
	add_edge(graph);
	print_graph(graph);

	/*
	 * mix
	 * add edge
	 * dump
	 * defrag
	 * dump
	 */
}
