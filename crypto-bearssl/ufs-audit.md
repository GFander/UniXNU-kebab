The three patches below are based on the supplied pristine `xnu-xnu-8796.141.3` tree. Apply them in this order. They contain only the completed driver, its build-list entry, and the BSD root-device hook; the input tree and the device repository were not edited.

| Order | Patch | Change |
|---|---|---|
| 1 | `0070-kebab-ufs-driver.patch` | Full, read-only `bsd/dev/kebab_ufs.c`, entirely inside `#if defined(KEBAB)`. |
| 2 | `0071-kebab-ufs-build.patch` | Add the driver after `bsd/dev/memdev.c` in `bsd/conf/files`. |
| 3 | `0072-kebab-ufs-rootdev.patch` | Resolve `rd=ufsroot0` directly, with a C-linkage declaration inside the existing `extern "C"` region. |

From a pristine checkout, using the patch directory as appropriate:

```sh
git apply /path/to/out/0070-kebab-ufs-driver.patch
git apply /path/to/out/0071-kebab-ufs-build.patch
git apply /path/to/out/0072-kebab-ufs-rootdev.patch
```

These are complete patches, not incremental fixes to the owner's preliminary port. All driver corrections are folded into 0070. Non-KEBAB builds compile an empty driver object and omit the root hook. No assembly startup, pmap, interrupt, clock, PHY, power-domain, or RAMDisk code is changed.

The exact additional entries in the existing `[payload].kernel_args` array in device `board.toml` are:

```toml
  "rd=ufsroot0",
  "ufsroot=userdata",
```

This gives `rd=ufsroot0 ufsroot=userdata`. Keep the existing diagnostic boot arguments. `ufsroot` is a case-sensitive GPT partition name, not a filesystem volume UUID. The driver uses LUN 0, verifies 4096-byte device blocks with READ CAPACITY(10), and initially exposes 512-byte BSD logical sectors. The existing disk ioctl convention also permits a filesystem to select 1024, 2048 or 4096-byte logical sectors. The raw character interface is `/dev/rufsroot0`.

Add this node to the **root's children** in `scripts/make-kebab-adt.py`, beside nodes such as `pram`/`arm-io`:

```python
node('kebab-ufs', [
    ('reg', struct.pack('<QQ', 0x01D84000, 0x30000)),
]),
```

This ADT property is exactly 16 bytes: two little-endian 64-bit values, absolute base and size. It is not a Linux DTS big-endian cell encoding and not an offset into `arm-io`. The driver copies the property before reading it, because ADT property data need only have four-byte alignment. `/arm-io/ufshc` is left alone and is never consulted. `/chosen/root-matching` may remain: the explicit `rd=ufsroot0` branch releases that matching dictionary and skips IOMedia matching. Do not emit `RAMDisk` in `/chosen/memory-map`; also disable any packed/data RAMDisk option or loader feature that recreates one. No RAMDisk is used by this series.

The API audit against the supplied 8796 headers and implementation is:

| API | 8796 declaration and result |
|---|---|
| `ml_io_map` | `osfmk/arm/machine_routines.h:703`: `vm_offset_t ml_io_map(vm_offset_t, vm_size_t)`. Hidden unless `PEXPERT_KERNEL_PRIVATE` or `MACH_KERNEL_PRIVATE`; BSD's `MACH_KERNEL` is not the same macro. Retain an explicit matching extern in the driver, without enabling Mach-private headers globally. Map only the fixed 0x30000-byte controller range. |
| `kmem_alloc_contig` | `osfmk/vm/vm_kern.h:686`: eight arguments `(vm_map_t, vm_offset_t *, vm_size_t, vm_offset_t mask, ppnum_t max_pnum, ppnum_t pnum_mask, kma_flags_t, vm_tag_t)`, returning `kern_return_t`. This tree implements it as an inline wrapper over `kmem_alloc_contig_guard`. The reference call shape and `KMA_DATA | KMA_ZERO | KMA_PERMANENT` are available. The port additionally limits `max_pnum` when CAP.64AS is clear. |
| `pmap_find_phys` | `osfmk/arm/pmap/pmap.h:494`: `ppnum_t pmap_find_phys(pmap_t, addr64_t)`. Implementation in `pmap.c:6573` returns `atop(pmap_find_pa(...))`. The explicit extern matches 8796 and the existing BSD memdev convention. |
| `ptoa_64` | `osfmk/mach/vm_param.h:100`: casts to `uint64_t` then shifts by `PAGE_SHIFT`. Include `<mach/vm_param.h>` explicitly. It uses 14, not 12, for this 16 KiB kernel configuration. |
| Cache operations | `osfmk/arm/caches_internal.h:43` declares `void flush_dcache64(addr64_t, unsigned, int)` only under `MACH_KERNEL_PRIVATE`. The symbol exists, but the reference include alone does not expose it to BSD. **There is no `invalidate_dcache64` in 8796.** `FlushPoC_DcacheRegion(vm_offset_t, size_t)` is the available VA clean-and-invalidate helper; `CleanPoC_DcacheRegion_Force` is clean-only, not an invalidate replacement. See the cache implementation decision below. |
| `bdevsw_add` | `bsd/sys/conf.h:305`: `int bdevsw_add(int, const struct bdevsw *)`; unchanged. Check its result and remove the block registration if character registration fails. |
| `cdevsw_add_with_bdev` | Same header, line 309: `int cdevsw_add_with_bdev(int, const struct cdevsw *, int)`; unchanged. Both reference switch-table layouts and callback signatures exist. |
| `devfs_make_node` | `bsd/miscfs/devfs/devfs.h:111`: `void *devfs_make_node(dev_t, int, uid_t, gid_t, int, const char *, ...)`; unchanged. Check both handles, clean up partial registration, and use mode 0440. |
| Device tree | `pexpert/pexpert/device_tree.h:180,249`: `SecureDTLookupEntry(const DTEntry, const char *, DTEntry *)` and `SecureDTGetProperty(const DTEntry, const char *, void const **, unsigned int *)`, both returning `int`; compare against `kSuccess`. Use an actual `const void *` property variable instead of a pointer-to-pointer cast. |
| Boot argument | `pexpert/pexpert/pexpert.h:337`: `boolean_t PE_parse_boot_argn(const char *, void *, int)`; unchanged. Keep the default, terminate the buffer explicitly, and reject empty or over-36-character names. |
| Locks | `<kern/locks.h>` includes the lock-group and mutex headers. `LCK_GRP_DECLARE` and `LCK_MTX_DECLARE` exist under `XNU_KERNEL_PRIVATE`, which the in-tree build sets; their LOCKS startup initialization precedes BSD root discovery. A mutex serializes initialization, descriptor/bounce ownership, reads and block-size changes. Do not use a spinlock over the polled transfers. |
| BSD buffer/raw I/O | `buf_*`, `physio(..., int blocksize)`, `minphys` and the reference disk ioctls exist. Add `<sys/fcntl.h>` for `FWRITE`. Replace ioctl pointer casts with `memcpy` to satisfy 8796's `-Wcast-align` and permit unaligned argument storage. Reject negative/overflowing block offsets before multiplication. |

The supplied `negroni_ufs.c` also began with `@@ -0,0 +1,536 @@`, a patch hunk marker rather than C. It has been removed. The guard is `KEBAB`, not `ARM64_BOARD_CONFIG_KEBAB`; ensure the existing machine configuration actually defines `KEBAB`.

Physical contiguity is a real allocator guarantee here. `kmem_alloc_contig_guard` calls `cpm_allocate`, which obtains an ordered contiguous page list through `vm_page_find_contiguous`; `cpm_allocate` itself asserts `vm_page_verify_contiguous`. The kernel allocation then wires/maps those pages. The driver's 0x104000-byte permanent allocation is exactly **65 × 16 KiB**, with no guard/pageable flags: transfer list at +0, command descriptor at +0x1000, bounce buffer at +0x4000. The port checks alignment, translates every page, and requires `ptoa_64(page) == base_pa + offset` before publishing any DMA address. Thus the four maximum-size PRDT entries can each span 256 KiB physically. CAP.64AS determines whether addresses above 4 GiB are allowed by the host.

Cache ownership is explicit. Before submission, every descriptor/data cache line is cleaned and invalidated with `dc civac`; after the doorbell clears, it is invalidated with `dc ivac` before CPU inspection/copying. The helper uses kernel VAs, reads the cache-line size from CTR_EL0, and issues `dsb sy` barriers. There are no CPU writes to DMA storage while a request is pending, and no unrelated allocations share these cache lines. MMIO accessors also enforce ordering. This deliberately avoids 8796's Apple-coherent build shortcuts: `FlushPoC_DcacheRegion` can compile to a barrier only, while physical `flush_dcache64` also depends on the platform's physical-to-virtual mapping/cache implementation. No nonexistent cache helper is declared or linked.

On timeout, fatal host error, invalid OCS or malformed transport response, the driver stops issuing commands. It attempts slot cancellation and stops the transfer list, but **does not assume either operation has drained DMA**. The permanent DMA memory is never freed or reused; initialization failure is latched so a second root lookup cannot allocate/restart behind a still-running transaction. A bounded retry is allowed only for a valid TEST UNIT READY unit-attention response.

The SM8250 review found no required SM8450-only register operation in this approach. The local `UFS-Whole-Device-Review.md` records kebab's earlier `VER=0x300`, `CAP=0x1587031F`, `HCE=1`, `HCS=0x10F`; these are historical observations, not a measurement from this run. The driver prints current VER/CAP/HCE/HCS and accepts only 0x200, 0x210, 0x300 or 0x310. It rejects 1.x, unknown revisions and 4.x/MCQ rather than guessing. Qualcomm's older version-register workaround is not blindly applied to SM8250. The version-dependent command-type definitions and Qualcomm quirk scope were checked against [Linux v6.1 ufshci.h](https://github.com/torvalds/linux/blob/v6.1/include/ufs/ufshci.h) and [ufs-qcom.c](https://github.com/torvalds/linux/blob/v6.1/drivers/ufs/host/ufs-qcom.c).

For takeover, HCE.EN must be set and HCS must report device-present and transfer-list-ready. Both transfer and task doorbells must be empty, with no fatal status. IE and interrupt aggregation are disabled; both lists are stopped and their stop status is polled. The new transfer-list base is written and read back while stopped, then RSR is started and checked. HCE is never toggled. This preserves firmware's link configuration.

The UTRD remains little-endian, command type 1 for the accepted revisions, data-in direction 2, and interrupt-command bit set with host interrupts disabled. UCD alignment is 128 bytes; response/PRDT offsets and response allocation length are DWORD units, while PRDT length is an entry count. OCS is the low nibble: 0xf initially, 0 for success. COMMAND/RESPONSE UPIUs use byte-defined headers and big-endian transfer length/CDB fields; tag 0 matches slot 0. Responses must match LUN/tag/type and have successful response/status with zero residual and no under/overflow flags. These encodings were cross-checked against [Linux's UFS command implementation](https://github.com/torvalds/linux/blob/v6.1/drivers/ufs/core/ufshcd.c) and [UPIU definitions](https://github.com/torvalds/linux/blob/v6.1/include/ufs/ufs.h).

The driver additionally verifies device capacity before GPT reads, prevents entry-count multiplication overflow, skips unused GPT entries, matches the complete ASCII name encoded in UTF-16LE, and bounds the chosen partition by the usable/device ranges. It retains the reference's deliberately small primary-GPT parser: 128-byte entries, at most 1 MiB of entries, no backup-GPT recovery or CRC verification. READ(10) and READ CAPACITY(10) suffice for this phone; devices requiring READ CAPACITY(16) are rejected.

The handoff approach is plausible on kebab, but **link-up does not prove that the kernel's new DMA allocation is reachable**. The local bring-up notes report that the vendor HALIOMMU domain had to be configured/attached before UFS DMA worked, even when registers appeared to indicate bypass. Firmware must preserve working access for the entire kernel allocation (potentially above 4 GiB), not merely a mapping of its former DMA pool. Physical contiguity and CAP.64AS cannot establish that IOMMU/firewall fact. Firmware must also stop its own UFS users and leave the controller/link/clocks/power alive when entering XNU. MMIO and DMA failures caused by a lost mapping may raise an SError rather than a recoverable command error.

If that handoff is not retained, the lowest-cost alternative is a firmware handoff change that leaves the initialized host and a valid physical-DMA domain in place. Supporting a restricted firmware DMA pool instead would require a reserved-memory/physical-allocation contract. A fully independent kernel driver would additionally need Qualcomm clock/reset/power/PHY control, link/device initialization and SMMU mapping, plus error recovery; that is a substantially larger platform-storage port, not a cache-helper substitution. This series makes no firmware calls after entry and does not implement those subsystems.

`userdata` must actually contain an unencrypted root filesystem layout supported by the loaded filesystem driver. This block driver neither converts Android ext4/F2FS/encrypted userdata nor mounts files embedded inside a partition, and it does not decrypt with ICE. Writes are rejected at open, strategy and raw-write entry points; `DKIOCISWRITABLE` reports false. Root discovery alone does not establish that the filesystem or later userspace boot is ready.

Validation completed on WSL Ubuntu with Clang 21.1.8:

- The full driver compiled to an arm64 Mach-O object with 16 KiB pages, KEBAB, BSD-private definitions, and the tree's warning set. Its disassembly contains the intended cache/barrier instructions and no PAC or FP/SIMD instructions.
- Non-KEBAB arm64 and x86_64 driver builds produce no executable code or external symbols.
- The isolated root-hook branch compiled as C++, and its undefined `_kebab_ufs_rootdev` symbol matches the C driver's definition. This is a branch/linkage check with a mock matching object, **not a full IOKit translation-unit build**.
- A host harness compiled the actual driver command, takeover, GPT, strategy and ioctl functions with simulated MMIO/kernel services under AddressSanitizer and UndefinedBehaviorSanitizer. It passed **1,040 assertions**, plus descriptor/PRDT validation on every simulated command. Cases include stuck lists, refused list base, timeout without reuse, transport errors, unit attention, residuals, capacity mismatch, corrupt GPT bounds, partial reads across device/bounce boundaries, unaligned ioctl arguments and write rejection. Cache instructions and physical hardware behavior are not simulated by this test.
- Each patch passed `git apply --check --whitespace=error-all`, applied in order to copies of the untouched target files, and produced exactly the staged source. Reverse-series applicability also passed. Input and patch SHA256 values are in `apply-validation.log`.

This is **not a full kernel build/link or a device boot**. The Linux object check uses real 8796 headers, staged option headers, XNU's own string header, VMAPPLE constants and `CONFIG_ROSETTA=1` to avoid the pristine pmap header's duplicate `reserved10` field when both PAC and Rosetta are disabled; PAC remains off. These validation settings do not modify the patches or replace your existing board/no-PAC changes. Clang-21-only diagnostics are disabled in the validation command; the baseline enum warning remains nonfatal under the tree's own flags. A full IOKit compile could not be completed here because generated MIG/IIG/DriverKit headers and Apple-specific compiler support are absent.

Reproduction scripts are one directory above this report: `build_patches.py`, `validate_compile.py` and `validate_behavior.py`. Their logs/object artifacts are under `../validation/`; concise results are copied to `validation-summary.txt` here. The remaining integration check is the normal KEBAB kernel build/link followed by a phone boot that verifies the printed DMA address is accepted, READ CAPACITY/GPT succeed, `BSD root: ufsroot0` appears, and the intended filesystem mounts read-only.
