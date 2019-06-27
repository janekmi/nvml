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
 * basic.c -- basic example for the librpma
 */
#include <getopt.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#include <librpma.h>

#include "sockets.h"

/* arbitrary chosen memory regions identifiers */
#define SERVER_MRID 37
#define CLIENT_MRID 41

/* memory region size */
#define MR_HALF 16
#define MR_LENGTH (MR_HALF * 2 + 1)

/* memory region description */
struct mr_t {
	unsigned id;
	int des;
	size_t len;
};

/* RPMA parameters - exchanged between server and the client */
struct app_rpma_params_t {
	uint16_t service;
	unsigned nlanes;
};

#define FINI_CLOSING_MAGIC 83

struct app_rpma_fini_t {
	int closing_magic;
};

/* RPMA state */
struct app_rpma_state_t {
	struct app_rpma_params_t params;
	unsigned nlanes;

	RPMAdomain *domain;
	RPMAconn *conn;

	struct mr_t mr_local;
	struct mr_t mr_remote;
	void *buff;
};

#define ADDR_MAX_LEN 100
#define PORT_DEFAULT 7777

struct args_t {
	/* input parameters */
	int is_server;
	char addr[ADDR_MAX_LEN];
	uint16_t port;

	/* sockets */
	struct sockets_node *sn;

	/* RPMA */
	struct app_rpma_state_t rpma;
};

/*
 * server_sockets_init -- initialize socket and accept a connection
 */
static int
server_sockets_init(struct args_t *args)
{
	int ret = 0;

	if ((ret = sockets_server_new(args->addr, args->port, &args->sn)))
		return ret;

	if ((ret = sockets_server_accept(args->sn)))
		goto err_accept;

err_accept:
	sockets_close(args->sn);
	return ret;
}

/*
 * server_sockets_fini_nowait -- do not wait for fini message and close the
 * socket
 */
static int
server_sockets_fini_nowait(struct args_t *args)
{
	return sockets_close(args->sn);
}

/*
 * server_sockets_fini_wait -- wait for fini message and close the socket
 */
static int
server_sockets_fini_wait(struct args_t *args)
{
	int ret = 0;
	struct app_rpma_fini_t fini;
	if ((ret = sockets_recv(args->sn, &fini, sizeof(fini))))
		return ret;

	if (fini.closing_magic != FINI_CLOSING_MAGIC)
		return 1;

	return sockets_close(args->sn);
}

/*
 * common_rpma_mr_init -- allocate local memory region and open it for RPMA
 */
static int
common_rpma_mr_init(struct app_rpma_state_t *rpma, unsigned mrid)
{
	struct mr_t *mrl = &rpma->mr_local;
	rpma->buff = calloc(MR_LENGTH, sizeof(char));
	if (!rpma->buff)
		return 1;
	mrl->id = mrid;
	mrl->len = MR_LENGTH;
	mrl->des = rpma_mr_open(rpma->domain, rpma->buff, mrl->len, mrl->id);

	return 0;
}

/*
 * common_rpma_mr_fini -- close the local memory region and release it
 */
static int
common_rpma_mr_fini(struct app_rpma_state_t *rpma)
{
	int ret = rpma_mr_close(rpma->domain, rpma->mr_local.des);
	free(rpma->buff);

	return ret;
}

/*
 * server_rpma_init -- initialize RPMA domain and required RPMA resources
 */
static int
server_rpma_init(struct args_t *args)
{
	struct app_rpma_state_t *rpma = &args->rpma;
	struct app_rpma_params_t *params = &rpma->params;
	rpma->domain = rpma_listen(args->addr, &params->service, &params->nlanes);
	if (!rpma->domain)
		return 1;

	/* assume single RPMA connection will consume all RPMA domain lanes */
	rpma->nlanes = params->nlanes;

	if (common_rpma_mr_init(rpma, SERVER_MRID))
		goto err_mr_init;

	return 0;

err_mr_init:
	rpma_shutdown(rpma->domain);
	return 1;
}

/*
 * server_rpma_fini -- release resources and close the domain
 */
static int
server_rpma_fini(struct app_rpma_state_t *rpma)
{
	int ret = 0;

	if ((ret == common_rpma_mr_fini(rpma)))
		return ret;

	return rpma_shutdown(rpma->domain);
}

/*
 * server_rpma_conn_init -- accept RPMA connection
 */
static int
server_rpma_conn_init(struct app_rpma_state_t *rpma)
{
	rpma->conn = rpma_accept(rpma->domain, &rpma->nlanes);

	if (!rpma->conn)
		return 1;

	return 0;
}

/*
 * server_rpma_conn_fini -- accept RPMA connection
 */
static int
server_rpma_conn_fini(struct app_rpma_state_t *rpma)
{
	return rpma_close(rpma->conn);
}

/*
 * server -- server side sequence
 */
static int
server(struct args_t *args)
{
	int ret = 0;

	if ((ret = server_sockets_init(args)))
		return ret;

	if ((ret = server_rpma_init(args)))
		goto err_rpma_init;

	struct app_rpma_state_t *rpma = &args->rpma;

	/* send RPMA parameters via sockets */
	struct app_rpma_params_t *params = &rpma->params;
	if ((ret = sockets_send(args->sn, (void *)params, sizeof(*params))))
		goto err_send;

	/* establish RPMA connection */
	if ((ret = server_rpma_conn_init(rpma)))
		goto err_rpma_conn;

	if ((ret = server_sockets_fini_wait(args)))
		goto err_rpma_conn;

	/* print out received string */
	printf("%s\n", (char *)rpma->buff);

	if ((ret = server_rpma_conn_fini(rpma)))
	{
		(void)server_rpma_fini(rpma);
		return ret;
	}

	if ((ret = server_rpma_fini(rpma)))
		return ret;

	return 0;

err_rpma_conn:
err_send:
	(void)server_rpma_fini(rpma);
err_rpma_init:
	(void)server_sockets_fini_nowait(args);
	return ret;
}

/*
 * client_sockets_init -- connect to the server socket
 */
static int
client_sockets_init(struct args_t *args)
{
	return sockets_client(args->addr, args->port, &args->sn);
}

/*
 * client_sockets_fini -- disconnect from the server socket
 */
static int
client_sockets_fini(struct args_t *args)
{
	int ret = 0;
	struct app_rpma_fini_t fini;
	fini.closing_magic = FINI_CLOSING_MAGIC;
	if ((ret == sockets_send(args->sn, &fini, sizeof(fini))))
		return ret;

	return sockets_close(args->sn);
}

/*
 * client_rpma_mr_init -- initialize local and remote memory region
 */
static int
client_rpma_mr_init(struct app_rpma_state_t *rpma)
{
	int ret = 0;

	/* initialize local memory region */
	if (common_rpma_mr_init(rpma, CLIENT_MRID))
		return 1;

	/* obtain the remote memory region descriptor */
	struct mr_t *mrr = &rpma->mr_remote;
	mrr->id = SERVER_MRID;
	mrr->des = rpma_conn_mr_get(rpma->conn, mrr->id, &mrr->len);
	if (mrr->des < 0) {
		ret = -mrr->des;
		goto err_conn_mr_get;
	}
	assert(rpma->mr_local.len <= rpma->mr_remote.len);

	return ret;

err_conn_mr_get:
	(void)common_rpma_mr_fini(rpma);
	return ret;
}

/*
 * client_rpma_mr_fini -- release local memory region
 */
static int
client_rpma_mr_fini(struct app_rpma_state_t *rpma)
{
	return common_rpma_mr_fini(rpma);
}

/*
 * client_rpma_init -- establish RPMA connection to the server and initialize
 * required resources
 */
static int
client_rpma_init(struct args_t *args)
{
	int ret = 0;
	struct app_rpma_state_t *rpma = &args->rpma;
	struct app_rpma_params_t *params = &rpma->params;
	rpma->nlanes = rpma->params.nlanes;

	rpma->conn = rpma_connect(args->addr, params->service, &rpma->nlanes);
	if (!rpma->conn)
		return 1;

	rpma->domain = rpma_get_domain(rpma->conn);
	assert(rpma->domain != NULL);

	if ((ret = client_rpma_mr_init(rpma)))
		goto err_mr_init;

	return 0;

err_mr_init:
	(void)rpma_close(rpma->conn);
	return ret;
}

/*
 * client_rpma_fini -- disconnect RDMA
 */
static int
client_rpma_fini(struct app_rpma_state_t *rpma)
{
	int ret = 0;
	if ((ret = client_rpma_mr_fini(rpma)))
		return ret;

	return rpma_close(rpma->conn);
}

/*
 * client_rpma_use -- example of RPMA operations usage
 */
static int
client_rpma_use(struct app_rpma_state_t *rpma)
{
	int ret = 0;
	int dest_mrdes = rpma->mr_remote.des;
	int src_mrdes = rpma->mr_local.des;
	int lane = 0;

	/* copy the second part first */
	if ((ret = rpma_write(rpma->conn, dest_mrdes, 0, src_mrdes, MR_HALF,
			MR_HALF, lane)))
		return ret;

	/* copy the first part */
	if ((ret = rpma_write(rpma->conn, dest_mrdes, MR_HALF, src_mrdes, 0,
				MR_HALF, lane)))
			return ret;

	/* copy the NULL terminator */
	size_t null_term_off = MR_HALF * 2;
	if ((ret = rpma_write(rpma->conn, dest_mrdes, null_term_off, src_mrdes,
			null_term_off, 1, lane)))
				return ret;

	if ((ret = rpma_flush(rpma->conn, lane)))
		return ret;

	return 0;
}

/*
 * client -- client side sequence
 */
static int
client(struct args_t *args)
{
	int ret = 0;

	if ((ret = client_sockets_init(args)))
		return ret;

	/* receive RPMA parameters via sockets */
	struct app_rpma_params_t *params = &args->rpma.params;
	if ((ret = sockets_recv(args->sn, &params, sizeof(params))))
		goto err_recv;

	if ((ret = client_rpma_init(args)))
		goto err_rpma_init;

	struct app_rpma_state_t *rpma = &args->rpma;

	/* initialize local memory region */
	char *buff = rpma->buff;
	struct mr_t *mrl = &rpma->mr_local;
	for (size_t i = 0; i < mrl->len - 1; ++i) {
		buff[i] = 'A' + i;
	}
	buff[mrl->len - 1] = '\0';

	/* remote persistent memory access */
	if ((ret = client_rpma_use(rpma)))
		goto err_rpma_use;

	if ((ret = client_rpma_fini(rpma)))
		goto err_rpma_fini;

	if ((ret = client_sockets_fini(args)))
		return ret;

	return 0;

err_rpma_use:
	(void)client_rpma_fini(rpma);
err_rpma_init:
err_rpma_fini:
err_recv:
	(void)client_sockets_fini(args);
	return ret;
}

/*
 * usage -- print application usage
 */
static void
usage(const char *app)
{
	printf("%s -s [-a addr] [-p port]\n", app);
	printf("%s -c -a addr [-p port]\n", app);
	printf("\t-c\t\tclient side\n");
	printf("\t-s\t\tserver side\n");
	printf("\t-a addr\t\taddress\n");
	printf("\t-p port\t\tport\n");
}

/*
 * parse_args -- parse command line arguments
 */
static int
parse_args(int argc, char *argv[], struct args_t *args)
{
	int ret = 0;
	int op;

	while ((op=getopt(argc, argv, "sca:p:")) != -1) {
		switch (op) {
		case 'a':
			assert(ADDR_MAX_LEN >= strlen(optarg) + 1);
			strcpy(args->addr, optarg);
			break;
		case 'p':
			args->port = htons(atoi(optarg));
			break;
		case 's':
			args->is_server = 1;
			break;
		case 'c':
			args->is_server = 0;
			break;
		default:
			usage(argv[0]);
			ret = EINVAL;
			goto out;
		}
	}
out:
	return ret;
}

int
main(int argc, char *argv[])
{
	struct args_t args;
	memset(&args, 0, sizeof(args));
	args.is_server = -1;
	args.port = PORT_DEFAULT;
	parse_args(argc, argv, &args);

	if (args.is_server == -1)
		goto err;
	if (strlen(args.addr) == 0)
		goto err;

	int ret = 0;
	if (args.is_server)
		ret = server(&args);
	else
		ret = client(&args);

	return ret;

err:
	usage(argv[0]);
	return 1;
}
