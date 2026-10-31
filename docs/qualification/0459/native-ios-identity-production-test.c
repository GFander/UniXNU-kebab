/*
 * Copyright (c) 2004-2020 Apple Inc. All rights reserved.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. The rights granted to you under the License
 * may not be used to create, or enable the creation or redistribution of,
 * unlawful or unlicensed copies of an Apple operating system, or to
 * circumvent, violate, or enable the circumvention or violation of, any
 * terms of an Apple operating system software license agreement.
 *
 * Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_END@
 */
/* Host qualification of unchanged production functions; credential carrier/libc are explicit models. */

#define _DEFAULT_SOURCE 1
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/types.h>
#include <arpa/inet.h>
#define CONFIG_EXT_RESOLVER 0
#define KAUTH_DEBUG(...) ((void)0)
#define NULLCRED_CHECK(c) assert((c)!=NULL&&(uintptr_t)(c)!=UINTPTR_MAX)
#ifndef __unused
#define __unused __attribute__((unused))
#endif
#define TRUE 1
#define FALSE 0
#define OSSwapHostToBigInt32(v) htonl(v)
typedef uint8_t uuid_t[16];
typedef struct {uint8_t g_guid[16];} guid_t;
typedef struct model_cred {uid_t cr_uid;int cr_ngroups;gid_t cr_groups[4];uid_t cr_gmuid;} *kauth_cred_t,*posix_cred_t;
static posix_cred_t posix_cred_get(kauth_cred_t cred){assert(cred);return cred;}
typedef int boolean_t;
typedef uint32_t kauth_ace_rights_t;
_Static_assert(sizeof(id_t)==4&&sizeof(guid_t)==16,"Native public identity widths");
#define KAUTH_UID_NONE  (~(uid_t)0 - 100)       /* not a valid UID */
#define KAUTH_WKG_NOT           0       /* not a well-known GUID */
#define KAUTH_WKG_OWNER         1
#define KAUTH_WKG_GROUP         2
#define KAUTH_WKG_NOBODY        3
#define KAUTH_WKG_EVERYBODY     4
#define KAUTH_RESULT_ALLOW      (1)
#define KAUTH_RESULT_DENY       (2)
#define KAUTH_RESULT_DEFER      (3)
#define KI_VALID_UID    (1<<0)          /* UID and GID are mutually exclusive */
#define KI_VALID_GID    (1<<1)
#define KI_VALID_GUID   (1<<2)
struct kauth_ace {
	guid_t          ace_applicable;
	u_int32_t       ace_flags;
#define KAUTH_ACE_KINDMASK              0xf
#define KAUTH_ACE_PERMIT                1
#define KAUTH_ACE_DENY                  2
#define KAUTH_ACE_AUDIT                 3       /* not implemented */
#define KAUTH_ACE_ALARM                 4       /* not implemented */
#define KAUTH_ACE_INHERITED             (1<<4)
#define KAUTH_ACE_FILE_INHERIT          (1<<5)
#define KAUTH_ACE_DIRECTORY_INHERIT     (1<<6)
#define KAUTH_ACE_LIMIT_INHERIT         (1<<7)
#define KAUTH_ACE_ONLY_INHERIT          (1<<8)
#define KAUTH_ACE_SUCCESS               (1<<9)  /* not implemented (AUDIT/ALARM) */
#define KAUTH_ACE_FAILURE               (1<<10) /* not implemented (AUDIT/ALARM) */
/* All flag bits controlling ACE inheritance */
#define KAUTH_ACE_INHERIT_CONTROL_FLAGS         \
	        (KAUTH_ACE_FILE_INHERIT |       \
	         KAUTH_ACE_DIRECTORY_INHERIT |  \
	         KAUTH_ACE_LIMIT_INHERIT |      \
	         KAUTH_ACE_ONLY_INHERIT)
	kauth_ace_rights_t ace_rights;          /* scope specific */
	/* These rights are never tested, but may be present in an ACL */
#define KAUTH_ACE_GENERIC_ALL           (1<<21)
#define KAUTH_ACE_GENERIC_EXECUTE       (1<<22)
#define KAUTH_ACE_GENERIC_WRITE         (1<<23)
#define KAUTH_ACE_GENERIC_READ          (1<<24)
};
typedef struct kauth_ace *kauth_ace_t;
struct kauth_acl_eval {
	kauth_ace_t             ae_acl;
	int                     ae_count;
	kauth_ace_rights_t      ae_requested;
	kauth_ace_rights_t      ae_residual;
	int                     ae_result;
	boolean_t               ae_found_deny;
	int                     ae_options;
#define KAUTH_AEVAL_IS_OWNER    (1<<0)          /* authorizing operation for owner */
#define KAUTH_AEVAL_IN_GROUP    (1<<1)          /* authorizing operation for groupmember */
#define KAUTH_AEVAL_IN_GROUP_UNKNOWN    (1<<2)          /* authorizing operation for unknown group membership */
	/* expansions for 'generic' rights bits */
	kauth_ace_rights_t      ae_exp_gall;
	kauth_ace_rights_t      ae_exp_gread;
	kauth_ace_rights_t      ae_exp_gwrite;
	kauth_ace_rights_t      ae_exp_gexec;
};
typedef struct kauth_acl_eval *kauth_acl_eval_t;
static __inline int
kauth_cred_cache_lookup(int from, int to, void *src, void *dst)
{
	/* NB: These must match the definitions used by Libinfo's mbr_identifier_translate(). */
	static const uuid_t _user_compat_prefix = {0xff, 0xff, 0xee, 0xee, 0xdd, 0xdd, 0xcc, 0xcc, 0xbb, 0xbb, 0xaa, 0xaa, 0x00, 0x00, 0x00, 0x00};
	static const uuid_t _group_compat_prefix = {0xab, 0xcd, 0xef, 0xab, 0xcd, 0xef, 0xab, 0xcd, 0xef, 0xab, 0xcd, 0xef, 0x00, 0x00, 0x00, 0x00};
#define COMPAT_PREFIX_LEN       (sizeof(uuid_t) - sizeof(id_t))

	assert(from != to);

	switch (from) {
	case KI_VALID_UID: {
		id_t uid = htonl(*(id_t *)src);

		if (to == KI_VALID_GUID) {
			uint8_t *uu = dst;
			memcpy(uu, _user_compat_prefix, sizeof(_user_compat_prefix));
			memcpy(&uu[COMPAT_PREFIX_LEN], &uid, sizeof(uid));
			return 0;
		}
		break;
	}
	case KI_VALID_GID: {
		id_t gid = htonl(*(id_t *)src);

		if (to == KI_VALID_GUID) {
			uint8_t *uu = dst;
			memcpy(uu, _group_compat_prefix, sizeof(_group_compat_prefix));
			memcpy(&uu[COMPAT_PREFIX_LEN], &gid, sizeof(gid));
			return 0;
		}
		break;
	}
	case KI_VALID_GUID: {
		const uint8_t *uu = src;

		if (to == KI_VALID_UID) {
			if (memcmp(uu, _user_compat_prefix, COMPAT_PREFIX_LEN) == 0) {
				id_t uid;
				memcpy(&uid, &uu[COMPAT_PREFIX_LEN], sizeof(uid));
				*(id_t *)dst = ntohl(uid);
				return 0;
			}
		} else if (to == KI_VALID_GID) {
			if (memcmp(uu, _group_compat_prefix, COMPAT_PREFIX_LEN) == 0) {
				id_t gid;
				memcpy(&gid, &uu[COMPAT_PREFIX_LEN], sizeof(gid));
				*(id_t *)dst = ntohl(gid);
				return 0;
			}
		}
		break;
	}
	default:
		/* NOT IMPLEMENTED */
		break;
	}
	return ENOENT;
}

uid_t
kauth_cred_getuid(kauth_cred_t cred)
{
	return posix_cred_get(cred)->cr_uid;
}

int
kauth_cred_uid2guid(uid_t uid, guid_t *guidp)
{
	return kauth_cred_cache_lookup(KI_VALID_UID, KI_VALID_GUID, &uid, guidp);
}

int
kauth_cred_gid2guid(gid_t gid, guid_t *guidp)
{
	return kauth_cred_cache_lookup(KI_VALID_GID, KI_VALID_GUID, &gid, guidp);
}

int
kauth_cred_guid2uid(guid_t *guidp, uid_t *uidp)
{
	return kauth_cred_cache_lookup(KI_VALID_GUID, KI_VALID_UID, guidp, uidp);
}

int
kauth_cred_guid2gid(guid_t *guidp, gid_t *gidp)
{
	return kauth_cred_cache_lookup(KI_VALID_GUID, KI_VALID_GID, guidp, gidp);
}

int
kauth_cred_getguid(kauth_cred_t cred, guid_t *guidp)
{
	NULLCRED_CHECK(cred);
	return kauth_cred_uid2guid(kauth_cred_getuid(cred), guidp);
}

int
kauth_guid_equal(guid_t *guid1, guid_t *guid2)
{
	return bcmp(guid1, guid2, sizeof(*guid1)) == 0;
}

int
kauth_wellknown_guid(guid_t *guid)
{
	static char     fingerprint[] = {0xab, 0xcd, 0xef, 0xab, 0xcd, 0xef, 0xab, 0xcd, 0xef, 0xab, 0xcd, 0xef};
	uint32_t                code;
	/*
	 * All WKGs begin with the same 12 bytes.
	 */
	if (bcmp((void *)guid, fingerprint, 12) == 0) {
		/*
		 * The final 4 bytes are our code (in network byte order).
		 */
		code = OSSwapHostToBigInt32(*(uint32_t *)&guid->g_guid[12]);
		switch (code) {
		case 0x0000000c:
			return KAUTH_WKG_EVERYBODY;
		case 0xfffffffe:
			return KAUTH_WKG_NOBODY;
		case 0x0000000a:
			return KAUTH_WKG_OWNER;
		case 0x00000010:
			return KAUTH_WKG_GROUP;
		}
	}
	return KAUTH_WKG_NOT;
}

int
kauth_cred_ismember_gid(kauth_cred_t cred, gid_t gid, int *resultp)
{
	posix_cred_t pcred = posix_cred_get(cred);
	int i;

	/*
	 * Check the per-credential list of override groups.
	 *
	 * We can conditionalise this on cred->cr_gmuid == KAUTH_UID_NONE since
	 * the cache should be used for that case.
	 */
	for (i = 0; i < pcred->cr_ngroups; i++) {
		if (gid == pcred->cr_groups[i]) {
			*resultp = 1;
			return 0;
		}
	}

	/*
	 * If we don't have a UID for group membership checks, the in-cred list
	 * was authoritative and we can stop here.
	 */
	if (pcred->cr_gmuid == KAUTH_UID_NONE) {
		*resultp = 0;
		return 0;
	}

#if CONFIG_EXT_RESOLVER
	struct kauth_group_membership *gm;
	struct kauth_identity_extlookup el;
	int error;

	/*
	 * If the resolver hasn't checked in yet, we are early in the boot
	 * phase and the local group list is complete and authoritative.
	 */
	if (!kauth_resolver_registered) {
		*resultp = 0;
		return 0;
	}

	/* TODO: */
	/* XXX check supplementary groups */
	/* XXX check whiteout groups */
	/* XXX nesting of supplementary/whiteout groups? */

	/*
	 * Check the group cache.
	 */
	KAUTH_GROUPS_LOCK();
	TAILQ_FOREACH(gm, &kauth_groups, gm_link) {
		if ((gm->gm_uid == pcred->cr_gmuid) && (gm->gm_gid == gid) && !kauth_groups_expired(gm)) {
			kauth_groups_lru(gm);
			break;
		}
	}

	/* did we find a membership entry? */
	if (gm != NULL) {
		*resultp = (gm->gm_flags & KAUTH_GROUP_ISMEMBER) ? 1 : 0;
	}
	KAUTH_GROUPS_UNLOCK();

	/* if we did, we can return now */
	if (gm != NULL) {
		DTRACE_PROC2(kauth__group__cache__hit, int, pcred->cr_gmuid, int, gid);
		return 0;
	}

	/* nothing in the cache, need to go to userland */
	bzero(&el, sizeof(el));
	el.el_info_pid = proc_getpid(current_proc());
	el.el_flags = KAUTH_EXTLOOKUP_VALID_UID | KAUTH_EXTLOOKUP_VALID_GID | KAUTH_EXTLOOKUP_WANT_MEMBERSHIP;
	el.el_uid = pcred->cr_gmuid;
	el.el_gid = gid;
	el.el_member_valid = 0;         /* XXX set by resolver? */

	DTRACE_PROC2(kauth__group__resolver__submitted, int, el.el_uid, int, el.el_gid);

	error = kauth_resolver_submit(&el, 0ULL);

	DTRACE_PROC2(kauth__group__resolver__returned, int, error, int, el.el_flags);

	if (error != 0) {
		return error;
	}
	/* save the results from the lookup */
	kauth_groups_updatecache(&el);

	/* if we successfully ascertained membership, report */
	if (el.el_flags & KAUTH_EXTLOOKUP_VALID_MEMBERSHIP) {
		*resultp = (el.el_flags & KAUTH_EXTLOOKUP_ISMEMBER) ? 1 : 0;
		return 0;
	}

	return ENOENT;
#else
	*resultp = 0;
	return 0;
#endif
}

int
kauth_cred_ismember_guid(__unused kauth_cred_t cred, guid_t *guidp, int *resultp)
{
	int error = 0;

	switch (kauth_wellknown_guid(guidp)) {
	case KAUTH_WKG_NOBODY:
		*resultp = 0;
		break;
	case KAUTH_WKG_EVERYBODY:
		*resultp = 1;
		break;
	default:
	{
		gid_t gid;
#if CONFIG_EXT_RESOLVER
		struct kauth_identity ki;

		/*
		 * Grovel the identity cache looking for this GUID.
		 * If we find it, and it is for a user record, return
		 * false because it's not a group.
		 *
		 * This is necessary because we don't have -ve caching
		 * of group memberships, and we really want to avoid
		 * calling out to the resolver if at all possible.
		 *
		 * Because we're called by the ACL evaluator, and the
		 * ACL evaluator is likely to encounter ACEs for users,
		 * this is expected to be a common case.
		 */
		ki.ki_valid = 0;
		if ((error = kauth_identity_find_guid(guidp, &ki, NULL)) == 0 &&
		    !kauth_identity_guid_expired(&ki)) {
			if (ki.ki_valid & KI_VALID_GID) {
				/* It's a group after all... */
				gid = ki.ki_gid;
				goto do_check;
			}
			if (ki.ki_valid & KI_VALID_UID) {
				*resultp = 0;
				return 0;
			}
		}
#endif /* CONFIG_EXT_RESOLVER */
		/*
		 * Attempt to translate the GUID to a GID.  Even if
		 * this fails, we will have primed the cache if it is
		 * a user record and we'll see it above the next time
		 * we're asked.
		 */
		if ((error = kauth_cred_guid2gid(guidp, &gid)) != 0) {
			/*
			 * If we have no guid -> gid translation, it's not a group and
			 * thus the cred can't be a member.
			 */
			if (error == ENOENT) {
				*resultp = 0;
				error = 0;
			}
		} else {
#if CONFIG_EXT_RESOLVER
do_check:
#endif /* CONFIG_EXT_RESOLVER */
			error = kauth_cred_ismember_gid(cred, gid, resultp);
		}
	}
	break;
	}
	return error;
}

int
kauth_acl_evaluate(kauth_cred_t cred, kauth_acl_eval_t eval)
{
	int applies, error, i, gotguid;
	kauth_ace_t ace;
	guid_t guid;
	uint32_t rights;
	int wkguid;

	if (cred == NULL) {
		KAUTH_DEBUG("    ACL - got NULL credential");
		return EINVAL;
	}

	if (eval == NULL) {
		KAUTH_DEBUG("    ACL - got NULL ACL evaluator");
		return EINVAL;
	}

	/* always allowed to do nothing */
	if (eval->ae_requested == 0) {
		eval->ae_result = KAUTH_RESULT_ALLOW;
		return 0;
	}

	eval->ae_residual = eval->ae_requested;
	eval->ae_found_deny = FALSE;

	/*
	 * Get our guid for comparison purposes.
	 */
	if ((error = kauth_cred_getguid(cred, &guid)) != 0) {
		KAUTH_DEBUG("    ACL - can't get credential GUID (%d)", error);
		error = 0;
		gotguid = 0;
	} else {
		gotguid = 1;
	}

	KAUTH_DEBUG("    ACL - %d entries, initial residual %x", eval->ae_count, eval->ae_residual);
	for (i = 0, ace = eval->ae_acl; i < eval->ae_count; i++, ace++) {
		/*
		 * Skip inherit-only entries.
		 */
		if (ace->ace_flags & KAUTH_ACE_ONLY_INHERIT) {
			continue;
		}

		/*
		 * Expand generic rights, if appropriate.
		 */
		rights = ace->ace_rights;
		if (rights & KAUTH_ACE_GENERIC_ALL) {
			rights |= eval->ae_exp_gall;
		}
		if (rights & KAUTH_ACE_GENERIC_READ) {
			rights |= eval->ae_exp_gread;
		}
		if (rights & KAUTH_ACE_GENERIC_WRITE) {
			rights |= eval->ae_exp_gwrite;
		}
		if (rights & KAUTH_ACE_GENERIC_EXECUTE) {
			rights |= eval->ae_exp_gexec;
		}

		/*
		 * Determine whether this entry applies to the current request.  This
		 * saves us checking the GUID if the entry has nothing to do with what
		 * we're currently doing.
		 */
		switch (ace->ace_flags & KAUTH_ACE_KINDMASK) {
		case KAUTH_ACE_PERMIT:
			if (!(eval->ae_residual & rights)) {
				continue;
			}
			break;
		case KAUTH_ACE_DENY:
			if (!(eval->ae_requested & rights)) {
				continue;
			}
			eval->ae_found_deny = TRUE;
			break;
		default:
			/* we don't recognise this ACE, skip it */
			continue;
		}

		/*
		 * Verify whether this entry applies to the credential.
		 */
		wkguid = kauth_wellknown_guid(&ace->ace_applicable);
		switch (wkguid) {
		case KAUTH_WKG_OWNER:
			applies = eval->ae_options & KAUTH_AEVAL_IS_OWNER;
			break;
		case KAUTH_WKG_GROUP:
			if (!gotguid || (eval->ae_options & KAUTH_AEVAL_IN_GROUP_UNKNOWN)) {
				applies = ((ace->ace_flags & KAUTH_ACE_KINDMASK) == KAUTH_ACE_DENY);
			} else {
				applies = eval->ae_options & KAUTH_AEVAL_IN_GROUP;
			}
			break;
		/* we short-circuit these here rather than wasting time calling the group membership code */
		case KAUTH_WKG_EVERYBODY:
			applies = 1;
			break;
		case KAUTH_WKG_NOBODY:
			applies = 0;
			break;

		default:
			/* check to see whether it's exactly us, or a group we are a member of */
			applies = !gotguid ? 0 : kauth_guid_equal(&guid, &ace->ace_applicable);
			KAUTH_DEBUG("    ACL - ACE applicable " K_UUID_FMT " caller " K_UUID_FMT " %smatched",
			    K_UUID_ARG(ace->ace_applicable), K_UUID_ARG(guid), applies ? "" : "not ");

			if (!applies) {
				error = !gotguid ? ENOENT : kauth_cred_ismember_guid(cred, &ace->ace_applicable, &applies);
				/*
				 * If we can't resolve group membership, we have to limit misbehaviour.
				 * If the ACE is an 'allow' ACE, assume the cred is not a member (avoid
				 * granting excess access).  If the ACE is a 'deny' ACE, assume the cred
				 * is a member (avoid failing to deny).
				 */
				if (error != 0) {
					KAUTH_DEBUG("    ACL[%d] - can't get membership, making pessimistic assumption", i);
					switch (ace->ace_flags & KAUTH_ACE_KINDMASK) {
					case KAUTH_ACE_PERMIT:
						applies = 0;
						break;
					case KAUTH_ACE_DENY:
						applies = 1;
						break;
					}
				} else {
					KAUTH_DEBUG("    ACL - %s group member", applies ? "is" : "not");
				}
			} else {
				KAUTH_DEBUG("    ACL - entry matches caller");
			}
		}
		if (!applies) {
			continue;
		}

		/*
		 * Apply ACE to outstanding rights.
		 */
		switch (ace->ace_flags & KAUTH_ACE_KINDMASK) {
		case KAUTH_ACE_PERMIT:
			/* satisfy any rights that this ACE grants */
			eval->ae_residual = eval->ae_residual & ~rights;
			KAUTH_DEBUG("    ACL[%d] - rights %x leave residual %x", i, rights, eval->ae_residual);
			/* all rights satisfied? */
			if (eval->ae_residual == 0) {
				eval->ae_result = KAUTH_RESULT_ALLOW;
				return 0;
			}
			break;
		case KAUTH_ACE_DENY:
			/* deny the request if any of the requested rights is denied */
			if (eval->ae_requested & rights) {
				KAUTH_DEBUG("    ACL[%d] - denying based on %x", i, rights);
				eval->ae_result = KAUTH_RESULT_DENY;
				return 0;
			}
			break;
		default:
			KAUTH_DEBUG("    ACL - unknown entry kind %d", ace->ace_flags & KAUTH_ACE_KINDMASK);
			break;
		}
	}
	/* if not permitted, defer to other modes of authorisation */
	eval->ae_result = KAUTH_RESULT_DEFER;
	return 0;
}

static unsigned cases;
static guid_t user_guid(uid_t uid){guid_t value;assert(!kauth_cred_uid2guid(uid,&value));return value;}
static guid_t group_guid(gid_t gid){guid_t value;assert(!kauth_cred_gid2guid(gid,&value));return value;}
static void one(kauth_cred_t cred,guid_t target,unsigned flags,unsigned rights,unsigned requested,unsigned options,int expected){
 struct kauth_ace entry={target,flags,rights};struct kauth_acl_eval eval={0};eval.ae_acl=&entry;eval.ae_count=1;eval.ae_requested=requested;eval.ae_options=options;eval.ae_exp_gall=15;eval.ae_exp_gread=1;eval.ae_exp_gwrite=2;eval.ae_exp_gexec=4;
 assert(!kauth_acl_evaluate(cred,&eval));assert(eval.ae_result==expected);cases++;
}
int main(void){
 static const uint8_t users[12]={0xff,0xff,0xee,0xee,0xdd,0xdd,0xcc,0xcc,0xbb,0xbb,0xaa,0xaa};
 static const uint8_t groups[12]={0xab,0xcd,0xef,0xab,0xcd,0xef,0xab,0xcd,0xef,0xab,0xcd,0xef};
 uint32_t special[]={0,1,20,24,64,65,89,250,501,502,1000,0x7fffffff,0x80000000,0xfffffffe,0xffffffff};
 for(unsigned index=0;index<1039;index++){
  uint32_t id=index<15?special[index]:(index*2654435761u)^0x87961413u;
  for(unsigned kind=0;kind<2;kind++){
   _Alignas(16) uint8_t bytes[48];memset(bytes,0xa7,sizeof(bytes));guid_t *value=(guid_t *)(bytes+16);uint32_t network=htonl(id);uid_t back=0x91827364;
   assert(!(kind?kauth_cred_gid2guid(id,value):kauth_cred_uid2guid(id,value)));
   assert(!memcmp(value,kind?groups:users,12)&&!memcmp(value->g_guid+12,&network,4));
   for(unsigned i=0;i<16;i++)assert(bytes[i]==0xa7&&bytes[32+i]==0xa7);
   assert(!(kind?kauth_cred_guid2gid(value,&back):kauth_cred_guid2uid(value,&back))&&back==id);
   back=0x91827364;assert((kind?kauth_cred_guid2uid(value,&back):kauth_cred_guid2gid(value,&back))==ENOENT&&back==0x91827364);
   cases+=3;
  }
 }
 guid_t unknown={{0}};uid_t unchanged=77;assert(kauth_cred_guid2uid(&unknown,&unchanged)==ENOENT&&unchanged==77);cases++;
 for(unsigned byte=0;byte<12;byte++)for(unsigned kind=0;kind<2;kind++){
  _Alignas(16) guid_t value=kind?group_guid(501):user_guid(501);value.g_guid[byte]^=0x80;unchanged=77;
  assert(kauth_cred_guid2uid(&value,&unchanged)==ENOENT&&unchanged==77);assert(kauth_cred_guid2gid(&value,&unchanged)==ENOENT&&unchanged==77);cases+=2;
 }
 uint8_t source[16]={0},destination[16],before[16];memset(before,0x91,sizeof(before));
 int pairs[][2]={{KI_VALID_UID,KI_VALID_GID},{KI_VALID_GID,KI_VALID_UID},{8,KI_VALID_UID},{KI_VALID_UID,8},{8,KI_VALID_GUID}};
 for(unsigned i=0;i<sizeof(pairs)/sizeof(pairs[0]);i++){memcpy(destination,before,16);assert(kauth_cred_cache_lookup(pairs[i][0],pairs[i][1],source,destination)==ENOENT&&!memcmp(destination,before,16));cases++;}
 struct model_cred cred={501,2,{501,20,0,0},501};
 gid_t gids[]={0,20,501,502,9999};
 for(unsigned i=0;i<5;i++){
  int member=-1;assert(!kauth_cred_ismember_gid(&cred,gids[i],&member)&&member==(gids[i]==20||gids[i]==501));cases++;
  guid_t guid=group_guid(gids[i]);member=-1;assert(!kauth_cred_ismember_guid(&cred,&guid,&member)&&member==(gids[i]==20||gids[i]==501));cases++;
 }
 int member=99;assert(!kauth_cred_ismember_guid(&cred,&unknown,&member)&&member==0);cases++;
 // Real unchanged ACL evaluator: exact user/group/owner/everybody/nobody,
 // generic rights, inherit-only, order and unknown-group pessimistic deny.
 guid_t me=user_guid(501),other=user_guid(502),in_group=group_guid(20),out_group=group_guid(502);
 guid_t everyone=group_guid(12),nobody=group_guid(0xfffffffe),owner=group_guid(10),own_group=group_guid(16);
 for(unsigned requested=1;requested<16;requested++)for(unsigned rights=1;rights<16;rights++){
  int all=(requested&rights)==requested;int overlap=(requested&rights)!=0;
  one(&cred,me,KAUTH_ACE_PERMIT,rights,requested,0,all?KAUTH_RESULT_ALLOW:KAUTH_RESULT_DEFER);
  one(&cred,me,KAUTH_ACE_DENY,rights,requested,0,overlap?KAUTH_RESULT_DENY:KAUTH_RESULT_DEFER);
  one(&cred,other,KAUTH_ACE_PERMIT,rights,requested,0,KAUTH_RESULT_DEFER);
  one(&cred,other,KAUTH_ACE_DENY,rights,requested,0,KAUTH_RESULT_DEFER);
  one(&cred,in_group,KAUTH_ACE_PERMIT,rights,requested,0,all?KAUTH_RESULT_ALLOW:KAUTH_RESULT_DEFER);
  one(&cred,in_group,KAUTH_ACE_DENY,rights,requested,0,overlap?KAUTH_RESULT_DENY:KAUTH_RESULT_DEFER);
  one(&cred,out_group,KAUTH_ACE_PERMIT,rights,requested,0,KAUTH_RESULT_DEFER);
  one(&cred,everyone,KAUTH_ACE_DENY,rights,requested,0,overlap?KAUTH_RESULT_DENY:KAUTH_RESULT_DEFER);
  one(&cred,everyone,KAUTH_ACE_PERMIT,rights,requested,0,all?KAUTH_RESULT_ALLOW:KAUTH_RESULT_DEFER);
  one(&cred,nobody,KAUTH_ACE_PERMIT,rights,requested,0,KAUTH_RESULT_DEFER);
  one(&cred,unknown,KAUTH_ACE_PERMIT,rights,requested,0,KAUTH_RESULT_DEFER);
  one(&cred,owner,KAUTH_ACE_PERMIT,rights,requested,KAUTH_AEVAL_IS_OWNER,all?KAUTH_RESULT_ALLOW:KAUTH_RESULT_DEFER);
  one(&cred,owner,KAUTH_ACE_PERMIT,rights,requested,0,KAUTH_RESULT_DEFER);
  one(&cred,own_group,KAUTH_ACE_PERMIT,rights,requested,KAUTH_AEVAL_IN_GROUP,all?KAUTH_RESULT_ALLOW:KAUTH_RESULT_DEFER);
  one(&cred,own_group,KAUTH_ACE_PERMIT,rights,requested,KAUTH_AEVAL_IN_GROUP_UNKNOWN,KAUTH_RESULT_DEFER);
  one(&cred,own_group,KAUTH_ACE_DENY,rights,requested,KAUTH_AEVAL_IN_GROUP_UNKNOWN,overlap?KAUTH_RESULT_DENY:KAUTH_RESULT_DEFER);
  one(&cred,everyone,KAUTH_ACE_DENY|KAUTH_ACE_ONLY_INHERIT,rights,requested,0,KAUTH_RESULT_DEFER);
  one(&cred,everyone,KAUTH_ACE_AUDIT,rights,requested,0,KAUTH_RESULT_DEFER);
 }
 unsigned generic[]={KAUTH_ACE_GENERIC_ALL,KAUTH_ACE_GENERIC_READ,KAUTH_ACE_GENERIC_WRITE,KAUTH_ACE_GENERIC_EXECUTE},expanded[]={15,1,2,4};
 for(unsigned g=0;g<4;g++)for(unsigned requested=1;requested<16;requested++){
  one(&cred,me,KAUTH_ACE_PERMIT,generic[g],requested,0,(requested&expanded[g])==requested?KAUTH_RESULT_ALLOW:KAUTH_RESULT_DEFER);
  one(&cred,me,KAUTH_ACE_DENY,generic[g],requested,0,(requested&expanded[g])?KAUTH_RESULT_DENY:KAUTH_RESULT_DEFER);
 }
 struct kauth_ace ordered[2]={{me,KAUTH_ACE_DENY,1},{me,KAUTH_ACE_PERMIT,1}};struct kauth_acl_eval eval={0};eval.ae_acl=ordered;eval.ae_count=2;eval.ae_requested=1;assert(!kauth_acl_evaluate(&cred,&eval)&&eval.ae_result==KAUTH_RESULT_DENY);cases++;
 ordered[0].ace_flags=KAUTH_ACE_PERMIT;ordered[1].ace_flags=KAUTH_ACE_DENY;assert(!kauth_acl_evaluate(&cred,&eval)&&eval.ae_result==KAUTH_RESULT_ALLOW);cases++;
 one(&cred,unknown,KAUTH_ACE_DENY,1,0,0,KAUTH_RESULT_ALLOW);
 assert(kauth_acl_evaluate(NULL,&eval)==EINVAL&&kauth_acl_evaluate(&cred,NULL)==EINVAL);cases+=2;
 cred.cr_gmuid=KAUTH_UID_NONE;member=99;assert(!kauth_cred_ismember_gid(&cred,9999,&member)&&!member);cases++;
 printf("PASS %u actual Apple no-resolver identity/group/ACL cases: exact and generic allow/deny/defer, strict unknown translations, ACL ordering/inheritance, well-known and POSIX groups; only credential carrier/libc modeled, no authority/result stubs\n",cases);
}
