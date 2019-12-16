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

typedef struct node
{
	int node_id;
	int number_of_edges;
	struct node* next;
} PMEMoid;

struct Graph
{
	int numberV;
	struct node* node[];
};

static struct node* create_node(int v)
{
	int number_of_edges = rand() % MAX_EDGES;
	struct node* newNode = malloc((long unsigned int)number_of_edges * sizeof(struct node*));
	newNode->node_id = v;
	newNode->number_of_edges = number_of_edges;
	//newNode->edges[NumberOfEdges];
	return newNode;
}

static struct Graph* create_graph(int numberOfVertices)
{
	struct Graph* graph = malloc(sizeof(struct Graph));
	graph->numberV = numberOfVertices;

	for (int i = 0; i < numberOfVertices; i++) {
		graph->node[i] = create_node(i);
	}
	return graph;
}

static struct node* get_node(struct Graph* graph, int id_node)
{
	struct node *node;

	node = graph->node[id_node];
	return node;
}

static void add_edge(struct Graph* graph, int numberOfVertices)
{
	int vertexCounter = 0;
	int edgeCounter = 0;
	struct node* node;
	for (vertexCounter = 0; vertexCounter < numberOfVertices; vertexCounter++) {
		node = get_node(graph, vertexCounter);
		int number_of_edges = node->number_of_edges;
		//printf("Vertices: %d", node->node_id);
		for (edgeCounter = 0; edgeCounter < number_of_edges; edgeCounter++) {
			if (rand()%2 == 1){
				srand ( time(NULL) );
				int linkedVertex = rand() % (numberOfVertices);
				node->next = graph->node[linkedVertex];
				//printf("%d, ", linkedVertex);
			}
		}
	}
}

static void print_graph(struct Graph* graph, int numberOfVertices)
{
	struct node* node;
	for (int i = 0; i < numberOfVertices; i++) {
		node = get_node(graph, i);
		int edge = node ->number_of_edges;
		printf("\nVertices: %d", node->node_id);
		for(int i = 0; i < edge; i++){
			printf("-> %d", node->node_id);
			node = node->next;
		}
		printf("\n");
	}
}

int main(){
	/*number of nodes in a graph*/
	srand ( time(NULL) );
	int numberOfVertices = rand() % MAX_VERTICES;

	/*graphs is 2 dimensional array of pointers*/
	if( numberOfVertices == 0)
		numberOfVertices++;
	struct Graph* graph = create_graph(numberOfVertices);
	add_edge(graph, numberOfVertices);
	printf("numberOfVertices: %d \n", numberOfVertices);
	print_graph(graph, numberOfVertices);

	/*
	 * mix
	 * add edge
	 * dump
	 * defrag
	 * dump
	 */
}
