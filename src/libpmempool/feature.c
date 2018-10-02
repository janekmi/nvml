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
 * feature.c -- implementation of pmempool_feature_(enable|disable|query)()
 */

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>

#include "libpmempool.h"
#include "util_pmem.h"
#include "pool_hdr.h"
#include "pool.h"

#define RW	0
#define RDONLY	1

#define FEATURE_MAXPRINT ((size_t)1024)
static char buff[FEATURE_MAXPRINT];

/*
 * buff_concat -- (internal) concat formatted string to string buffer
 */
static int
buff_concat(size_t *pos, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int ret = vsnprintf(buff + *pos, FEATURE_MAXPRINT - *pos - 1, fmt, ap);
	va_end(ap);

	if (ret < 0) {
		ERR("vsprintf");
		return ret;
	}
	*pos += (size_t)ret;
	return 0;
}

/*
 * buff_concat_features -- (internal) concat features string to string buffer
 */
static int
buff_concat_features(size_t *pos, features_t features)
{
	return buff_concat(pos, "{compat 0x%x, incompat 0x%x, ro_compat 0x%x}",
			features.compat, features.incompat, features.ro_compat);
}


/*
 * poolset_close -- (internal) close pool set
 */
static void
poolset_close(struct pool_set *set)
{
	for (unsigned r = 0; r < set->nreplicas; ++r) {
		struct pool_replica *rep = REP(set, r);
		ASSERT(!rep->remote);
		for (unsigned p = 0; p < rep->nparts; ++p) {
			util_unmap_hdr(PART(rep, p));
		}
	}

	util_poolset_close(set, DO_NOT_DELETE_PARTS);
}

/* invalid features_t value */
#define FEATURES_INVALID \
	(features_t){UINT32_MAX, UINT32_MAX, UINT32_MAX}

/*
 * features_check -- (internal) check if features are correct
 */
static int
features_check(features_t *features, struct pool_hdr *hdrp)
{
	struct pool_hdr hdr;
	memcpy(&hdr, hdrp, sizeof(hdr));
	util_convert2h_hdr_nocheck(&hdr);

	if (util_feature_cmp(*features, FEATURES_INVALID) != 0) {
		if (util_feature_cmp(*features, hdr.features) != 0) {
			size_t pos = 0;
			if (!buff_concat_features(&pos, hdr.features))
				goto err;
			if (!buff_concat(&pos, "%s", " != "))
				goto err;
			if (!buff_concat_features(&pos, *features))
				goto err;
			fprintf(stderr, "features mismatch detected: %s\n",
					buff);
			return -1;
		}
	}

	features_t unknown = util_get_unknown_features(
			hdr.features,
			(features_t)POOL_FEAT_VALID);

	/* all features are known */
	if (util_feature_is_zero(unknown)) {
		memcpy(features, &hdr.features, sizeof(*features));
		return 0;
	}

	/* unknown features detected - print error message */
	size_t pos = 0;
	if (buff_concat_features(&pos, unknown))
		goto err;
	fprintf(stderr, "invalid features detected: %s\n", buff);
	return -1;

err:
	return -1;
}

/*
 * get_pool_open_flags -- (internal) generate pool open flags
 */
static inline unsigned
get_pool_open_flags(struct pool_set *set, int rdonly)
{
	unsigned flags = 0;
	if (rdonly == RDONLY && !util_pool_has_device_dax(set))
		flags = POOL_OPEN_COW;
	flags |= POOL_OPEN_IGNORE_BAD_BLOCKS;
	return flags;
}

/*
 * get_mmap_flags -- (internal) generate mmap flags
 */
static inline int
get_mmap_flags(struct pool_set_part *part, int rdonly)
{
	if (part->is_dev_dax)
		return MAP_SHARED;
	else
		return rdonly ? MAP_PRIVATE : MAP_SHARED;
}

/*
 * poolset_open -- (internal) open pool set
 */
static struct pool_set *
poolset_open(const char *path, int rdonly)
{
	struct pool_set *set;
	features_t features = FEATURES_INVALID;

	/* read poolset */
	int ret = util_poolset_create_set(&set, path, 0, 0, true);
	if (ret < 0) {
		ERR("cannot open pool set -- '%s'", path);
		goto err_poolset;
	}
	if (set->remote) {
		fprintf(stderr, "poolsets with remote replicas "
				"are not supported\n");
		errno = EINVAL;
		goto err_open;
	}

	/* open a memory pool */
	unsigned flags = get_pool_open_flags(set, rdonly);
	if (util_pool_open_nocheck(set, flags))
		goto err_open;

	/* map all headers and check features */
	for (unsigned r = 0; r < set->nreplicas; ++r) {
		struct pool_replica *rep = REP(set, r);
		ASSERT(!rep->remote);

		for (unsigned p = 0; p < rep->nparts; ++p) {
			struct pool_set_part *part = PART(rep, p);
			int mmap_flags = get_mmap_flags(part, rdonly);
			if (util_map_hdr(part, mmap_flags, rdonly)) {
				part->hdr = NULL;
				goto err_map_hdr;
			}

			if (features_check(&features, HDR(rep, p))) {
				ERR("invalid features - "
						"replica #%d part #%d",
						r, p);
				goto err_open;
			}
		}
	}
	return set;

err_map_hdr:
	/* unmap all headers */
	for (unsigned r = 0; r < set->nreplicas; ++r) {
		struct pool_replica *rep = REP(set, r);
		ASSERT(!rep->remote);
		for (unsigned p = 0; p < rep->nparts; ++p) {
			util_unmap_hdr(PART(rep, p));
		}
	}
err_open:
	/* close the memory pool and release pool set structure */
	util_poolset_close(set, DO_NOT_DELETE_PARTS);
err_poolset:
	return NULL;
}

/*
 * get_hdr -- (internal) read header in host byte order
 */
static struct pool_hdr *
get_hdr(struct pool_set *set, unsigned rep, unsigned part)
{
	static struct pool_hdr hdr;

	/* copy header */
	struct pool_hdr *hdrp = HDR(REP(set, rep), part);
	memcpy(&hdr, hdrp, sizeof(hdr));

	/* convert to host byte order and return */
	util_convert2h_hdr_nocheck(&hdr);
	return &hdr;
}

/*
 * set_hdr -- (internal) convert header to little-endian, checksum and write
 */
static void
set_hdr(struct pool_set *set, unsigned rep, unsigned part, struct pool_hdr *src)
{
	/* convert to little-endian and set new checksum */
	const size_t skip_off = POOL_HDR_CSUM_END_OFF(src);
	util_convert2le_hdr(src);
	util_checksum(src, sizeof(*src), &src->checksum, 1, skip_off);

	/* write header */
	struct pool_replica *replica = REP(set, rep);
	struct pool_hdr *dst = HDR(replica, part);
	memcpy(dst, src, sizeof(*src));
	util_persist_auto(PART(replica, part)->is_dev_dax, dst, sizeof(*src));
}

#define FEATURE_IS_ENABLED_STR	"feature already enabled: %s"
#define FEATURE_IS_DISABLED_STR	"feature already disabled: %s"

#define ENABLED		1
#define DISABLED	0

/*
 * require_feature_is_not -- check if required feature is enabled / disabled
 */
static int
require_feature_is_not(struct pool_set *set, features_t feature, int unwanted)
{
	struct pool_hdr *hdrp = get_hdr((set), 0, 0);
	int state = util_feature_is_set(hdrp->features, feature);
	if (state != unwanted)
		return 1;

	const char *msg = (state == ENABLED)
			? FEATURE_IS_ENABLED_STR : FEATURE_IS_DISABLED_STR;
	LOG(3, msg, util_feature2str(feature, NULL));
	return 1;
}

#define FEATURE_IS_NOT_ENABLED_PRIOR_STR	"enable %s prior to %s %s\n"
#define FEATURE_IS_NOT_DISABLED_PRIOR_STR	"disable %s prior to %s %s\n"
/*
 * require_feature_is_not -- (internal) check if other feature is enabled
 * (or disabled) in case the other feature has to be enabled (or disabled)
 * prior to the main one
 */
static int
require_other_feature_is(struct pool_set *set, features_t other, int wanted,
		features_t feature, const char *cause)
{
	struct pool_hdr *hdrp = get_hdr((set), 0, 0);
	int state = util_feature_is_set(hdrp->features, other);
	if (state == wanted)
		return 1;

	const char *msg = (wanted == ENABLED)
			? FEATURE_IS_NOT_ENABLED_PRIOR_STR
			: FEATURE_IS_NOT_DISABLED_PRIOR_STR;
	LOG(3, msg, util_feature2str(feature, NULL), cause);
	return 1;
}

#define FEATURE_ENABLE(flags, X) \
	(flags) |= (X)

#define FEATURE_DISABLE(flags, X) \
	(flags) &= ~(X)

/*
 * feature_enable -- (internal) enable feature
 */
static void
feature_enable(features_t *features, features_t new_feature)
{
	FEATURE_ENABLE(features->compat, new_feature.compat);
	FEATURE_ENABLE(features->incompat, new_feature.incompat);
	FEATURE_ENABLE(features->ro_compat, new_feature.ro_compat);
}

/*
 * feature_disable -- (internal) disable feature
 */
static void
feature_disable(features_t *features, features_t old_feature)
{
	FEATURE_DISABLE(features->compat, old_feature.compat);
	FEATURE_DISABLE(features->incompat, old_feature.incompat);
	FEATURE_DISABLE(features->ro_compat, old_feature.ro_compat);
}

/*
 * feature_set -- (internal) enable / disable feature
 */
static void
feature_set(struct pool_set *set, features_t feature, int value)
{
	for (unsigned r = 0; r < set->nreplicas; ++r) {
		for (unsigned p = 0; p < REP(set, r)->nparts; ++p) {
			struct pool_hdr *hdrp = get_hdr(set, r, p);
			if (value == ENABLED)
				feature_enable(&hdrp->features, feature);
			else
				feature_disable(&hdrp->features, feature);
			set_hdr(set, r, p, hdrp);
		}
	}
}

/*
 * query_feature -- (internal) query feature value
 */
static int
query_feature(const char *path, features_t feature)
{
	struct pool_set *set = poolset_open(path, RDONLY);
	if (!set)
		goto err_open;

	struct pool_hdr *hdrp = get_hdr(set, 0, 0);
	const int query = util_feature_is_set(hdrp->features, feature);

	poolset_close(set);

	return query;

err_open:
	return -1;
}

/*
 * unsupported_feature -- (internal) report unsupported feature
 */
static inline int
unsupported_feature(features_t feature)
{
	fprintf(stderr, "unsupported feature: %s\n",
			util_feature2str(feature, NULL));
	errno = EINVAL;
	return -1;
}

#define FEATURE_INCOMPAT(X) \
	(features_t)FEAT_INCOMPAT(X)

static const features_t f_singlehdr = FEATURE_INCOMPAT(SINGLEHDR);
static const features_t f_cksum_2k = FEATURE_INCOMPAT(CKSUM_2K);
static const features_t f_sds = FEATURE_INCOMPAT(SDS);

/*
 * enable_singlehdr -- (internal) enable POOL_FEAT_SINGLEHDR
 */
static int
enable_singlehdr(const char *path)
{
	return unsupported_feature(f_singlehdr);
}

/*
 * disable_singlehdr -- (internal) disable POOL_FEAT_SINGLEHDR
 */
static int
disable_singlehdr(const char *path)
{
	return unsupported_feature(f_singlehdr);
}

/*
 * query_singlehdr -- (internal) query POOL_FEAT_SINGLEHDR
 */
static int
query_singlehdr(const char *path)
{
	return query_feature(path, f_singlehdr);
}

/*
 * enable_checksum_2k -- (internal) enable POOL_FEAT_CKSUM_2K
 */
static int
enable_checksum_2k(const char *path)
{
	struct pool_set *set = poolset_open(path, RW);
	if (!set)
		return -1;
	if (require_feature_is_not(set, f_cksum_2k, ENABLED))
		feature_set(set, f_cksum_2k, ENABLED);

	poolset_close(set);
	return 0;
}

/*
 * disable_checksum_2k -- (internal) disable POOL_FEAT_CKSUM_2K
 */
static int
disable_checksum_2k(const char *path)
{
	struct pool_set *set = poolset_open(path, RW);
	if (!set)
		return -1;


	int ret = 0;
	if (!require_feature_is_not(set, f_cksum_2k, ENABLED))
		goto exit;

	/* disable POOL_FEAT_SDS prior to disabling POOL_FEAT_CKSUM_2K */
	if (!require_other_feature_is(set, f_sds, DISABLED,
			f_cksum_2k, "disabling")) {
		ret = -1;
		goto exit;
	}

	feature_set(set, f_cksum_2k, DISABLED);
exit:
	poolset_close(set);
	return ret;
}

/*
 * query_checksum_2k -- (internal) query POOL_FEAT_CKSUM_2K
 */
static int
query_checksum_2k(const char *path)
{
	return query_feature(path, f_cksum_2k);
}

/*
 * enable_shutdown_state -- (internal) enable POOL_FEAT_SDS
 */
static int
enable_shutdown_state(const char *path)
{
	struct pool_set *set = poolset_open(path, RW);
	if (!set)
		return -1;

	int ret = 0;
	if (!require_feature_is_not(set, f_sds, ENABLED))
		goto exit;

	/* enable POOL_FEAT_CKSUM_2K prior to enabling POOL_FEAT_SDS */
	if (!require_other_feature_is(set, f_cksum_2k, ENABLED,
			f_sds, "enabling")) {
		ret = -1;
		goto exit;
	}

	feature_set(set, f_sds, ENABLED);

exit:
	poolset_close(set);
	return ret;
}

/*
 * reset_shutdown_state -- zero all shutdown structures
 */
static void
reset_shutdown_state(struct pool_set *set)
{
	for (unsigned rep = 0; rep < set->nreplicas; ++rep) {
		for (unsigned part = 0; part < REP(set, rep)->nparts; ++part) {
			struct pool_hdr *hdrp = HDR(REP(set, rep), part);
			shutdown_state_init(&hdrp->sds, REP(set, rep));
		}
	}
}

/*
 * disable_shutdown_state -- (internal) disable POOL_FEAT_SDS
 */
static int
disable_shutdown_state(const char *path)
{
	struct pool_set *set = poolset_open(path, RW);
	if (!set)
		return -1;

	if (require_feature_is_not(set, f_sds, DISABLED)) {
		feature_set(set, f_sds, DISABLED);
		reset_shutdown_state(set);
	}

	poolset_close(set);
	return 0;
}

/*
 * query_shutdown_state -- (internal) query POOL_FEAT_SDS
 */
static int
query_shutdown_state(const char *path)
{
	return query_feature(path, f_sds);
}

struct feature_funcs {
	int (*enable)(const char *);
	int (*disable)(const char *);
	int (*query)(const char *);
};

static struct feature_funcs features[] = {
		{
			.enable = enable_singlehdr,
			.disable = disable_singlehdr,
			.query = query_singlehdr
		},
		{
			.enable = enable_checksum_2k,
			.disable = disable_checksum_2k,
			.query = query_checksum_2k
		},
		{
			.enable = enable_shutdown_state,
			.disable = disable_shutdown_state,
			.query = query_shutdown_state
		},
};

#define FEATURE_FUNCS_MAX ARRAY_SIZE(features)

/*
 * is_feature_valid -- (internal) check if feature is valid
 */
static inline int
is_feature_valid(uint32_t feature)
{
	if (feature >= FEATURE_FUNCS_MAX) {
		ERR("invalid feature: 0x%x", feature);
		errno = EINVAL;
		return 0;
	}
	return 1;
}

/*
 * pmempool_feature_enableU -- enable pool set feature
 */
#ifndef _WIN32
static inline
#endif
int
pmempool_feature_enableU(const char *path, enum pmempool_feature feature)
{
	LOG(3, "path %s, feature %x", path, feature);
	if (!is_feature_valid(feature))
		return -1;
	return features[feature].enable(path);
}

/*
 * pmempool_feature_disableU -- disable pool set feature
 */
#ifndef _WIN32
static inline
#endif
int
pmempool_feature_disableU(const char *path, enum pmempool_feature feature)
{
	LOG(3, "path %s, feature %x", path, feature);
	if (!is_feature_valid(feature))
		return -1;
	return features[feature].disable(path);
}

/*
 * pmempool_feature_queryU -- query pool set feature
 */
#ifndef _WIN32
static inline
#endif
int
pmempool_feature_queryU(const char *path, enum pmempool_feature feature)
{
	LOG(3, "path %s, feature %x", path, feature);

#define CHECK_INCOMPAT_MAPPING(FEAT, ENUM) \
	COMPILE_ERROR_ON( \
		util_feature2pmempool_feature(FEATURE_INCOMPAT(FEAT)) != ENUM)

	CHECK_INCOMPAT_MAPPING(SINGLEHDR, PMEMPOOL_FEAT_SINGLEHDR);
	CHECK_INCOMPAT_MAPPING(CKSUM_2K, PMEMPOOL_FEAT_CKSUM_2K);
	CHECK_INCOMPAT_MAPPING(SDS, PMEMPOOL_FEAT_SHUTDOWN_STATE);

#undef CHECK_INCOMPAT_MAPPING

	if (!is_feature_valid(feature))
		return -1;
	return features[feature].query(path);
}

#ifndef _WIN32
/*
 * pmempool_feature_enable -- enable pool set feature
 */
int
pmempool_feature_enable(const char *path, enum pmempool_feature feature)
{
	return pmempool_feature_enableU(path, feature);
}
#else
/*
 * pmempool_feature_enableW -- enable pool set feature as widechar
 */
int
pmempool_feature_enableW(const wchar_t *path, enum pmempool_feature feature)
{
	char *upath = util_toUTF8(path);
	if (upath == NULL) {
		ERR("Invalid poolest/pool file path.");
		return -1;
	}

	int ret = pmempool_feature_enableU(upath, feature);

	util_free_UTF8(upath);
	return ret;
}
#endif

#ifndef _WIN32
/*
 * pmempool_feature_disable -- disable pool set feature
 */
int
pmempool_feature_disable(const char *path, enum pmempool_feature feature)
{
	return pmempool_feature_disableU(path, feature);
}
#else
/*
 * pmempool_feature_disableW -- disable pool set feature as widechar
 */
int
pmempool_feature_disableW(const wchar_t *path, enum pmempool_feature feature)
{
	char *upath = util_toUTF8(path);
	if (upath == NULL) {
		ERR("Invalid poolest/pool file path.");
		return -1;
	}

	int ret = pmempool_feature_disableU(upath, feature);

	util_free_UTF8(upath);
	return ret;
}
#endif

#ifndef _WIN32
/*
 * pmempool_feature_query -- query pool set feature
 */
int
pmempool_feature_query(const char *path, enum pmempool_feature feature)
{
	return pmempool_feature_queryU(path, feature);
}
#else
/*
 * pmempool_feature_queryW -- query pool set feature as widechar
 */
int
pmempool_feature_queryW(const wchar_t *path, enum pmempool_feature feature)
{
	char *upath = util_toUTF8(path);
	if (upath == NULL) {
		ERR("Invalid poolest/pool file path.");
		return -1;
	}

	int ret = pmempool_feature_queryU(upath, feature);

	util_free_UTF8(upath);
	return ret;
}
#endif
