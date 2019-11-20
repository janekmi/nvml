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
 * client.c -- librpma-based communicator client
 */

#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include <libpmem.h>
#include <librpma/base.h>
#include <librpma/memory.h>
#include <librpma/msg.h>
#include <librpma/rma.h>
#include <librpma/transmission.h>

#include "pstructs.h"
#include "mlog.h"
#include "msgs.h"

/* client-side assumptions */
#define MSG_LOG_MIN_CAPACITY (1000)

/* client-side persistent root object */
struct root_obj {
	struct msg_log ml;
};

/* derive minimal pool size from the assumptions */
#define POOL_MIN_SIZE \
	(MSG_LOG_SIZE(MSG_LOG_MIN_CAPACITY))

/* client context */
struct client_ctx {
	struct rpma_zone *zone;
	struct rpma_connection *conn;
	uint64_t exiting;
	uint64_t hello_done;

	/* persistent data and its derivatives */
	struct root_obj *root;
	size_t root_size;
	size_t ml_capacity; /* the message log capacity */
	struct rpma_memory_local *ml_local;
	struct rpma_memory_remote *ml_remote;

	/* transient data */
	struct client_row cr;
	struct rpma_memory_local *cr_local;
	struct rpma_memory_remote *cr_remote;

	/* RPMA send and recv messages */
	struct rpma_msg *send_msg;
	struct rpma_msg *recv_msg;

	/* writer */
	pthread_t thread;
};

/*
 * send_bye_bye -- send MSG_TYPE_BYE_BYE message
 */
static void
send_bye_bye(struct client_ctx *ctx)
{
	struct msg_t *msg;
	rpma_msg_get_ptr(ctx->send_msg, (void **)&msg);

	msg->base.type = MSG_TYPE_BYE_BYE;
	rpma_connection_send(ctx->conn, ctx->send_msg);
}

/*
 * writer_publish_msg -- XXX
 */
static void
writer_publish_msg(struct client_ctx *ctx)
{
	/* write the message */
	size_t offset = offsetof(struct client_row, msg);
	size_t length = sizeof(char) * MSG_SIZE_MAX;
	rpma_connection_write_and_commit(ctx->conn, ctx->cr_remote, offset,
			ctx->cr_local, offset, length);

	/* write the client status */
	/* XXX atomic_write? */
	offset = offsetof(struct client_row, status);
	length = sizeof(ctx->cr.status);
	rpma_connection_write_and_commit(ctx->conn, ctx->cr_remote, offset,
			ctx->cr_local, offset, length);
}

/*
 * writer_thread_func -- client writer entry point
 */
static void *
writer_thread_func(void *arg)
{
	struct client_ctx *ctx = arg;
	ssize_t ret;

	while(!ctx->exiting) {
		printf("< ");
		ret = read(STDIN_FILENO, ctx->cr.msg, MSG_SIZE_MAX);

		if (ret == 0)
			continue;
		if (ret < 0) {
			send_bye_bye(ctx);
			ctx->exiting = true; /* XXX atomic */
			break;
		}

		/* new message */
		ctx->cr.status = CLIENT_MSG_READY;
		writer_publish_msg(ctx);
	}

	return NULL;
}

/*
 * writer_init -- initialize writer thread
 */
static int
writer_init(struct client_ctx *ctx)
{
	return pthread_create(&ctx->thread, NULL, writer_thread_func,
			ctx);
}

/*
 * writer_fini -- cleanup writer thread
 */
static int
writer_fini(struct client_ctx *ctx)
{
	return pthread_join(ctx->thread, NULL);
}

/*
 * on_transmission_recv_process_ack -- process an ACK message
 */
static int
on_transmission_recv_process_ack(struct msg_t *msg, struct client_ctx *ctx)
{
	if (msg->ack.status != 0)
		return msg->ack.status;

	switch (msg->ack.original_msg_type) {
	case MSG_TYPE_BYE_BYE:
		writer_fini(ctx);
		return RPMA_E_OK;
	default:
		return RPMA_E_INVALID_MSG;
	}
}

/*
 * process_hello -- process MSG_TYPE_HELLO
 */
static int
process_hello(struct client_ctx *ctx, struct msg_t *msg)
{
	struct rpma_zone *zone = ctx->zone;

	/* decode and allocate remote memory regions descriptor */
	rpma_memory_remote_new(zone, &msg->hello.cr_id, &ctx->cr_remote);
	rpma_memory_remote_new(zone, &msg->hello.ml_id, &ctx->ml_remote);
	ctx->hello_done = true;

	/* initialize writer */
	writer_init(ctx);

	/* post back the recv msg - waiting for the mlog update */
	rpma_connection_recv_post(ctx->conn, ctx->recv_msg);

	/* prepare the hello message ACK */
	struct msg_t *ack;
	rpma_msg_get_ptr(ctx->send_msg, (void **)&ack);

	ack->base.type = MSG_TYPE_ACK;
	ack->ack.original_msg_type = MSG_TYPE_HELLO;
	ack->ack.status = 0;

	/* send the hello message ACK */
	return rpma_connection_send(ctx->conn, ctx->send_msg);
}

/*
 * process_hello -- process MSG_TYPE_MLOG_UPDATE
 */
static int
process_mlog_update(struct client_ctx *ctx, struct msg_t *msg)
{
	struct msg_log *ml = &ctx->root->ml;

	/* calculate remote read parameters */
	uintptr_t wptr = ml_get_wptr(ml);
	size_t offset = ml_offset(ml, wptr);
	size_t length = ml_offset(ml, msg->update.wptr) - offset;

	/* read mlog data */
	rpma_connection_read(ctx->conn,
			ctx->ml_local, offset, ctx->ml_remote, offset, length);

	/* progress the mlog write pointer */
	ml_set_wptr(ml, msg->update.wptr);

	/* post back the recv mlog update */
	rpma_connection_recv_post(ctx->conn, ctx->recv_msg);

	/* prepare the mlog update ack */
	struct msg_t *ack;
	rpma_msg_get_ptr(ctx->send_msg, (void **)&ack);

	ack->base.type = MSG_TYPE_ACK;
	ack->ack.original_msg_type = MSG_TYPE_HELLO;
	ack->ack.status = 0;

	rpma_connection_send(ctx->conn, ctx->send_msg);

	/* display the mlog */
	ml_read(ml);

	return 0;
}

/*
 * on_transmission_recv -- on transmission receive callback
 */
static int
on_transmission_recv(struct rpma_connection *conn, struct rpma_msg *rmsg, size_t length, void *uarg)
{
	struct client_ctx *ctx = uarg;

	/* obtain a message content */
	struct msg_t *msg;
	rpma_msg_get_ptr(rmsg, (void **)&msg);

	/* process the message */
	switch (msg->base.type) {
	case MSG_TYPE_ACK:
		return on_transmission_recv_process_ack(msg, ctx);
	case MSG_TYPE_HELLO:
		return process_hello(ctx, msg);
	case MSG_TYPE_MLOG_UPDATE:
		return process_mlog_update(ctx, msg);
	default:
		return RPMA_E_INVALID_MSG;
	}
}

/*
 * hello_init -- send the hello message and prepare for ACK
 */
static void
hello_init(struct client_ctx *ctx)
{
	struct rpma_zone *zone = ctx->zone;

	/* allocate & post the hello message recv */
	rpma_msg_new(zone, RPMA_MSG_RECV, &ctx->recv_msg);
	rpma_connection_recv_post(ctx->conn, ctx->recv_msg);

	/* allocate the hello message ACK */
	rpma_msg_new(zone, RPMA_MSG_SEND, &ctx->send_msg);
}

/*
 * hello_fini -- cleanup after the hello message exchange
 */
static void
hello_fini(struct client_ctx *ctx)
{
	rpma_msg_delete(&ctx->send_msg);
	rpma_msg_delete(&ctx->recv_msg);
}

/*
 * remote_init -- prepare RPMA context
 */
static void
remote_init(struct client_ctx *ctx, const char *addr, const char *service)
{
	/* prepare RPMA configuration */
	struct rpma_config *cfg;
	rpma_config_new(&cfg);
	rpma_config_set_addr(cfg, addr);
	rpma_config_set_service(cfg, service);

	/* allocate RPMA context */
	rpma_zone_new(cfg, &ctx->zone);
	struct rpma_zone *zone = ctx->zone;

	/* destroy RPMA configuration */
	rpma_config_delete(&cfg);

	/* register local memory regions */
	rpma_memory_local_new(zone, &ctx->root->ml, MSG_LOG_SIZE(ctx->ml_capacity),
			RPMA_MR_READ_DST, &ctx->ml_local);
	rpma_memory_local_new(zone, &ctx->cr, sizeof(ctx->cr),
			RPMA_MR_WRITE_SRC, &ctx->cr_local);
}

#define RPMA_TIMEOUT (60) /* 1m */

/*
 * remote_main -- main entry-point to RPMA
 */
static void
remote_main(struct client_ctx *ctx)
{
	struct rpma_zone *zone = ctx->zone;

	rpma_connection_new(zone, &ctx->conn);

	hello_init(ctx);
	rpma_connection_make(ctx->conn, RPMA_TIMEOUT);

	/* register transmission callback */
	rpma_transmission_register_on_recv(ctx->conn, on_transmission_recv);

	rpma_transmission_loop(ctx->conn, ctx);

	hello_fini(ctx);
}

/*
 * remote_fini -- delete RPMA content
 */
static void
remote_fini(struct client_ctx *ctx)
{
	/* deallocate local memory regions */
	rpma_memory_local_delete(&ctx->cr_local);
	rpma_memory_local_delete(&ctx->ml_local);

	/* deallocate remote memory regions */
	if (ctx->hello_done) {
		rpma_memory_remote_delete(&ctx->cr_remote);
		rpma_memory_remote_delete(&ctx->ml_remote);
	}

	rpma_zone_delete(&ctx->zone);
}

/*
 * pmem_init -- map the server root object
 */
static void
pmem_init(struct client_ctx *ctx, const char *path)
{
	ctx->root = pmem_map_file(path, POOL_MIN_SIZE, PMEM_FILE_CREATE, O_RDWR,
			&ctx->root_size, NULL);
	const size_t ml_offset = offsetof(struct root_obj, ml);
	size_t ml_size = ctx->root_size - ml_offset;
	ml_init(&ctx->root->ml, ml_size);
}

/*
 * pmem_fini -- unmap the persistent part
 */
static void
pmem_fini(struct client_ctx *ctx)
{
	pmem_unmap(ctx->root, ctx->root_size);
}

int
main(int argc, char *argv[])
{
	const char *path = argv[1];
	const char *addr = argv[2];
	const char *service = argv[3];

	struct client_ctx ctx = {0};

	pmem_init(&ctx, path);
	remote_init(&ctx, addr, service);

	remote_main(&ctx);

	remote_fini(&ctx);
	pmem_fini(&ctx);

	return 0;
}
