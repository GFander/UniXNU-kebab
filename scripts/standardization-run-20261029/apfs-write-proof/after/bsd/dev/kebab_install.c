/*
 * KEBAB INST: install a userland stream onto the writable root volume.
 *
 * The kernel has UFS block access, a read-write HFS+ root and a USB CDC-ACM
 * console whose bulk-OUT endpoint already carries console commands.  What it
 * has no path for is 3.3 GB of userland: the payload volume is 64 MiB and the
 * host has no block-level route to the HFS+ volume, so the bytes have to walk
 * over the console.
 *
 * This file is the receiving half.  It parses the KIST stream that
 * scripts/pack-userland.py writes -- a fixed 32-byte stream header followed by
 * fixed 192-byte entry headers and payloads -- and turns each entry into a
 * file, directory or symlink on the root volume:
 *
 *   - the stream is consumed strictly forward: no seeking, no file length in
 *     advance, one entry at a time;
 *   - directories are created as needed, files are created with the mode in
 *     the entry header, and v2 DIR/LINK entries create a directory with the
 *     exact mode (01777 included) or a symlink with the target in the payload;
 *   - every created inode is explicitly chowned to uid 0/gid 0 and its
 *     resulting uid/gid/mode is read back and printed;
 *   - every file entry is checked twice: the payload is hashed as it arrives
 *     (the transport check) and the file is read back and hashed after the
 *     write (the storage check);
 *   - an entry whose destination already has the right size and hash is
 *     reported as SKIP and its payload is discarded;
 *   - INST DRY runs the whole parser, hashes everything and prints the plan,
 *     but creates no directory and writes no byte;
 *   - a failed write prints, stops the stream and leaves what was written.
 *     Nothing here panics, and nothing here trusts a length from the wire.
 *
 * The console side (bsd/dev/kebab_usb.c) owns the DMA buffer and the ACK line;
 * this file owns the state machine and never touches the controller.  It runs
 * on the USB event thread with no transport lock held, because vnode I/O can
 * sleep and the console must keep transmitting while a chunk is written.
 *
 * Gating: the whole command family is behind the boot argument
 * kebab_install=1 (default off), so an ordinary boot cannot write anything.
 */
#if defined(KEBAB)

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/kauth.h>
#include <sys/kernel.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/file_internal.h>
#include <sys/proc.h>
#include <sys/uio.h>
#include <sys/ubc.h>
#include <sys/vnode.h>
#include <sys/vnode_internal.h>
#include <kern/clock.h>
#include <kern/debug.h>
#include <mach/mach_time.h>
#include <libkern/libkern.h>
#include <libkern/crypto/sha2.h>
#include <libkern/zlib.h>
#include <pexpert/pexpert.h>

#include "kebab_install.h"
#include "kebab_ufs.h"

#define KI_STREAM_HDR     32u
#define KI_ENTRY_HDR      192u
#define KI_ENTRY_MAGIC    0x544e454bu   /* 'KENT' */
#define KI_STREAM_MAGIC   0x5453494bu   /* 'KIST' */
#define KI_VERSION        2u
#define KI_VERSION_MIN    1u
#define KI_F_DEFLATE      0x0001u
#define KI_F_DIR          0x0002u
#define KI_F_SYMLINK      0x0004u
#define KI_F_TYPE         0x0006u
#define KI_SF_DEFLATE     0x0001u
#define KI_SF_SKELETON    0x0002u
#define KI_MODE_MASK      07777u
#define KI_PATH_FIELD     128u
#define KI_IO_BUF         65536u        /* inflate output and read-back block */
#define KI_TIMEOUT_NS     60000000000ull /* 60 s with no chunk */
#define KI_TAIL_LEN       4096u         /* cold-read tail sample per file */
#define KI_COLD_FULL_MAX  (4u * 1024u * 1024u) /* full cold hash up to 4 MiB */
#define KI_SAMPLE_MAX     4u
/* HFS+ volume header, read from the device after a flush. */
#define KI_VH_OFF         1024u
#define KI_VH_SIZE        512u
#define KI_VH_BSIZE_OFF   40u
#define KI_VH_BLOCKS_OFF  44u
#define KI_VH_FREE_OFF    48u
#define KI_HDR_PATH_OFF   60u
#define KI_HDR_SHA_OFF    28u
#define KI_HDR_USIZE_OFF  20u
#define KI_HDR_WIRE_OFF   12u
#define KI_HDR_FLAGS_OFF  8u
#define KI_HDR_MODE_OFF   6u
#define KI_HDR_PLEN_OFF   4u

/* One entry's destination must start with one of these.  Everything else is
 * refused before a directory is created or a file is opened. */
static const char *const ki_allow[] = {
	"/sbin/",
	"/usr/lib/",
	"/usr/libexec/",
	"/usr/standalone/",
	"/System/Library/Caches/com.apple.dyld/",
	"/System/Library/Caches/com.apple.kernelcaches/",
	"/System/Library/LaunchDaemons/",
};

/*
 * A skeleton stream may only write the exact paths below.  This table is
 * mirrored by scripts/make-minimal-userland.py's SKELETON_* tables; the host
 * verifier checks the generated stream against the same list.
 */
static const char *const ki_skel_allow[] = {
	"/dev",
	"/private",
	"/private/etc",
	"/private/etc/passwd",
	"/private/etc/master.passwd",
	"/private/etc/group",
	"/private/tmp",
	"/private/var",
	"/private/var/db",
	"/private/var/log",
	"/private/var/root",
	"/private/var/run",
	"/private/var/tmp",
	"/etc",
	"/tmp",
	"/var",
};

enum ki_state {
	KI_S_STREAM_HDR = 0,
	KI_S_ENTRY_HDR,
	KI_S_PAYLOAD,
	KI_S_PAD,
	KI_S_DONE,
};

struct ki_entry {
	char        path[KEBAB_INST_PATH_MAX];
	char        link[KEBAB_INST_PATH_MAX];  /* symlink target, v2 only */
	uint16_t    mode;
	uint16_t    flags;
	uint64_t    wire;          /* payload bytes on the wire */
	uint64_t    size;          /* bytes the file must have */
	uint64_t    old_size;      /* size before this write, 0 if new */
	uint8_t     sha[32];       /* hash of the uncompressed content */
	uint64_t    wire_left;
	uint64_t    got;           /* uncompressed bytes produced so far */
	uint64_t    offset;        /* write offset */
	uint8_t     tail[KI_TAIL_LEN]; /* last bytes written, for the cold check */
	uint32_t    tail_len;
	bool        skip;
	bool        deflate;
	bool        is_dir;
	bool        is_link;
	bool        opened;
	vnode_t     vp;
	SHA256_CTX  shactx;
	bool        shactx_live;
	z_stream    zs;
	bool        zs_live;
	bool        z_end;
};

/*
 * A stream completion re-reads a few destinations after dropping their UBC
 * pages.  The full digest is checked for small files; a large file is checked
 * by size plus the digest of its last KI_TAIL_LEN bytes.  A truncation loses
 * the size and the tail, which is the failure this guards against.
 */
struct ki_sample {
	char        path[KEBAB_INST_PATH_MAX];
	uint64_t    size;
	uint8_t     sha[32];
	uint8_t     tail_sha[32];
	uint32_t    tail_len;
	bool        set;
};

/* Slot order: first file, largest file, middle entry, last file. */
enum {
	KI_SAMPLE_FIRST = 0,
	KI_SAMPLE_LARGEST,
	KI_SAMPLE_MIDDLE,
	KI_SAMPLE_LAST,
};

static struct {
	bool        gate_checked;
	bool        gated;
	bool        active;
	bool        dry;
	bool        skel;          /* v2 stream flag: skeleton allowlist */
	uint16_t    version;
	uint16_t    stream_flags;
	uint32_t    state;
	uint32_t    hdr_got;
	uint8_t     hdr[KI_ENTRY_HDR];

	uint64_t    wire_total;
	uint64_t    wire_seen;
	uint64_t    wire_left;
	uint64_t    usize_total;   /* what the stream header declares */
	uint32_t    entries_total;
	uint32_t    entries_done;
	uint32_t    ok;
	uint32_t    skipped;
	uint32_t    failed;

	uint64_t    bytes_written;  /* regular-file bytes written this stream */
	uint64_t    bytes_replaced; /* old bytes those writes replaced */
	uint32_t    free_before;    /* volume-header free blocks at INST BEGIN */
	uint32_t    vh_bsize;
	bool        free_before_valid;

	uint64_t    ns_start;
	uint64_t    ns_last;

	struct ki_sample samples[KI_SAMPLE_MAX];

	uint8_t    *io;            /* KI_IO_BUF, allocated per session */
	uint32_t    session;
	struct ki_entry e;
} ki;

/* ------------------------------------------------------------- utilities */

static uint16_t
ki_le16(const uint8_t *p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t
ki_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t
ki_le64(const uint8_t *p)
{
	return (uint64_t)ki_le32(p) | ((uint64_t)ki_le32(p + 4) << 32);
}

static uint16_t
ki_be16(const uint8_t *p)
{
	return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t
ki_be32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	    ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint32_t
ki_hex08(char *out, uint8_t v)
{
	static const char digits[] = "0123456789abcdef";

	out[0] = digits[v >> 4];
	out[1] = digits[v & 15];
	return 2;
}

static uint64_t
ki_now_ns(void)
{
	uint64_t ns = 0;

	absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
	return ns;
}

/* --------------------------------------------------------------- gating */

bool
kebab_install_gated(void)
{
	uint32_t on = 0;

	if (!ki.gate_checked) {
		ki.gate_checked = true;
		ki.gated = PE_parse_boot_argn("kebab_install", &on, sizeof(on)) && on != 0;
	}
	return ki.gated;
}

static bool
ki_gate(void)
{
	if (!kebab_install_gated()) {
		printf("INST disabled: boot with kebab_install=1 to enable the "
		    "userland installer\n");
		return false;
	}
	return true;
}

/* ------------------------------------------------------------ path rules */

/*
 * A destination is accepted only when it is absolute, short enough for the
 * header's fixed field, printable and has no empty or ".." component.  A
 * normal stream then has to match one of the allowlist prefixes; a v2
 * skeleton stream has to match one of the exact skeleton paths.  The check is
 * done on the header's bounded field, never with strlen() on wire data.
 */
static bool
ki_path_ok(const char *path, uint32_t len, bool skel)
{
	uint32_t i, start;

	if (len < 3 || len >= KEBAB_INST_PATH_MAX) {
		return false;
	}
	if (path[0] != '/') {
		return false;
	}
	for (i = 0; i < len; i++) {
		char c = path[i];
		if (c == 0 || c == '\n' || c == '\r' || c == '\t') {
			return false;
		}
		if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7e) {
			return false;
		}
		if (c == '/' && (i + 1 == len || path[i + 1] == '/')) {
			return false;   /* trailing or doubled slash */
		}
	}
	/* components of "." are harmless; ".." is never allowed */
	start = 1;
	for (i = 1; i <= len; i++) {
		if (i == len || path[i] == '/') {
			uint32_t n = i - start;
			if (n == 2 && path[start] == '.' && path[start + 1] == '.') {
				return false;
			}
			start = i + 1;
		}
	}
	if (skel) {
		for (i = 0; i < sizeof(ki_skel_allow) /
		    sizeof(ki_skel_allow[0]); i++) {
			size_t slen = strlen(ki_skel_allow[i]);
			if ((size_t)len == slen &&
			    memcmp(path, ki_skel_allow[i], slen) == 0) {
				return true;
			}
		}
		return false;
	}
	for (i = 0; i < sizeof(ki_allow) / sizeof(ki_allow[0]); i++) {
		size_t plen = strlen(ki_allow[i]);
		if ((size_t)len >= plen && memcmp(path, ki_allow[i], plen) == 0) {
			return true;
		}
	}
	return false;
}

/* INST CHECK may name either family: it only reads. */
static bool
ki_path_ok_any(const char *path, uint32_t len)
{
	return ki_path_ok(path, len, true) || ki_path_ok(path, len, false);
}

/* --------------------------------------------------------- vnode helpers */

/*
 * Force uid 0/gid 0 and the exact permission value.  HFS+ applies the
 * process umask and may inherit the parent's gid at create time, so every
 * inode the installer creates goes through this before it is reported.
 */
static int
ki_set_attrs(vnode_t vp, uint16_t mode, vfs_context_t ctx)
{
	struct vnode_attr va;

	VATTR_INIT(&va);
	VATTR_SET(&va, va_uid, 0);
	VATTR_SET(&va, va_gid, 0);
	VATTR_SET(&va, va_mode, mode & KI_MODE_MASK);
	return vnode_setattr(vp, &va, ctx);
}

/* A symlink's mode is fixed at creation; only the ownership is forced. */
static int
ki_set_owner(vnode_t vp, vfs_context_t ctx)
{
	struct vnode_attr va;

	VATTR_INIT(&va);
	VATTR_SET(&va, va_uid, 0);
	VATTR_SET(&va, va_gid, 0);
	return vnode_setattr(vp, &va, ctx);
}

static int
ki_read_attrs(vnode_t vp, vfs_context_t ctx, uint32_t *uidp, uint32_t *gidp,
    uint16_t *modep)
{
	struct vnode_attr va;
	int error;

	VATTR_INIT(&va);
	VATTR_WANTED(&va, va_uid);
	VATTR_WANTED(&va, va_gid);
	VATTR_WANTED(&va, va_mode);
	error = vnode_getattr(vp, &va, ctx);
	if (error) {
		return error;
	}
	*uidp = (uint32_t)va.va_uid;
	*gidp = (uint32_t)va.va_gid;
	*modep = (uint16_t)(va.va_mode & KI_MODE_MASK);
	return 0;
}

/*
 * Read the inode back and print uid/gid/mode.  A mismatch is a failure: the
 * whole point of creating the skeleton in the kernel is that the result is
 * verified on the volume, not assumed from the request.
 */
static bool
ki_report_attrs(struct ki_entry *e, vnode_t vp, vfs_context_t ctx)
{
	uint32_t uid = 0, gid = 0;
	uint16_t mode = 0;
	int error;

	error = ki_read_attrs(vp, ctx, &uid, &gid, &mode);
	if (error) {
		printf("FAIL %s cannot read attributes (error %d)\n", e->path,
		    error);
		return false;
	}
	if (e->is_dir) {
		printf("OK %s 0 dir mode 0%o uid %u gid %u\n", e->path, mode, uid,
		    gid);
	} else if (e->is_link) {
		printf("OK %s %llu link -> %s mode 0%o uid %u gid %u\n", e->path,
		    (unsigned long long)e->size, e->link, mode, uid, gid);
	} else {
		printf("OK %s %llu mode 0%o uid %u gid %u\n", e->path,
		    (unsigned long long)e->size, mode, uid, gid);
	}
	if (uid != 0 || gid != 0 ||
	    (!e->is_link && mode != e->mode)) {
		printf("FAIL %s attrs uid %u gid %u mode 0%o, wanted 0:0 mode "
		    "0%o\n", e->path, uid, gid, mode, e->mode);
		return false;
	}
	return true;
}

static int
ki_mkdir(const char *path, uint16_t mode, vfs_context_t ctx, bool exact)
{
	struct nameidata nd;
	struct vnode_attr va;
	vnode_t dvp, vp;
	int error;

	NDINIT(&nd, CREATE, OP_MKDIR, LOCKPARENT, UIO_SYSSPACE,
	    CAST_USER_ADDR_T(path), ctx);
	nd.ni_cnd.cn_flags |= WILLBEDIR;
	error = nameiat(&nd, AT_FDCWD);
	if (error) {
		return error;
	}
	dvp = nd.ni_dvp;
	if (nd.ni_vp != NULLVP) {
		/* Already there.  It has to be a directory, not a file. */
		bool isdir = vnode_isdir(nd.ni_vp);
		if (isdir && exact) {
			error = ki_set_attrs(nd.ni_vp, mode, ctx);
		} else {
			error = 0;
		}
		vnode_put(nd.ni_vp);
		nameidone(&nd);
		if (dvp) {
			vnode_put(dvp);
		}
		return isdir ? error : ENOTDIR;
	}
	VATTR_INIT(&va);
	VATTR_SET(&va, va_type, VDIR);
	VATTR_SET(&va, va_mode, mode & KI_MODE_MASK);
	vp = NULLVP;
	error = vn_create(dvp, &vp, &nd, &va, 0, 0, NULL, ctx);
	if (error == 0 && vp != NULLVP) {
		error = ki_set_attrs(vp, mode, ctx);
	}
	if (vp != NULLVP) {
		vnode_put(vp);
	}
	nameidone(&nd);
	if (dvp) {
		vnode_put(dvp);
	}
	return error;
}

/*
 * Create a symlink.  On a re-run the link already exists; the caller treats
 * EEXIST as SKIP after verifying it is at least a symlink.  The target comes
 * from the payload and is never used as a format string.
 */
static int
ki_symlink(const char *path, char *target, uint16_t mode,
    vfs_context_t ctx, vnode_t *vpp)
{
	struct nameidata nd;
	struct vnode_attr va;
	vnode_t dvp, vp;
	int error;

	*vpp = NULLVP;
	NDINIT(&nd, CREATE, OP_SYMLINK, LOCKPARENT, UIO_SYSSPACE,
	    CAST_USER_ADDR_T(path), ctx);
	error = nameiat(&nd, AT_FDCWD);
	if (error) {
		return error;
	}
	dvp = nd.ni_dvp;
	vp = nd.ni_vp;
	if (vp != NULLVP) {
		vnode_put(vp);
		nameidone(&nd);
		if (dvp) {
			vnode_put(dvp);
		}
		return EEXIST;
	}
	VATTR_INIT(&va);
	VATTR_SET(&va, va_type, VLNK);
	VATTR_SET(&va, va_mode, mode & KI_MODE_MASK);
	error = vnode_authattr_new(dvp, &va, 0, ctx);
	if (error == 0) {
		error = VNOP_SYMLINK(dvp, &vp, &nd.ni_cnd, &va, target, ctx);
	}
	if (error == 0 && vp != NULLVP) {
		error = vnode_setattr_fallback(vp, &va, ctx);
		if (error == 0) {
			error = ki_set_owner(vp, ctx);
		}
	}
	nameidone(&nd);
	if (dvp) {
		vnode_put(dvp);
	}
	if (error && vp != NULLVP) {
		vnode_put(vp);
		vp = NULLVP;
	}
	*vpp = vp;
	return error;
}

/*
 * Create every missing parent of `path` from "/" down.  EEXIST is success;
 * anything else stops the entry, because a file opened under a missing
 * directory would just fail later with a less useful error.  Missing parents
 * are 0755; an explicit DIR entry later applies its exact mode.
 */
static int
ki_mkdirs(const char *path, vfs_context_t ctx)
{
	char buf[KEBAB_INST_PATH_MAX];
	size_t len = strlen(path);
	int error;

	if (len >= sizeof(buf)) {
		return ENAMETOOLONG;
	}
	memcpy(buf, path, len + 1);
	for (size_t i = 1; i < len; i++) {
		if (buf[i] != '/') {
			continue;
		}
		buf[i] = 0;
		error = ki_mkdir(buf, 0755, ctx, false);
		buf[i] = '/';
		if (error == EEXIST) {
			continue;
		}
		if (error) {
			return error;
		}
	}
	return 0;
}

/*
 * Hash a destination file.  This is both the resume test and the post-write
 * verification: the caller compares the size and the digest with the entry
 * header.  Returns 0 only when the file was read completely.
 */
static int
ki_hash_file(const char *path, uint64_t *sizep, uint8_t digest[32],
    vfs_context_t ctx)
{
	vnode_t vp = NULLVP;
	SHA256_CTX shactx;
	off_t size;
	uint64_t off = 0;
	int error;

	error = vnode_lookup(path, 0, &vp, ctx);
	if (error) {
		return error;
	}
	if (vp->v_type != VREG) {
		vnode_put(vp);
		return EINVAL;
	}
	size = ubc_getsize(vp);
	if (size < 0) {
		vnode_put(vp);
		return EIO;
	}
	SHA256_Init(&shactx);
	while (off < (uint64_t)size) {
		uint64_t left = (uint64_t)size - off;
		int want = left > KI_IO_BUF ? (int)KI_IO_BUF : (int)left;
		int resid = 0;

		error = vn_rdwr(UIO_READ, vp, (caddr_t)ki.io, want, (off_t)off,
		    UIO_SYSSPACE, 0, vfs_context_ucred(ctx), &resid, NULL);
		if (error) {
			vnode_put(vp);
			return error;
		}
		if (resid != 0) {
			vnode_put(vp);
			return EIO;
		}
		SHA256_Update(&shactx, ki.io, (size_t)want);
		off += (uint64_t)want;
	}
	vnode_put(vp);
	SHA256_Final(digest, &shactx);
	*sizep = (uint64_t)size;
	return 0;
}

/* --------------------------------------------------------- durable output */

/* Normal reset must also flush a separately mounted writable APFS volume.
 * vfs_iterate(0) holds iteration references and calls outside the list lock.
 * Panic reset never reaches this function. Preserve the first sync failure. */
struct ki_sync_state { vfs_context_t context; int error; };
static int
ki_sync_mount(mount_t mount, void *argument)
{
 struct ki_sync_state *state = argument;
 if (!(vfs_flags(mount) & MNT_RDONLY)) {
  int error = VFS_SYNC(mount, MNT_WAIT, state->context);
  if (!state->error) state->error = error;
 }
 return VFS_RETURNED;
}

static int
ki_vfs_sync(void)
{
 if (rootvnode == NULLVP || rootvnode->v_mount == NULL) return ENODEV;
 struct ki_sync_state state = { vfs_context_current(), 0 };
 int error = vfs_iterate(0, ki_sync_mount, &state);
 return state.error ? state.error : error;
}

/*
 * Read the on-disk HFS+ volume header, not the mounted filesystem's cached
 * statfs.  This is the number that proves a flush: the allocation bitmap and
 * the header's freeBlocks field only reach the device when HFS+ syncs.  The
 * caller must have flushed first and must not be running an install.
 */
static int
ki_read_vh(uint32_t *bsizep, uint32_t *blocksp, uint32_t *freep)
{
	uint8_t vh[KI_VH_SIZE];
	uint32_t bsize, blocks, freeb;
	int error;

	error = kebab_ufs_raw_read(KI_VH_OFF, vh, KI_VH_SIZE);
	if (error) {
		return error;
	}
	if (ki_be16(vh) != 0x482b || ki_be16(vh + 2) != 4) {
		return EINVAL;
	}
	bsize = ki_be32(vh + KI_VH_BSIZE_OFF);
	blocks = ki_be32(vh + KI_VH_BLOCKS_OFF);
	freeb = ki_be32(vh + KI_VH_FREE_OFF);
	if (bsize == 0 || blocks == 0 || freeb > blocks) {
		return EINVAL;
	}
	*bsizep = bsize;
	*blocksp = blocks;
	*freep = freeb;
	return 0;
}

static void
ki_sample_store(uint32_t slot)
{
	struct ki_entry *e = &ki.e;
	struct ki_sample *s = &ki.samples[slot];
	size_t len = strlen(e->path);

	memset(s, 0, sizeof(*s));
	if (len >= sizeof(s->path)) {
		return;
	}
	memcpy(s->path, e->path, len + 1);
	s->size = e->size;
	memcpy(s->sha, e->sha, sizeof(s->sha));
	s->tail_len = e->tail_len;
	if (s->tail_len) {
		SHA256_CTX shactx;

		SHA256_Init(&shactx);
		SHA256_Update(&shactx, e->tail, s->tail_len);
		SHA256_Final(s->tail_sha, &shactx);
	}
	s->set = true;
}

/*
 * Remember a few written files for a cold check after the stream has been
 * flushed to the device: the first file, the largest file, the entry at the
 * middle of the stream and the last file.  The last slot is always rewritten,
 * and duplicates are skipped by path at verify time.
 */
static void
ki_sample_add(void)
{
	struct ki_entry *e = &ki.e;

	if (e->is_dir || e->is_link) {
		return;
	}
	if (!ki.samples[KI_SAMPLE_FIRST].set) {
		ki_sample_store(KI_SAMPLE_FIRST);
	}
	if (!ki.samples[KI_SAMPLE_LARGEST].set ||
	    e->size > ki.samples[KI_SAMPLE_LARGEST].size) {
		ki_sample_store(KI_SAMPLE_LARGEST);
	}
	if (!ki.samples[KI_SAMPLE_MIDDLE].set && ki.entries_total > 1 &&
	    ki.entries_done == ki.entries_total / 2) {
		ki_sample_store(KI_SAMPLE_MIDDLE);
	}
	ki_sample_store(KI_SAMPLE_LAST);
}

/*
 * Re-read one sample after its UBC pages were dropped.  There is no
 * device-level read for an arbitrary file: the extents that map a file to
 * device blocks are private to HFS+, so this is the closest a caller can get
 * to the media without changing the filesystem.  The read goes through
 * VNOP_READ again, which has to fetch the pages from the device.
 */
static int
ki_cold_verify_one(const struct ki_sample *s)
{
	vfs_context_t ctx = vfs_context_current();
	vnode_t vp = NULLVP;
	off_t size;
	uint8_t digest[32];
	int resid = 0;
	int error;

	error = vnode_lookup(s->path, 0, &vp, ctx);
	if (error) {
		printf("FAIL %s cold lookup failed (error %d)\n", s->path, error);
		return error;
	}
	if (vp->v_type != VREG) {
		vnode_put(vp);
		printf("FAIL %s is not a regular file after the write\n", s->path);
		return EINVAL;
	}
	size = ubc_getsize(vp);
	if (size < 0 || (uint64_t)size != s->size) {
		printf("FAIL %s cold size %lld, wrote %llu\n", s->path,
		    (long long)size, (unsigned long long)s->size);
		vnode_put(vp);
		return EIO;
	}
	if (size > 0) {
		/* Dirty pages were fsynced and the device cache was flushed
		 * before this runs, so invalidating only discards clean cache. */
		(void)ubc_msync(vp, 0, size, NULL,
		    UBC_PUSHALL | UBC_INVALIDATE | UBC_SYNC);
	}
	if (s->size <= KI_COLD_FULL_MAX) {
		uint64_t have = 0;

		vnode_put(vp);
		error = ki_hash_file(s->path, &have, digest, ctx);
		if (error) {
			printf("FAIL %s cold read failed (error %d)\n", s->path,
			    error);
			return error;
		}
		if (have != s->size ||
		    memcmp(digest, s->sha, sizeof(digest)) != 0) {
			printf("FAIL %s cold full hash differs (%llu bytes)\n",
			    s->path, (unsigned long long)have);
			return EIO;
		}
		printf("INST VERIFY %s %llu cold full ok\n", s->path,
		    (unsigned long long)s->size);
		return 0;
	}
	if (s->tail_len == 0) {
		vnode_put(vp);
		return 0;
	}
	error = vn_rdwr(UIO_READ, vp, (caddr_t)ki.io, (int)s->tail_len,
	    (off_t)(s->size - s->tail_len), UIO_SYSSPACE, 0,
	    vfs_context_ucred(ctx), &resid, NULL);
	if (!error && resid != 0) {
		error = EIO;
	}
	if (error) {
		printf("FAIL %s cold tail read failed (error %d)\n", s->path,
		    error);
		vnode_put(vp);
		return error;
	}
	{
		SHA256_CTX shactx;

		SHA256_Init(&shactx);
		SHA256_Update(&shactx, ki.io, s->tail_len);
		SHA256_Final(digest, &shactx);
	}
	if (memcmp(digest, s->tail_sha, sizeof(digest)) != 0) {
		printf("FAIL %s cold tail differs (%u bytes at %llu)\n", s->path,
		    s->tail_len, (unsigned long long)(s->size - s->tail_len));
		vnode_put(vp);
		return EIO;
	}
	printf("INST VERIFY %s %llu cold tail %u ok\n", s->path,
	    (unsigned long long)s->size, s->tail_len);
	vnode_put(vp);
	return 0;
}

/* Returns the number of samples that failed. */
static int
ki_cold_verify(void)
{
	uint32_t checked = 0;
	uint32_t bad = 0;

	for (uint32_t i = 0; i < KI_SAMPLE_MAX; i++) {
		bool duplicate = false;

		if (!ki.samples[i].set) {
			continue;
		}
		for (uint32_t j = 0; j < i; j++) {
			if (ki.samples[j].set &&
			    strcmp(ki.samples[i].path,
			    ki.samples[j].path) == 0) {
				duplicate = true;
				break;
			}
		}
		if (duplicate) {
			continue;
		}
		checked++;
		if (ki_cold_verify_one(&ki.samples[i]) != 0) {
			bad++;
		}
	}
	if (bad) {
		printf("INST VERIFY %u of %u cold sample(s) failed\n", bad,
		    checked);
	} else if (checked) {
		printf("INST VERIFY %u cold sample(s) ok\n", checked);
	}
	return (int)bad;
}

/*
 * Print the on-disk free-block movement for this stream.  before is read from
 * the volume header at INST BEGIN, after from the same header once VFS_SYNC
 * and SYNCHRONIZE CACHE have run.  expected converts the bytes this stream
 * actually wrote (minus the old contents it replaced) to 4 KiB blocks, so a
 * non-zero delta is what distinguishes "allocated on the volume" from "only
 * in the page cache".
 */
static void
ki_report_free_delta(void)
{
	uint32_t bsize = 0, blocks = 0, after = 0;
	uint64_t net, expected;
	int64_t delta;
	int error;

	if (!ki.free_before_valid) {
		printf("INST FREE before unknown: the volume header was not "
		    "readable at INST BEGIN\n");
		return;
	}
	error = ki_read_vh(&bsize, &blocks, &after);
	if (error) {
		printf("INST FREE before %u after unknown (error %d)\n",
		    ki.free_before, error);
		return;
	}
	delta = (int64_t)ki.free_before - (int64_t)after;
	net = ki.bytes_written > ki.bytes_replaced ?
	    ki.bytes_written - ki.bytes_replaced : 0;
	expected = bsize ? (net + bsize - 1) / bsize : 0;
	printf("INST FREE before %u after %u delta %lld expected %llu "
	    "bsize %u written %llu replaced %llu\n", ki.free_before, after,
	    (long long)delta, (unsigned long long)expected, bsize,
	    (unsigned long long)ki.bytes_written,
	    (unsigned long long)ki.bytes_replaced);
}

/*
 * Push the whole install to stable storage: VFS metadata first, then the
 * device write cache, then the cold sample check.  Called from the stream
 * state machine before INST DONE.  Returns the number of failed steps (0
 * means everything reached the device).
 */
static int
ki_sync_media(const char *why, bool samples)
{
	uint64_t t0 = ki_now_ns();
	int vfs_error;
	int dev_error;
	int bad = 0;

	vfs_error = ki_vfs_sync();
	/* SYNCHRONIZE CACHE is meaningless when ufsrw=0 never enabled writes. */
	dev_error = kebab_ufs_sync();
	if (dev_error == EROFS) {
		dev_error = 0;
	}
	if (vfs_error) {
		printf("FAIL install data: VFS_SYNC (%s) failed (error %d)\n",
		    why, vfs_error);
		bad++;
	}
	if (dev_error) {
		printf("FAIL install data: SYNCHRONIZE CACHE (%s) failed "
		    "(error %d)\n", why, dev_error);
		bad++;
	}
	if (bad) {
		return bad;
	}
	printf("INST SYNC %s vfs ok device ok ms %llu\n", why,
	    (unsigned long long)((ki_now_ns() - t0) / 1000000ull));
	if (samples) {
		bad += ki_cold_verify();
	}
	return bad;
}

/*
 * Reset-path flush.  FASTBOOT/REBOOT are console commands, so they run after
 * the stream and after any install; the data has to be on the device before
 * the reset, not merely in the UBC or the drive's write cache.  A panic must
 * not wait for a filesystem sync, and the guard below is what makes that true
 * instead of a comment that nothing enforces.
 */
bool
kebab_install_sync_for_reset(const char *why)
{
	uint64_t t0 = ki_now_ns();
	int vfs_error;
	int dev_error;

	/*
	 * A panic runs with preemption disabled, so any mutex acquisition on this
	 * path is an immediate nested panic.  The console command dispatcher used
	 * to reach this function from the panic pump, which meant a REBOOT from
	 * the host after a panic ran VFS_SYNC(MNT_WAIT) and then took
	 * kebab_ufs_lock with preemption disabled.  The dispatcher no longer calls
	 * this while panicking; this guard keeps the rule true for any future
	 * caller.
	 */
	if (panic_active()) {
		printf("KEBAB SYNC: %s refused: a panic may not run a filesystem "
		    "sync (VFS_SYNC/kebab_ufs_lock would nest)\n", why);
		return false;
	}

	vfs_error = ki_vfs_sync();
	dev_error = kebab_ufs_sync();
	if (dev_error == EROFS) {
		dev_error = 0;
	}
	if (vfs_error || dev_error) {
		printf("KEBAB SYNC: %s FAILED (vfs %d, device %d); not resetting\n",
		    why, vfs_error, dev_error);
		return false;
	}
	printf("KEBAB SYNC: %s: VFS_SYNC + SYNCHRONIZE CACHE ok ms %llu\n", why,
	    (unsigned long long)((ki_now_ns() - t0) / 1000000ull));
	return true;
}

/*
 * SYNC console command.  It forces the same two flushes as the stream end and
 * then reads the volume header back from the device, so an operator can see
 * the free-block count that actually reached the medium.  It works with
 * kebab_noinit=1 because it does not need the init thread.
 */
void
kebab_install_sync_report(void)
{
	uint64_t t0 = ki_now_ns();
	uint32_t before_bsize = 0, before_blocks = 0, before_free = 0;
	uint32_t after_bsize = 0, after_blocks = 0, after_free = 0;
	int before_error, after_error;
	int vfs_error, dev_error;

	printf("SYNC BEGIN\n");
	before_error = ki_read_vh(&before_bsize, &before_blocks, &before_free);
	vfs_error = ki_vfs_sync();
	dev_error = kebab_ufs_sync();
	if (dev_error == EROFS) {
		dev_error = 0;
	}
	after_error = ki_read_vh(&after_bsize, &after_blocks, &after_free);
	printf("SYNC VFS vfs %d device %d\n", vfs_error, dev_error);
	if (before_error == 0 && after_error == 0) {
		printf("SYNC VH before %u after %u delta %lld bsize %u "
		    "blocks %u\n", before_free, after_free,
		    (long long)((int64_t)before_free - (int64_t)after_free),
		    after_bsize, after_blocks);
	} else {
		printf("SYNC VH unreadable (before %d, after %d)\n",
		    before_error, after_error);
	}
	printf("SYNC DONE vfs %d device %d ms %llu\n", vfs_error, dev_error,
	    (unsigned long long)((ki_now_ns() - t0) / 1000000ull));
}

/*
 * Free and total bytes on the root volume.  HFS+ reports these in allocation
 * blocks; the installer uses them to refuse a stream that cannot fit before it
 * writes the first byte, and to show the operator why.
 */
static int
ki_space(uint64_t *freep, uint64_t *totalp)
{
	struct vfsstatfs *sfs;

	if (rootvnode == NULLVP || rootvnode->v_mount == NULL) {
		return ENODEV;
	}
	sfs = vfs_statfs(rootvnode->v_mount);
	if (sfs == NULL || sfs->f_bsize == 0) {
		return ENODEV;
	}
	*freep = (uint64_t)sfs->f_bavail * (uint64_t)sfs->f_bsize;
	*totalp = (uint64_t)sfs->f_blocks * (uint64_t)sfs->f_bsize;
	return 0;
}

/* ------------------------------------------------------------ zlib hooks */

/*
 * zlib in this kernel has no allocator of its own configured for BSD
 * callers: give it one and keep the allocation size in a header, because
 * kfree_data() wants the same size it was handed.
 */
#define KI_ZHEAD 16u

static voidpf
ki_zalloc(voidpf opaque __unused, uInt items, uInt size)
{
	size_t bytes = (size_t)items * (size_t)size + KI_ZHEAD;
	void *raw = kalloc_data(bytes, Z_WAITOK | Z_ZERO);
	uintptr_t base;

	if (raw == NULL) {
		return NULL;
	}
	*(size_t *)raw = bytes;
	base = (uintptr_t)raw + KI_ZHEAD;
	return (voidpf)base;
}

static void
ki_zfree(voidpf opaque __unused, voidpf ptr)
{
	uintptr_t base;
	size_t bytes;

	if (ptr == NULL) {
		return;
	}
	base = (uintptr_t)ptr - KI_ZHEAD;
	bytes = *(size_t *)base;
	{
		void *raw = (void *)base;
		kfree_data(raw, bytes);
	}
}

/* ---------------------------------------------------------- entry output */

/*
 * Keep the last KI_TAIL_LEN bytes of a file as it is written.  The stream is
 * forward-only, so this is the only way to know the tail without a second
 * pass; the completion sample check compares it against an uncached read.
 */
static void
ki_tail_update(struct ki_entry *e, const void *p, size_t n)
{
	const uint8_t *b = p;

	if (n >= KI_TAIL_LEN) {
		memcpy(e->tail, b + n - KI_TAIL_LEN, KI_TAIL_LEN);
		e->tail_len = KI_TAIL_LEN;
		return;
	}
	if ((size_t)e->tail_len + n > KI_TAIL_LEN) {
		uint32_t drop = (uint32_t)((size_t)e->tail_len + n - KI_TAIL_LEN);

		memmove(e->tail, e->tail + drop, e->tail_len - drop);
		e->tail_len -= drop;
	}
	memcpy(e->tail + e->tail_len, b, n);
	e->tail_len += (uint32_t)n;
}

static int
ki_write_bytes(const void *p, size_t n)
{
	struct ki_entry *e = &ki.e;
	int resid = 0;
	int error;

	SHA256_Update(&e->shactx, p, n);
	e->got += n;
	if (ki.dry || e->skip) {
		return 0;
	}
	if (!e->opened) {
		return EBADF;
	}
	error = vn_rdwr(UIO_WRITE, e->vp, (caddr_t)(uintptr_t)p, (int)n,
	    (off_t)e->offset,
	    UIO_SYSSPACE, 0, vfs_context_ucred(vfs_context_current()), &resid,
	    NULL);
	if (error) {
		return error;
	}
	if (resid != 0) {
		return EIO;
	}
	e->offset += n;
	ki_tail_update(e, p, n);
	return 0;
}

/* --------------------------------------------------------------- abort */

static void
ki_reset(void)
{
	struct ki_entry *e = &ki.e;

	if (e->zs_live) {
		inflateEnd(&e->zs);
		e->zs_live = false;
	}
	if (e->opened && e->vp != NULLVP) {
		vnode_close(e->vp, FWRITE, vfs_context_current());
		e->opened = false;
		e->vp = NULLVP;
	}
	ki.active = false;
	ki.state = KI_S_DONE;
	ki.hdr_got = 0;
	ki.session++;
	memset(e, 0, sizeof(*e));
	e->vp = NULLVP;
}

static void
ki_abort(const char *why, int error)
{
	if (error) {
		printf("INST ABORT %s (error %d)\n", why, error);
	} else {
		printf("INST ABORT %s\n", why);
	}
	ki.failed++;
	ki_reset();
}

/* --------------------------------------------------------- entry handling */

static void ki_finish_entry(void);

static void
ki_next_entry(void)
{
	ki.entries_done++;
	if (ki.entries_done >= ki.entries_total) {
		ki.state = KI_S_PAD;
	} else {
		ki.state = KI_S_ENTRY_HDR;
		ki.hdr_got = 0;
	}
}

static void
ki_start_entry(void)
{
	struct ki_entry *e = &ki.e;
	vfs_context_t ctx = vfs_context_current();
	uint32_t plen = ki_le16(ki.hdr + KI_HDR_PLEN_OFF);
	uint64_t size = ki_le64(ki.hdr + KI_HDR_USIZE_OFF);
	uint64_t wire = ki_le64(ki.hdr + KI_HDR_WIRE_OFF);
	uint64_t have = 0;
	uint16_t eflags;
	uint8_t digest[32];
	int error = 0;

	if (ki_le32(ki.hdr) != KI_ENTRY_MAGIC) {
		ki_abort("entry magic", 0);
		return;
	}
	if (plen == 0 || plen >= KEBAB_INST_PATH_MAX) {
		ki_abort("entry path length", 0);
		return;
	}
	memset(e, 0, sizeof(*e));
	e->vp = NULLVP;
	memcpy(e->path, ki.hdr + KI_HDR_PATH_OFF, plen);
	e->path[plen] = 0;
	if (!ki_path_ok(e->path, plen, ki.skel)) {
		printf("FAIL %s rejected: outside the %s allowlist or "
		    "malformed\n", e->path, ki.skel ? "skeleton" : "install");
		ki_abort("destination refused", 0);
		return;
	}
	e->mode = ki_le16(ki.hdr + KI_HDR_MODE_OFF) &
	    (ki.version == 1 ? 0777u : KI_MODE_MASK);
	e->flags = ki_le16(ki.hdr + KI_HDR_FLAGS_OFF);
	eflags = e->flags;
	e->deflate = (eflags & KI_F_DEFLATE) != 0;
	e->is_dir = (eflags & KI_F_DIR) != 0;
	e->is_link = (eflags & KI_F_SYMLINK) != 0;
	e->size = size;
	e->wire = wire;
	e->wire_left = wire;
	memcpy(e->sha, ki.hdr + KI_HDR_SHA_OFF, sizeof(e->sha));

	if (ki.version < 2 && (eflags & KI_F_TYPE)) {
		printf("FAIL %s: a v%u stream cannot carry a DIR/LINK entry\n",
		    e->path, ki.version);
		ki_abort("entry type needs KIST v2", 0);
		return;
	}
	if ((eflags & KI_F_TYPE) == KI_F_TYPE) {
		printf("FAIL %s: entry is both a directory and a symlink\n",
		    e->path);
		ki_abort("ambiguous entry type", 0);
		return;
	}
	if (eflags & ~(KI_F_DEFLATE | KI_F_TYPE)) {
		printf("FAIL %s: unknown entry flags 0x%04x\n", e->path, eflags);
		ki_abort("unknown entry flags", 0);
		return;
	}
	if ((e->is_dir || e->is_link) && e->deflate) {
		printf("FAIL %s: DIR/LINK entry carries a deflated payload\n",
		    e->path);
		ki_abort("deflated non-file entry", 0);
		return;
	}

	/* The payload cannot be longer than the entry claims, and the entry
	 * cannot claim more bytes than remain in the stream. */
	if (wire > ki.wire_left) {
		printf("FAIL %s wire size %llu exceeds the %llu bytes left in "
		    "the stream\n", e->path, (unsigned long long)wire,
		    (unsigned long long)ki.wire_left);
		ki_abort("entry overruns the stream", 0);
		return;
	}
	if (!e->deflate && wire != size) {
		printf("FAIL %s stored payload %llu != file size %llu\n", e->path,
		    (unsigned long long)wire, (unsigned long long)size);
		ki_abort("inconsistent entry", 0);
		return;
	}

	if (e->is_dir) {
		static const uint8_t zero_sha[32];

		if (wire != 0 || size != 0 ||
		    memcmp(e->sha, zero_sha, sizeof(zero_sha)) != 0) {
			printf("FAIL %s: directory entry has a payload or a "
			    "hash\n", e->path);
			ki_abort("malformed directory entry", 0);
			return;
		}
		if (ki.dry) {
			printf("PLAN %s 0 dir mode 0%o\n", e->path, e->mode);
		} else {
			vnode_t vp = NULLVP;

			error = ki_mkdir(e->path, e->mode, ctx, true);
			if (error == 0) {
				error = vnode_lookup(e->path, 0, &vp, ctx);
			}
			if (error) {
				printf("FAIL %s cannot create directory "
				    "(error %d)\n", e->path, error);
				ki_abort("mkdir failed", error);
				return;
			}
			if (!ki_report_attrs(e, vp, ctx)) {
				vnode_put(vp);
				ki_abort("directory attributes", 0);
				return;
			}
			vnode_put(vp);
			ki.ok++;
		}
		ki_next_entry();
		return;
	}

	if (e->is_link) {
		if (wire == 0 || wire >= KEBAB_INST_PATH_MAX) {
			printf("FAIL %s: symlink target length %llu\n", e->path,
			    (unsigned long long)wire);
			ki_abort("bad symlink target length", 0);
			return;
		}
		SHA256_Init(&e->shactx);
		e->shactx_live = true;
		ki.state = KI_S_PAYLOAD;
		return;
	}

	if (!ki.dry && !ki.skel) {
		/*
		 * Resume: a destination that already has the right size and
		 * hash is left alone.  Only a regular file with a matching
		 * size is worth reading.  Skeleton files are always rewritten
		 * so their ownership and mode are re-applied.
		 */
		error = ki_hash_file(e->path, &have, digest, ctx);
		if (error == 0) {
			e->old_size = have;
		}
		if (error == 0 && have == size &&
		    memcmp(digest, e->sha, sizeof(digest)) == 0) {
			e->skip = true;
			ki.skipped++;
			printf("SKIP %s %llu\n", e->path,
			    (unsigned long long)size);
			/* The payload still arrives; consume and discard it. */
			ki.state = KI_S_PAYLOAD;
			return;
		}
	}
	if (!ki.dry) {
		/*
		 * Space check.  A destination that already exists and is about
		 * to be truncated gives its blocks back, so the room needed is
		 * the new size minus what the old file holds.  Refusing here
		 * keeps a full volume from turning into a half-written file
		 * that looks like a transport failure.
		 */
		uint64_t freeb = 0, totalb = 0;
		uint64_t reclaimable = (error == 0) ? have : 0;

		if (ki_space(&freeb, &totalb) == 0 &&
		    size > freeb + reclaimable) {
			printf("FAIL %s needs %llu bytes but the root "
			    "volume has %llu free of %llu\n",
			    e->path, (unsigned long long)size,
			    (unsigned long long)freeb,
			    (unsigned long long)totalb);
			ki_abort("not enough space", 0);
			return;
		}
		error = ki_mkdirs(e->path, ctx);
		if (error) {
			printf("FAIL %s cannot create parent directories "
			    "(error %d)\n", e->path, error);
			ki_abort("mkdir failed", error);
			return;
		}
		error = vnode_open(e->path, O_CREAT | O_TRUNC | FWRITE, e->mode,
		    0, &e->vp, ctx);
		if (error) {
			printf("FAIL %s cannot open for writing (error %d)\n",
			    e->path, error);
			ki_abort("open failed", error);
			return;
		}
		e->opened = true;
		if (e->vp->v_type != VREG) {
			printf("FAIL %s is not a regular file\n", e->path);
			ki_abort("not a regular file", 0);
			return;
		}
		/* vnode_open only applies cmode when it creates the file, and
		 * the filesystem may apply its umask and inherit the parent's
		 * gid.  Force the exact uid/gid/mode. */
		error = ki_set_attrs(e->vp, e->mode, ctx);
		if (error) {
			printf("FAIL %s cannot set attributes (error %d)\n",
			    e->path, error);
			ki_abort("setattr failed", error);
			return;
		}
		e->offset = 0;
	}
	SHA256_Init(&e->shactx);
	e->shactx_live = true;

	if (e->deflate) {
		memset(&e->zs, 0, sizeof(e->zs));
		e->zs.zalloc = ki_zalloc;
		e->zs.zfree = ki_zfree;
		e->zs.opaque = NULL;
		error = inflateInit(&e->zs);
		if (error != Z_OK) {
			printf("FAIL %s cannot start inflate (%d)\n", e->path,
			    error);
			ki_abort("inflate init failed", 0);
			return;
		}
		e->zs_live = true;
		/* Compressed payloads are usually larger than a chunk?  No:
		 * they are on average smaller, but keep the guarantee explicit
		 * by not requiring a whole entry to fit. */
	}
	ki.state = KI_S_PAYLOAD;
}

static void
ki_payload(const uint8_t *p, uint32_t n)
{
	struct ki_entry *e = &ki.e;
	int error = 0;

	if (e->is_link) {
		if (e->got + n > KEBAB_INST_PATH_MAX - 1) {
			printf("FAIL %s symlink target longer than %u bytes\n",
			    e->path, KEBAB_INST_PATH_MAX - 1);
			ki_abort("symlink target too long", 0);
			return;
		}
		memcpy(e->link + e->got, p, n);
		SHA256_Update(&e->shactx, p, n);
		e->got += n;
		return;
	}
	if (e->skip) {
		/* Nothing is written and nothing is verified: the destination
		 * is already known good. */
		e->got += n;
		return;
	}
	if (!e->deflate) {
		error = ki_write_bytes(p, n);
		if (error) {
			goto fail;
		}
		return;
	}

	e->zs.next_in = (Bytef *)(uintptr_t)(const void *)p;
	e->zs.avail_in = n;
	while (e->zs.avail_in > 0) {
		uint32_t produced;
		int rc;

		e->zs.next_out = (Bytef *)ki.io;
		e->zs.avail_out = KI_IO_BUF;
		rc = inflate(&e->zs, Z_NO_FLUSH);
		if (rc != Z_OK && rc != Z_STREAM_END) {
			printf("FAIL %s inflate error %d\n", e->path, rc);
			ki_abort("inflate failed", 0);
			return;
		}
		produced = KI_IO_BUF - e->zs.avail_out;
		if (produced) {
			error = ki_write_bytes(ki.io, produced);
			if (error) {
				goto fail;
			}
		}
		if (rc == Z_STREAM_END) {
			e->z_end = true;
		}
		if (rc == Z_STREAM_END && e->zs.avail_in > 0) {
			printf("FAIL %s deflate stream ends before its payload "
			    "does\n", e->path);
			ki_abort("trailing compressed bytes", 0);
			return;
		}
	}
	return;

fail:
	printf("FAIL %s write failed (error %d)\n", e->path, error);
	ki_abort("write failed", error);
}

static void
ki_finish_entry(void)
{
	struct ki_entry *e = &ki.e;
	vfs_context_t ctx = vfs_context_current();
	uint8_t digest[32];
	uint64_t have = 0;
	int error;

	/* A directory entry is completely handled by ki_start_entry(). */
	if (e->is_dir) {
		ki_next_entry();
		return;
	}
	if (e->skip) {
		ki_next_entry();
		return;
	}
	if (!e->shactx_live) {
		ki_abort("entry has no hash context", 0);
		return;
	}
	if (!e->is_link && e->deflate) {
		while (e->zs_live && !e->z_end) {
			uint32_t produced;
			int rc;

			e->zs.next_out = (Bytef *)ki.io;
			e->zs.avail_out = KI_IO_BUF;
			rc = inflate(&e->zs, Z_FINISH);
			produced = KI_IO_BUF - e->zs.avail_out;
			if (produced) {
				error = ki_write_bytes(ki.io, produced);
				if (error) {
					printf("FAIL %s write failed (error %d)\n",
					    e->path, error);
					ki_abort("write failed", error);
					return;
				}
			}
			if (rc == Z_STREAM_END) {
				e->z_end = true;
			} else if (rc != Z_OK || produced == 0) {
				printf("FAIL %s deflate stream did not end (%d)\n",
				    e->path, rc);
				ki_abort("short deflate stream", 0);
				return;
			}
		}
		if (e->zs_live) {
			inflateEnd(&e->zs);
			e->zs_live = false;
		}
	}
	if (e->got != e->size) {
		printf("FAIL %s produced %llu bytes, header says %llu\n", e->path,
		    (unsigned long long)e->got, (unsigned long long)e->size);
		ki_abort("size mismatch", 0);
		return;
	}
	SHA256_Final(digest, &e->shactx);
	e->shactx_live = false;
	if (memcmp(digest, e->sha, sizeof(digest)) != 0) {
		char want[65], got[65];
		for (uint32_t i = 0; i < 32; i++) {
			ki_hex08(&want[i * 2], e->sha[i]);
			ki_hex08(&got[i * 2], digest[i]);
		}
		want[64] = got[64] = 0;
		printf("FAIL %s payload hash %s != header %s\n", e->path, got, want);
		ki_abort("payload hash mismatch", 0);
		return;
	}

	if (e->is_link) {
		e->link[e->got] = 0;
		if (ki.dry) {
			printf("PLAN %s %llu link -> %s mode 0%o\n", e->path,
			    (unsigned long long)e->size, e->link, e->mode);
		} else {
			vnode_t vp = NULLVP;
			bool existing = false;

			error = ki_mkdirs(e->path, ctx);
			if (error == 0) {
				error = ki_symlink(e->path, e->link, e->mode, ctx,
				    &vp);
			}
			if (error == EEXIST) {
				existing = true;
				error = vnode_lookup(e->path,
				    VNODE_LOOKUP_NOFOLLOW, &vp, ctx);
			}
			if (error) {
				printf("FAIL %s cannot create symlink "
				    "(error %d)\n", e->path, error);
				ki_abort("symlink failed", error);
				return;
			}
			if (existing) {
				/*
				 * A re-run: accept an existing symlink as
				 * already present.  The creation path above is
				 * the one that proves the target.
				 */
				if (!vnode_islnk(vp)) {
					printf("FAIL %s exists and is not a "
					    "symlink\n", e->path);
					vnode_put(vp);
					ki_abort("existing non-symlink", 0);
					return;
				}
				printf("SKIP %s %llu link exists\n", e->path,
				    (unsigned long long)e->size);
				ki.skipped++;
				vnode_put(vp);
			} else {
				if (!ki_report_attrs(e, vp, ctx)) {
					vnode_put(vp);
					ki_abort("symlink attributes", 0);
					return;
				}
				vnode_put(vp);
				ki.ok++;
			}
		}
		ki_next_entry();
		return;
	}

	if (ki.dry) {
		printf("PLAN %s %llu mode 0%o %s\n", e->path,
		    (unsigned long long)e->size, e->mode,
		    e->deflate ? "deflate" : "store");
	} else {
		if (e->opened) {
			/*
			 * The file's data is only in the UBC until this runs.
			 * hfs_fsync() pushes the clusters with IO_SYNC and
			 * updates/metasyncs the catalog record; the stream-end
			 * VFS_SYNC and SYNCHRONIZE CACHE below then make that
			 * durable on the device.  Do it while the vnode is still
			 * held, before close drops it.
			 */
			error = VNOP_FSYNC(e->vp, MNT_WAIT, ctx);
			if (error) {
				printf("FAIL %s fsync failed (error %d)\n",
				    e->path, error);
				ki_abort("fsync failed", error);
				return;
			}
			error = vnode_close(e->vp, FWRITE, ctx);
			e->opened = false;
			e->vp = NULLVP;
			if (error) {
				printf("FAIL %s close failed (error %d)\n",
				    e->path, error);
				ki_abort("close failed", error);
				return;
			}
		}
		/* Read the file back: the payload hash proved the wire, this
		 * proves the volume. */
		error = ki_hash_file(e->path, &have, digest, ctx);
		if (error) {
			printf("FAIL %s cannot be read back (error %d)\n", e->path,
			    error);
			ki_abort("read-back failed", error);
			return;
		}
		if (have != e->size || memcmp(digest, e->sha, sizeof(digest)) != 0) {
			printf("FAIL %s read back %llu bytes with a different "
			    "hash\n", e->path, (unsigned long long)have);
			ki_abort("read-back mismatch", 0);
			return;
		}
		{
			vnode_t vp = NULLVP;

			error = vnode_lookup(e->path, 0, &vp, ctx);
			if (error) {
				printf("FAIL %s cannot look up after write "
				    "(error %d)\n", e->path, error);
				ki_abort("lookup failed", error);
				return;
			}
			if (!ki_report_attrs(e, vp, ctx)) {
				vnode_put(vp);
				ki_abort("file attributes", 0);
				return;
			}
			vnode_put(vp);
		}
		ki_sample_add();
		ki.bytes_written += e->size;
		ki.bytes_replaced += e->old_size;
		ki.ok++;
	}
	ki_next_entry();
}

/* --------------------------------------------------------------- session */

void
kebab_install_begin(bool dry, uint64_t wire_bytes)
{
	if (!ki_gate()) {
		return;
	}
	if (ki.active) {
		printf("INST busy: %llu of %llu bytes already consumed\n",
		    (unsigned long long)ki.wire_seen,
		    (unsigned long long)ki.wire_total);
		return;
	}
	if (wire_bytes == 0) {
		printf("INST refused: a stream needs a non-zero length\n");
		return;
	}
	if (ki.io == NULL) {
		ki.io = kalloc_data(KI_IO_BUF, Z_WAITOK);
		if (ki.io == NULL) {
			printf("INST refused: no %u-byte buffer\n", KI_IO_BUF);
			return;
		}
	}
	memset(&ki.e, 0, sizeof(ki.e));
	ki.e.vp = NULLVP;
	memset(ki.samples, 0, sizeof(ki.samples));
	ki.active = true;
	ki.dry = dry;
	ki.skel = false;
	ki.version = 0;
	ki.stream_flags = 0;
	ki.state = KI_S_STREAM_HDR;
	ki.hdr_got = 0;
	ki.wire_total = wire_bytes;
	ki.wire_seen = 0;
	ki.wire_left = wire_bytes;
	ki.usize_total = 0;
	ki.entries_total = 0;
	ki.entries_done = 0;
	ki.ok = 0;
	ki.skipped = 0;
	ki.failed = 0;
	ki.bytes_written = 0;
	ki.bytes_replaced = 0;
	ki.free_before = 0;
	ki.vh_bsize = 0;
	ki.free_before_valid = false;
	if (!dry) {
		uint32_t blocks = 0;

		if (ki_read_vh(&ki.vh_bsize, &blocks, &ki.free_before) == 0) {
			ki.free_before_valid = true;
		}
	}
	ki.ns_start = ki.ns_last = ki_now_ns();
	ki.session++;
	printf("INST BEGIN %llu %s chunk %u\n",
	    (unsigned long long)wire_bytes, dry ? "dry" : "write",
	    (unsigned)KEBAB_INST_CHUNK);
}

static void
ki_finish_stream(void)
{
	uint64_t ms;

	if (ki.state != KI_S_PAD && ki.state != KI_S_DONE) {
		ki_abort("stream ended mid-entry", 0);
		return;
	}
	if (ki.entries_done != ki.entries_total) {
		printf("INST ABORT stream ended after %u of %u entries\n",
		    ki.entries_done, ki.entries_total);
		ki.failed++;
		ki_reset();
		return;
	}
	if (ki.dry) {
		printf("INST PLAN entries %u bytes %llu wire %llu\n",
		    ki.entries_done, (unsigned long long)ki.usize_total,
		    (unsigned long long)ki.wire_seen);
		ki_reset();
		return;
	}
	/*
	 * The stream is complete, but every byte can still be in the UBC or in
	 * the device's write cache.  Flush the filesystem metadata, then the
	 * device cache, then re-read a few files with their cache dropped;
	 * only after that may INST DONE claim the install is on the media.
	 */
	if (ki.failed == 0) {
		ki.failed += (uint32_t)ki_sync_media("stream end", true);
	}
	if (ki.failed == 0) {
		ki_report_free_delta();
	}
	ms = (ki_now_ns() - ki.ns_start) / 1000000ull;
	if (ki.failed == 0) {
		printf("INST DONE entries %u ok %u skipped %u bytes %llu "
		    "wire %llu ms %llu\n", ki.entries_done, ki.ok, ki.skipped,
		    (unsigned long long)ki.usize_total,
		    (unsigned long long)ki.wire_seen, (unsigned long long)ms);
	} else {
		printf("INST DONE with %u failure(s), entries %u ok %u "
		    "skipped %u\n", ki.failed, ki.entries_done, ki.ok, ki.skipped);
	}
	ki_reset();
}

/*
 * Consume one bulk-OUT chunk.  The caller (the USB event thread) must not hold
 * the transport's receive lock: everything below can sleep in the filesystem.
 * The ACK is printed only after the bytes are written, verified or discarded,
 * which is what stops the host from overrunning this side.
 */
void
kebab_install_bytes(const void *buf, uint32_t len)
{
	const uint8_t *p = buf;
	uint32_t n = len;

	if (!ki.active) {
		return;
	}
	if ((uint64_t)len > ki.wire_left) {
		printf("INST ABORT chunk of %u bytes exceeds the %llu bytes left\n",
		    len, (unsigned long long)ki.wire_left);
		ki.failed++;
		ki_reset();
		return;
	}
	ki.ns_last = ki_now_ns();

	while (n > 0 && ki.active) {
		switch (ki.state) {
		case KI_S_STREAM_HDR: {
			uint32_t need = KI_STREAM_HDR - ki.hdr_got;
			uint32_t take = MIN(need, n);

			memcpy(ki.hdr + ki.hdr_got, p, take);
			ki.hdr_got += take;
			p += take;
			n -= take;
			ki.wire_seen += take;
			ki.wire_left -= take;
			if (ki.hdr_got < KI_STREAM_HDR) {
				break;
			}
			if (ki_le32(ki.hdr) != KI_STREAM_MAGIC) {
				ki_abort("bad stream magic", 0);
				return;
			}
			{
				uint16_t kver = ki_le16(ki.hdr + 4);
				uint16_t sflags = ki_le16(ki.hdr + 6);

				if (kver < KI_VERSION_MIN ||
				    kver > KI_VERSION) {
					printf("INST ABORT unsupported KIST "
					    "version %u (want %u..%u)\n",
					    kver, KI_VERSION_MIN, KI_VERSION);
					ki.failed++;
					ki_reset();
					return;
				}
				if (kver == 1) {
					if (sflags & ~KI_SF_DEFLATE) {
						ki_abort("v1 stream flags", 0);
						return;
					}
				} else if (sflags &
				    ~(KI_SF_DEFLATE | KI_SF_SKELETON)) {
					ki_abort("unknown stream flags", 0);
					return;
				}
				ki.version = kver;
				ki.stream_flags = sflags;
				ki.skel = (kver >= 2) &&
				    (sflags & KI_SF_SKELETON) != 0;
			}
			ki.entries_total = ki_le32(ki.hdr + 8);
			ki.usize_total = ki_le64(ki.hdr + 16);
			if (ki_le32(ki.hdr + 12) != KI_STREAM_HDR) {
				ki_abort("unexpected stream header size", 0);
				return;
			}
			if ((uint64_t)ki_le64(ki.hdr + 24) + KI_STREAM_HDR !=
			    ki.wire_total) {
				printf("INST ABORT stream declares %llu wire "
				    "bytes, INST BEGIN was given %llu\n",
				    (unsigned long long)(ki_le64(ki.hdr + 24) +
				    KI_STREAM_HDR),
				    (unsigned long long)ki.wire_total);
				ki.failed++;
				ki_reset();
				return;
			}
			printf("INST STREAM v%u%s entries %u bytes %llu "
			    "wire %llu\n", ki.version,
			    ki.skel ? " skeleton" : "", ki.entries_total,
			    (unsigned long long)ki.usize_total,
			    (unsigned long long)ki.wire_total);
			ki.hdr_got = 0;
			ki.state = ki.entries_total ? KI_S_ENTRY_HDR : KI_S_PAD;
			break;
		}
		case KI_S_ENTRY_HDR: {
			uint32_t need = KI_ENTRY_HDR - ki.hdr_got;
			uint32_t take = MIN(need, n);

			memcpy(ki.hdr + ki.hdr_got, p, take);
			ki.hdr_got += take;
			p += take;
			n -= take;
			ki.wire_seen += take;
			ki.wire_left -= take;
			if (ki.hdr_got < KI_ENTRY_HDR) {
				break;
			}
			ki.hdr_got = 0;
			ki_start_entry();
			break;
		}
		case KI_S_PAYLOAD: {
			uint64_t left = ki.e.wire_left;
			uint32_t take = left > n ? n : (uint32_t)left;

			ki_payload(p, take);
			if (!ki.active) {
				return;
			}
			p += take;
			n -= take;
			ki.wire_seen += take;
			ki.wire_left -= take;
			ki.e.wire_left -= take;
			if (ki.e.wire_left == 0) {
				ki_finish_entry();
			}
			break;
		}
		case KI_S_PAD:
			p += n;
			ki.wire_seen += n;
			ki.wire_left -= n;
			n = 0;
			break;
		default:
			return;
		}
	}
	if (!ki.active) {
		return;
	}
	printf("INST ACK %llu\n", (unsigned long long)ki.wire_seen);
	if (ki.wire_left == 0) {
		ki_finish_stream();
	}
}

/* Watchdog: a host that dies mid-stream must not leave the console deaf. */
void
kebab_install_poll(void)
{
	if (!ki.active) {
		return;
	}
	if (ki_now_ns() - ki.ns_last < KI_TIMEOUT_NS) {
		return;
	}
	printf("INST TIMEOUT after %llu of %llu bytes\n",
	    (unsigned long long)ki.wire_seen,
	    (unsigned long long)ki.wire_total);
	ki.failed++;
	ki_reset();
}

bool
kebab_install_active(void)
{
	return ki.active;
}

void
kebab_install_status(void)
{
	bool rw = false;
	uint32_t vh_bsize = 0, vh_blocks = 0, vh_free = 0;

	if (rootvnode != NULLVP && rootvnode->v_mount != NULL) {
		rw = (vfs_flags(rootvnode->v_mount) & MNT_RDONLY) == 0;
	}
	printf("INST? gated %d active %d dry %d root %s chunk %u\n",
	    kebab_install_gated() ? 1 : 0, ki.active ? 1 : 0, ki.dry ? 1 : 0,
	    rw ? "rw" : "ro", (unsigned)KEBAB_INST_CHUNK);
	printf("INST? wire %llu/%llu entries %u/%u ok %u skip %u fail %u\n",
	    (unsigned long long)ki.wire_seen, (unsigned long long)ki.wire_total,
	    ki.entries_done, ki.entries_total, ki.ok, ki.skipped, ki.failed);
	/*
	 * The on-disk header, not the mounted statfs: this is the number that
	 * moves only when HFS+ has actually written the allocation bitmap and
	 * the volume header.  Do not issue a raw read while a stream is live.
	 */
	if (!ki.active && ki_read_vh(&vh_bsize, &vh_blocks, &vh_free) == 0) {
		printf("INST? vh bsize %u blocks %u free %u\n", vh_bsize,
		    vh_blocks, vh_free);
	}
	if (!rw) {
		printf("INST? the root volume is read-only; boot with rootrw=1\n");
	} else {
		uint64_t freeb = 0, totalb = 0;

		if (ki_space(&freeb, &totalb) == 0) {
			struct vfsstatfs *sfs = vfs_statfs(rootvnode->v_mount);

			printf("INST? space %llu free of %llu bytes\n",
			    (unsigned long long)freeb,
			    (unsigned long long)totalb);
			if (sfs != NULL) {
				printf("INST? bsize %u blocks %llu free %llu\n",
				    sfs->f_bsize,
				    (unsigned long long)sfs->f_blocks,
				    (unsigned long long)sfs->f_bavail);
			}
		}
	}
}

/*
 * INST CHECK <path>: what the destination has now, so the host can pack a
 * resumed stream without the payloads that are already correct.
 */
void
kebab_install_check(const char *path)
{
	vfs_context_t ctx = vfs_context_current();
	uint64_t size = 0;
	uint8_t digest[32];
	char hex[65];
	size_t len;
	int error;

	if (!ki_gate()) {
		return;
	}
	len = strlen(path);
	if (!ki_path_ok_any(path, (uint32_t)len)) {
		printf("INST CHECK %s refused\n", path);
		return;
	}
	if (ki.io == NULL) {
		ki.io = kalloc_data(KI_IO_BUF, Z_WAITOK);
		if (ki.io == NULL) {
			printf("INST CHECK %s no buffer\n", path);
			return;
		}
	}
	error = ki_hash_file(path, &size, digest, ctx);
	if (error == ENOENT) {
		printf("INST CHECK %s MISSING\n", path);
		return;
	}
	if (error) {
		printf("INST CHECK %s ERROR %d\n", path, error);
		return;
	}
	for (uint32_t i = 0; i < 32; i++) {
		ki_hex08(&hex[i * 2], digest[i]);
	}
	hex[64] = 0;
	printf("INST CHECK %s %llu %s\n", path, (unsigned long long)size, hex);
}

#endif /* KEBAB */
