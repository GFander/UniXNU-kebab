# kebab bring-up state (2026-10-25 02:00)

## What works

* 16 KiB xnu-8796 kernel builds with `blacktop/darwin-xnu-build` on macOS 14 and
  links; the payload volume assembles.
* The whole UEFI chain runs: ABL -> boot_b image -> Mu-Silicium -> UniXNU menu
  -> DarwinBoot -> ExitBootServices -> kernel (x0 = boot_args PA).
* `start.s` runs through: bootstrap page tables, framebuffer mapping, MMU
  enable (verified by a panel marker right after `msr sctlr_el1`).
* The kernel entered at its **identity** address reaches C code and completes
  `arm_init`, `PE_init_platform`, `ml_parse_cpu_topology`, `cpu_bootstrap` and
  `kernel_startup_bootstrap`, with the real device tree (ADT length 0x31c8)
  and a correct 19.2 MHz timebase.

## What blocks

`start.s` computes `lr` as the **kernel virtual address** of `arm_init` and
returns to it (`ret`). Execution never reaches `arm_init`'s first statement:

* a 60x24 probe at the top of `arm_init` never paints;
* a large red fault marker driven by a custom `vbar_el1` never paints either,
  so either no exception is taken or the vector itself is wrong.

The bootstrap KVA map is *not* the problem: walking it from C gives
`L1[0x7e0] = 0xc0178003 -> L2[0x003] = 0xc0000601`, i.e. kernel VA
`0xfffffe0007004000` maps to PA `0xc0004000`, exactly where the kernel sits.
`TCR_EL1 = 0x226511a511` (T0SZ = T1SZ = 17, TG1 = 16 KiB) is consistent with
that; `TTBR1 = 0xc0170000`.

## Next diagnostics, in order

1. **Verify the vector install**: read `vbar_el1` back right after `msr` and
   compare with the computed address; if the `adrp`+KVA conversion is wrong the
   "no fault" conclusion is worthless.
2. **Branch to a `b .` loop** placed in the same 4 KiB page as the `ret` and
   converted with the same `+x22-x23`, to prove that *any* KVA branch works.
3. If the branch mechanism works but `arm_init` does not run, compare the
   `LC_UNIXTHREAD` entry (`_start`) against the symbol used for `lr`; the
   identity run proves the C entry itself is fine.
4. Fall back plan: link the kernel at the **iOS** address
   (`0xfffffff007004000`) instead of the macOS one, so every address in the
   bootstrap path is inside the range the port was written for, then repeat.

## Flashing

`fastboot flash boot_b <image>`; the UniXNU firmware reads the payload from
`boot_b` regardless of the active slot.
