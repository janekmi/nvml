/*
 * Copyright 2018, Intel Corporation
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
 * feature.c -- implementation of pmempool_features()
 */

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>

#include "libpmempool.h"
#include "pool.h"

/*
 * feature_set_open -- XXX
 */
static struct pool_set *
feature_set_open(const char *path)
{
	struct pool_set *set;
	if (util_poolset_read(&set, path)) {
		ERR("!invalid poolset file '%s'", path);
		return NULL;
	}

	/* readonly open */
	if (util_replica_open(set, 0, MAP_PRIVATE | MAP_NORESERVE)) {
		ERR("!replica open failed: replica 0");
		util_poolset_free(set);
		return NULL;
	}

	return set;
}

/*
 * feature_set_close -- XXX
 */
static void
feature_set_close(struct pool_set *set)
{
	if (util_replica_close(set, 0)) {
		FATAL("!replica close failed: replica 0");
	}

	util_poolset_free(set);
}

/*
 * feature_query -- XXX
 */
static int
feature_query(struct pool_set *set, uint32_t feature)
{
	struct pool_hdr *hdrp = HDR(REP(set, 0), 0);
	struct pool_hdr hdr;

	memcpy(&hdr, hdrp, sizeof(hdr));
	util_convert2h_hdr_nocheck(&hdr);
	return hdr.incompat_features & feature;
}

static int
pmempool_enable_singlehdr(const char *path)
{
	return 0;
}

static int
pmempool_disable_singlehdr(const char *path)
{
	return 1;
}

static int
pmempool_query_singlehdr(const char *path)
{
	return -1;
}

static int
pmempool_enable_checksum_2k(const char *path)
{
	return 0;
}

static int
pmempool_disable_checksum_2k(const char *path)
{
	return 1;
}

static int
pmempool_query_checksum_2k(const char *path)
{
	return -1;
}

static int
pmempool_enable_shutdown_state(const char *path)
{
	return 0;
}

static int
pmempool_disable_shutdown_state(const char *path)
{
	return 1;
}

static int
pmempool_query_shutdown_state(const char *path)
{
	return -1;
}

struct feature_funcs {
	int (*enable)(const char *);
	int (*disable)(const char *);
	int (*query)(struct pool_set *set);
};

static struct feature_funcs features[] = {
		{
			.enable = pmempool_enable_singlehdr,
			.disable = pmempool_disable_singlehdr,
			.query = pmempool_query_singlehdr
		},
		{
			.enable = pmempool_enable_checksum_2k,
			.disable = pmempool_disable_checksum_2k,
			.query = pmempool_query_checksum_2k
		},
		{
			.enable = pmempool_enable_shutdown_state,
			.disable = pmempool_disable_shutdown_state,
			.query = pmempool_query_shutdown_state
		},
};

#define FEATURE_FUNCS_MAX ARRAY_SIZE(features)

#ifndef _WIN32
static inline
#endif
int
pmempool_feature_enableU(const char *path, enum pmempool_feature f)
{
	LOG(3, "path %s feature %x", path, f);

	if (f >= FEATURE_FUNCS_MAX) {
		ERR("!Invalid feature argument");
		return -1;
	}

	return features[f].enable(path);
}

#ifndef _WIN32
static inline
#endif
int
pmempool_feature_disableU(const char *path, enum pmempool_feature f)
{
	LOG(3, "path %s feature %x", path, f);

	if (f >= FEATURE_FUNCS_MAX) {
		ERR("Invalid feature argument");
		return -1;
	}

	return features[f].disable(path);
}

#ifndef _WIN32
static inline
#endif
int
pmempool_feature_queryU(const char *path, enum pmempool_feature f)
{
	LOG(3, "path %s feature %x", path, f);

	struct pool_set *set;

	if (f >= FEATURE_FUNCS_MAX) {
		ERR("Invalid feature argument");
		return -1;
	}

	if ((set = feature_set_open(path)) == NULL) {
		return -1;
	}

	int query = features[f].query(set);

	feature_set_close(set);
	return query ? 1 : 0;
}

#ifndef _WIN32
/*
 * pmempool_feature_enable -- enable feature for poolset
 */
int
pmempool_feature_enable(const char *path, enum pmempool_feature f)
{
	return pmempool_feature_enableU(path, f);
}
#else
/*
 * pmempool_feature_enableW -- enable feature for poolset in widechar
 */
int
pmempool_feature_enableW(const wchar_t *path, enum pmempool_feature f)
{
	char *upath = util_toUTF8(path);
	if (upath == NULL) {
		ERR("Invalid poolest/pool file path.");
		return -1;
	}

	int ret = pmempool_feature_enableU(upath, f);

	util_free_UTF8(upath);
	return ret;
}
#endif

#ifndef _WIN32
/*
 * pmempool_feature_disable -- disable feature for poolset
 */
int
pmempool_feature_disable(const char *path, enum pmempool_feature f)
{
	return pmempool_feature_disableU(path, f);
}
#else
/*
 * pmempool_feature_disableW -- disable feature for poolset in widechar
 */
int
pmempool_feature_disableW(const wchar_t *path, enum pmempool_feature f)
{
	char *upath = util_toUTF8(path);
	if (upath == NULL) {
		ERR("Invalid poolest/pool file path.");
		return -1;
	}

	int ret = pmempool_feature_disableU(upath, f);

	util_free_UTF8(upath);
	return ret;
}
#endif

#ifndef _WIN32
/*
 * pmempool_feature_query -- query feature for poolset
 */
int
pmempool_feature_query(const char *path, enum pmempool_feature f)
{
	return pmempool_feature_queryU(path, f);
}
#else
/*
 * pmempool_feature_queryW -- query feature for poolset in widechar
 */
int
pmempool_feature_queryW(const wchar_t *path, enum pmempool_feature f)
{
	char *upath = util_toUTF8(path);
	if (upath == NULL) {
		ERR("Invalid poolest/pool file path.");
		return -1;
	}

	int ret = pmempool_feature_queryU(upath, f);

	util_free_UTF8(upath);
	return ret;
}
#endif
