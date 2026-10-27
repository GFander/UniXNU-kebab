/** @file
  Kebab firmware log: a fixed physical page shared by the firmware and XNU.

  The framebuffer serial port library appends every byte it renders to the
  ring buffer, so the log the user sees on screen is also kept in memory and
  survives into the kernel.  The range is declared EfiReservedMemoryType by the
  "Kebab FwLog" descriptor in kebabPkg's MemoryMapLib and the header is written
  by KebabFwLogDxe; until the magic is set the append is a no-op.
**/
#ifndef KEBAB_FW_LOG_H
#define KEBAB_FW_LOG_H

#include <Uefi.h>

#define KEBAB_FWLOG_PA      0xF44C0000ULL
#define KEBAB_FWLOG_PAGES   64u          /* 256 KiB */
#define KEBAB_FWLOG_SIZE    (KEBAB_FWLOG_PAGES * 0x1000u)
#define KEBAB_FWLOG_MAGIC   0x474C464Bu  /* 'KFLG' */

#define KEBAB_FWLOG_DATA_SIZE (KEBAB_FWLOG_SIZE - 16u)

typedef struct {
  UINT32 Magic;
  UINT32 Size;
  UINT64 Head;                 /* total bytes ever written */
  UINT8  Data[KEBAB_FWLOG_DATA_SIZE];
} KEBAB_FWLOG;

#endif
