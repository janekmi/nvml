/*
 * Copyright 2019-2020, Intel Corporation
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
 * hello.c -- hello world for librpma
 */
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <pthread.h>

#include <libpmem.h>
#include <librpma.h>

enum lang_t {en, es};

static const char *hello_str[] = {
	[en] = "Hello world!",
	[es] = "¡Hola Mundo!"
};

#define LANG_NUM	(sizeof(hello_str) / sizeof(hello_str[0]))

#define STR_SIZE	100

struct hello_t {
	enum lang_t lang;
	char str[STR_SIZE];
	uint64_t valid;
};

#define HELLO_SIZE	(sizeof(struct hello_t))

#define TYPE_SERVER ('s')
#define TYPE_CLIENT ('c')

struct base_t {
	const char *addr;
	const char *service;
	const char *file; /* applicable to server only */
	char type;
	void *specific;

	struct rpma_device *rdev;
	struct rpma_zone *zone;
	struct rpma_conn *conn;
};

struct compat_mode_t {
	struct rpma_compat *compat;
	pthread_t thread;
	uint64_t exit;
};

struct server_t {
	struct hello_t *ptr;
	size_t total_size;

	struct rpma_conn_cfg *ccfg;
	struct rpma_compat *compat;

	struct rpma_memory_local *mem;
	struct rpma_memory_id id;
};

struct msg_t {
	struct rpma_memory_id id;
	uint64_t init_required;
};

struct client_t {
	struct {
		struct hello_t *ptr;
		struct rpma_memory_local *mem;
	} local;

	struct {
		struct rpma_memory_remote *mem;
		uint64_t init_required;
	} remote;
};

static inline void
hello_write(struct hello_t *hello, enum lang_t lang)
{
	hello->lang = lang;
	strncpy(hello->str, hello_str[hello->lang], STR_SIZE);
	hello->valid = 1;
}

static int
hello_init(struct base_t *b)
{
	assert(b->type == TYPE_CLIENT);
	struct client_t *clnt = b->specific;

	hello_write(clnt->local.ptr, en);

	printf("write message to the target...\n");
	rpma_write(b->conn, clnt->remote.mem, 0, clnt->local.mem, 0,
			HELLO_SIZE, NULL, RPMA_NO_COMPLETION); /* op context */
	rpma_commit(b->conn, RPMA_WITH_COMPLETION);
	int ret = rpma_complete(b->conn, RPMA_OP_COMMIT, NULL);
	assert(ret == RPMA_OP_COMMIT);

	return 0;
}

static int
hello_revisit(struct base_t *b)
{
	assert(b->type == TYPE_CLIENT);
	struct client_t *clnt = b->specific;
	int ret = 0;

	printf("read message from the target...\n");
	rpma_read(b->conn, clnt->local.mem, 0, clnt->remote.mem, 0,
			HELLO_SIZE, NULL, RPMA_WITH_COMPLETION);
	ret = rpma_complete(b->conn, RPMA_OP_READ);
	assert(ret == RPMA_OP_READ);

	printf("translating...\n");
	struct hello_t *hello = clnt->local.ptr;
	enum lang_t lang = (enum lang_t)((hello->lang + 1) % LANG_NUM);
	hello_write(hello, lang);
	printf("%s\n", hello->str);

	printf("write message to the target...\n");
	rpma_write(b->conn, clnt->remote.mem, 0, clnt->local.mem, 0,
			HELLO_SIZE, NULL, RPMA_NO_COMPLETION);
	rpma_commit(b->conn, RPMA_WITH_COMPLETION);
	ret = rpma_complete(b->conn, RPMA_OP_COMMIT, NULL);
	assert(ret == RPMA_OP_COMMIT);

	return 0;
}

static int
msg_recv(struct base_t *b)
{
	assert(b->type == TYPE_SERVER);
	struct client_t *clnt = b->specific;

	struct msg_t *msg = NULL;
	size_t msg_size = 0;
	struct rpma_memory_id id;
	int ret = 0;

	/* wait for RECV */
	ret = rpma_complete(b->conn, RPMA_OP_RECV, NULL);
	assert(ret == RPMA_OP_RECV);

	/* process RECV'ed message */
	rpma_recv_buffer_get(b->conn, &msg_size, &msg);
	assert(msg_size == sizeof(*msg));
	memcpy(&id, &msg->id, sizeof(id));
	clnt->remote.init_required = msg->init_required;
	rpma_recv_buffer_return(b->conn, msg);

	/* create remote memory */
	rpma_memory_remote_new(b->zone, &id, &clnt->remote.mem);

	return 0;
}

static int
msg_send(struct base_t *b)
{
	assert(b->type == TYPE_SERVER);
	struct server_t *svr = b->specific;

	struct msg_t *msg;

	rpma_send_buffer_get(b->conn, sizeof(*msg), (void **)&msg);
	memcpy(&msg->id, &svr->id, sizeof(svr->id));
	if (!svr->ptr->valid)
		msg->init_required = 1;
	int ret = rpma_msg_send(b->conn, msg);

	return ret;
}

#define TIMEOUT_TIME (15000) /* 15s */

static int
common_zone_init(struct base_t *b)
{
	/* configure an RPMA zone */
	struct rpma_zone_cfg *zcfg;
	rpma_zone_cfg_new(&zcfg);
	rpma_zone_cfg_set_msg_buffer_alloc_funcs(zcfg, malloc, free);
	rpma_zone_cfg_set_persist_func(zcfg, pmem_persist);
	rpma_zone_cfg_set_memcpy_persist_func(zcfg, pmem_memcpy_persist);

	int ret = rpma_zone_new(zcfg, b->rdev, &b->zone);
	rpma_zone_cfg_delete(&zcfg);

	if (!b->zone) {
		fprintf(stderr, "Cannot create an RPMA zone: %d\n", ret);
		return ret;
	}

	return 0;
}

static int
common_conn_cfg_init(struct rpma_conn_cfg **ccfg)
{
	struct rpma_conn_cfg *tmp;

	rpma_conn_cfg_new(&tmp);
	rpma_conn_cfg_set_send_buffers_num(tmp, 10);
	rpma_conn_cfg_set_recv_buffers_num(tmp, 10);
	rpma_conn_cfg_set_max_msg_size(tmp, sizeof(struct msg_t));
	rpma_conn_cfg_set_setup_timeout(tmp, TIMEOUT_TIME);
	rpma_conn_cfg_set_op_timeout(tmp, TIMEOUT_TIME);

	*ccfg = tmp;

	return 0;
}

static int
server_init(struct base_t *b)
{
	assert(b->type == TYPE_SERVER);
	struct server_t *svr = b->specific;

	/* obtain a device */
	int ret = rpma_device_by_src_address(b->addr, &b->rdev);
	if (ret)
		return ret;

	/* preare RPMA zone */
	ret = common_zone_init(b);
	assert(ret == 0);

	/* register local memory */
	rpma_memory_new(b->zone, svr->ptr, HELLO_SIZE,
			RPMA_MR_WRITE_DST | RPMA_MR_READ_SRC, &svr->mem);
	rpma_memory_get_id(svr->mem, &svr->id);

	/* prepare connection config */
	ret = common_conn_cfg_init(&svr->ccfg);
	if (ret)
		goto err_conn_cfg_init;

	/* creates an RPMA compat object and it to a connection config */
	rpma_compat_new(b->rdev, &svr->compat);
	rpma_conn_cfg_set_op_types(svr->ccfg, 0);
	rpma_conn_cfg_set_compat(svr->ccfg, svr->compat);

	return 0;

err_conn_cfg_init:
	assert(0);
	/* XXX cleanup required */
	return ret;
}

static int
client_init(struct base_t *b)
{
	assert(b->type == TYPE_CLIENT);
	struct client_t *clnt = b->specific;

	/* obtain a device */
	struct rpma_device *rdev = NULL;
	int ret = rpma_device_by_dst_address(b->addr, &rdev);
	if (ret)
		return ret;

	/* prepare RPMA zone */
	ret = common_zone_init(b, rdev);

	/* register local memory */
	ret = rpma_memory_new(b->zone, clnt->local.ptr, HELLO_SIZE,
			RPMA_MR_WRITE_SRC | RPMA_MR_READ_DST, &clnt->local.mem);

	/* connect */
	struct rpma_conn_cfg *ccfg;
	ret = common_conn_cfg_init(&ccfg);
	if (ret)
		goto err_conn_cfg_init;

	int op_flags = RPMA_OP_READ | RPMA_OP_WRITE | RPMA_OP_COMMIT;
	rpma_conn_cfg_set_op_types(ccfg, op_flags);

	ret = rpma_connect(b->zone, ccfg, b->addr, b->service, &b->conn);
	assert(ret == 0);

	return 0;

err_conn_cfg_init:
	assert(0);
	/* XXX cleanup required */
	return ret;
}

static void
peer_fini(struct base_t *b)
{
	struct client_t *clnt;
	struct server_t *svr;

	switch (b->type) {
	case TYPE_CLIENT:
		clnt = b->specific;

		rpma_memory_local_delete(&clnt->local.mem);
		break;
	case TYPE_SERVER:
		svr = b->specific;
		rpma_memory_local_delete(&svr->mem);
	}

	rpma_zone_delete(&b->zone);

	rpma_device_delete(&b->rdev);
}

static void
parse_args(int argc, char *argv[], struct base_t *b)
{
	if (argc < 4)
		goto err_usage;

	b->type = argv[1][0];
	b->addr = argv[2];
	b->service = argv[3];

	switch (b->type) {
	case TYPE_CLIENT:
		break;
	case TYPE_SERVER:
		if (argc < 5)
			goto err_usage;

		b->file = argv[4];
		break;
	default:
		goto err_usage;
	}

	return;

err_usage:
	fprintf(stderr,
			"usage:\n"
			"\t%s c <addr> <service>\n"
			"\t%s s <addr> <service> <file>\n",
			argv[0], argv[0]);
	exit(1);
}

static void *
alloc_memory()
{
	long pagesize = sysconf(_SC_PAGESIZE);
	if (pagesize < 0) {
		perror("sysconf");
		exit(1);
	}

	/* allocate a page size aligned local memory pool */
	void *mem;
	int ret = posix_memalign(&mem, pagesize, HELLO_SIZE);
	if (ret) {
		fprintf(stderr, "posix_memalign: %s\n", strerror(ret));
		exit(1);
	}

	return mem;
}

static int
server_compat_routine(void *arg)
{
	struct compat_mode_t *mode = arg;
	int ret;

	do {
		ret = rpma_compat_mode(mode->compat, TIMEOUT_TIME);
	} while (mode->exit == 0 && ret == 0);

	int conn_num = 0;
	ret = rpma_compat_get_conn_num(mode->compat, &conn_num);
	assert(ret == 0 && conn_num == 0);

	return ret;
}

static void
server_pmem(struct base_t *b)
{
	assert(b->type == TYPE_SERVER);
	struct server_t *svr = b->specific;

	/* try creating a memory pool */
	size_t len = HELLO_SIZE;
	int flags = PMEM_FILE_CREATE;
	mode_t mode = 0666;
	svr->ptr = pmem_map_file(b->file, len, flags, mode,
				&svr->total_size, NULL);

	if (!svr->ptr) {
		assert(errno == EEXIST);

		/* try opening a memory pool */
		len = 0;
		flags = 0;
		svr->ptr = pmem_map_file(b->file, len, flags, 0,
				&svr->total_size, NULL);
	}

	assert(svr->ptr != NULL);
}

static int
server_main(struct base_t *b)
{
	assert(b->type == TYPE_SERVER);

	/* initialize server's memory */
	struct server_t *svr = calloc(1, sizeof(*svr));
	b->specific = svr;
	server_pmem(b);

	int ret = server_init(b);
	if (ret)
		goto err_init;

	/* initialize compat mode thread */
	struct compat_mode_t mode = {0};
	mode.compat = svr->compat;
	pthread_create(&mode.thread, NULL, server_compat_routine, &mode);

	do {
		ret = rpma_accept(b->zone, svr->ccfg, b->addr, b->service, &b->conn);
		if (ret)
			goto err_accept;

		ret = msg_send(b);
		if (ret)
			goto err_msg_send;

		ret = rpma_disconnect(b->conn, RPMA_DISCONNECT_WHEN_DONE);
		assert(ret == 0);
	} while (0);

	/* join the compat mode thread */
	mode.exit = 1;
	pthread_join(mode.thread, NULL);

err_msg_recv:
err_msg_send:
err_accept:
	peer_fini(b);
err_init:
	pmem_unmap(svr->ptr, svr->total_size);
	free(svr);
	return ret;
}

static int
client_main(struct base_t *b)
{
	assert(b->type == TYPE_CLIENT);

	/* initialize client's memory */
	struct client_t *clnt = calloc(1, sizeof(*clnt));
	clnt->local.ptr = alloc_memory();
	b->specific = clnt;

	/* connect */
	int ret = client_init(b);
	if (ret)
		goto err_init;

	/* receive a message from the server */
	ret = msg_recv(b);
	if (ret)
		goto err_msg_recv;

	/* hello RPMA */
	if (clnt->remote.init_required)
		hello_init();
	else
		hello_revisit();

	/* disconnect */
	do {
		ret = rpma_disconnect(b->conn, RPMA_DISCONNECT_NOW);
	} while (ret == RPMA_E_TIMEOUT);

err_msg_recv:
err_msg_send:
	peer_fini(b);
err_init:
	free(clnt->local.ptr);
	free(clnt);
	return ret;
}

int
main(int argc, char *argv[])
{
	int ret = 0;

	struct base_t base;
	memset(&base, 0, sizeof(base));

	parse_args(argc, argv, &base);

	if (base.type == TYPE_CLIENT)
		ret = client_main(&base);
	else /* TYPE_SERVER */
		ret = server_main(&base);

	if (ret)
		fprintf(stderr, "hello: %s\n", strerror(ret));

err_msg_recv:
err_msg_send:
err_remote_init:
	mem_fini(&base);

	return ret;
}
