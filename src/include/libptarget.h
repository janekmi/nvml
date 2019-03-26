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
 * libptarget.h -- definitions of libptarget entry points (EXPERIMENTAL)
 *
 * This library allows to expose persistent memory for low-level remote access
 * utilizing RDMA-capable RNICs.
 *
 * See libptarget(3) for details.
 */

#ifndef LIBPTARGET_H
#define LIBPTARGET_H 1

#include <sys/types.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ptarget Ptarget;

struct ptarget_pool {
	void *pool;
	size_t size;
	int is_pmem;
	void *ctx;
};

struct ptarget_funcs {
	struct ptarget_pool *(*create)(const char *pool_name, size_t pool_size,
			void *ctx, size_t ctx_size);
	struct ptarget_pool *(*open)(const char *pool_name, size_t pool_size,
			void *ctx, size_t ctx_size);
	int (*close)(struct ptarget_pool *);
	int (*ctrl)(const char *pool_name, void *ctrl, size_t ctrl_size);
	int (*msg)(struct ptarget_pool *, void *msg, size_t msg_size);
};

#define PTARGET_PERSIST_APPLIANCE	(1 << 0)
#define PTARGET_PERSIST_GENERAL		(1 << 1)

Ptarget *ptarget_reg(struct ptarget_funcs *, unsigned flags);

int ptarget_dereg(Ptarget *ptarget);

/*
 * PTARGET_MAJOR_VERSION and PTARGET_MINOR_VERSION provide the current version
 * of the libptarget API as provided by this header file. Applications can
 * verify that the version available at run-time is compatible with the version
 * used at compile-time by passing these defines to ptarget_check_version().
 */
#define PTARGET_MAJOR_VERSION 1
#define PTARGET_MINOR_VERSION 3
const char *ptarget_check_version(unsigned major_required,
		unsigned minor_required);

const char *ptarget_errormsg(void);

#ifdef __cplusplus
}
#endif
#endif	/* libptarget.h */
