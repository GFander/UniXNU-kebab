/** @file
  Kebab RPMh probe (read-only).

  Locates the RPMh protocol published by RpmhDxe and answers the two questions
  the earlier probe could not:

    - Is the interface pointer itself the function table, or does it point at
      a structure whose first words point somewhere else?  The first eight raw
      64-bit words at the interface are printed as rva= or not-code, then the
      interface is dumped as a table, then word[0] and word[1] are dumped as
      candidate tables when they resolve inside the owning image.
    - Does the picture change between the 5 s timer and EndOfDxe?  Every dump
      carries the tag "t5" or "eod" so the two can be compared line by line.

  The GUID, the LocateProtocol status and the interface pointer are printed on
  every dump, including a failed locate.  The owning loaded image is resolved
  once and its base and size are cached; no pointer classification enumerates
  handles again.

  Nothing is written to RPMh/RSC and no protocol function is called.

  Output uses DEBUG at ERROR level: the platform sets
  PcdDebugPrintErrorLevel to 0x80000000, so only errors are emitted.
**/
#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DebugLib.h>
#include <Protocol/LoadedImage.h>
#include <Guid/EventGroup.h>

#define KRP_RAW_WORDS   8u
#define KRP_TABLE_MAX   64u
#define KRP_TIMER_5S    50000000ULL   /* 5 s in 100 ns units */

STATIC EFI_GUID mKebabRpmhProtocolGuid = {
  0xe1d0b573, 0xf29f, 0x495d,
  { 0x93, 0x52, 0xca, 0x0d, 0xd4, 0x2a, 0x0a, 0x74 }
};

STATIC EFI_EVENT mTimerEvent;
STATIC EFI_EVENT mEndOfDxeEvent;

STATIC UINT64  mImageBase;
STATIC UINT64  mImageSize;
STATIC BOOLEAN mImageResolved;

/**
  Classify Address against the cached owning image.

  @param[in]  Address  Value to classify.
  @param[out] Rva      Image-relative offset when the function returns TRUE.

  @retval TRUE   Address lies inside the cached image.
  @retval FALSE  It does not, or no image has been resolved yet.
**/
STATIC
BOOLEAN
KrpRvaOf (
  IN  UINT64  Address,
  OUT UINT64  *Rva
  )
{
  if (!mImageResolved || Address < mImageBase) {
    return FALSE;
  }
  if (Address - mImageBase >= mImageSize) {
    return FALSE;
  }
  *Rva = Address - mImageBase;
  return TRUE;
}

/**
  Resolve and cache the loaded image that owns Interface.

  The handle list is walked at most once per successful resolution; every
  later address test uses mImageBase/mImageSize only.

  @param[in] Interface  Protocol interface pointer.
  @param[in] Tag        "t5" or "eod", for the error line only.
**/
STATIC
VOID
KrpResolveImage (
  IN VOID         *Interface,
  IN CONST CHAR8  *Tag
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  *Handles;
  UINTN       HandleCount;
  UINTN       Index;
  UINT64      Address;

  if (mImageResolved) {
    return;
  }

  Address     = (UINT64)(UINTN)Interface;
  Handles     = NULL;
  HandleCount = 0;
  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiLoadedImageProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "KRP: %a cannot enumerate loaded images: %r\n",
      Tag, Status));
    return;
  }

  for (Index = 0; Index < HandleCount; Index++) {
    EFI_LOADED_IMAGE_PROTOCOL  *Image;
    UINT64                     Base;
    UINT64                     Size;

    Image  = NULL;
    Status = gBS->HandleProtocol (Handles[Index], &gEfiLoadedImageProtocolGuid,
                    (VOID **)&Image);
    if (EFI_ERROR (Status) || Image == NULL ||
        Image->ImageBase == NULL || Image->ImageSize == 0) {
      continue;
    }
    Base = (UINT64)(UINTN)Image->ImageBase;
    Size = (UINT64)Image->ImageSize;
    if (Address >= Base && Address - Base < Size) {
      mImageBase     = Base;
      mImageSize     = Size;
      mImageResolved = TRUE;
      break;
    }
  }

  FreePool (Handles);

  if (!mImageResolved) {
    DEBUG ((DEBUG_ERROR,
      "KRP: %a interface 0x%lx is not inside any loaded image\n",
      Tag, Address));
  }
}

/**
  Number of 64-bit entries that may be read from Table without leaving the
  owning image.  Outside a known image the probe still reads the requested
  maximum, which is what makes an unexpected pointer visible.

  @param[in] Table  Candidate table pointer.

  @return Entry count, never more than KRP_TABLE_MAX.
**/
STATIC
UINT32
KrpEntryCount (
  IN UINT64  Table
  )
{
  UINT64  Rva;
  UINT64  Bytes;

  if (!KrpRvaOf (Table, &Rva)) {
    return KRP_TABLE_MAX;
  }
  Bytes = mImageSize - Rva;
  if (Bytes / sizeof (UINT64) < KRP_TABLE_MAX) {
    return (UINT32)(Bytes / sizeof (UINT64));
  }
  return KRP_TABLE_MAX;
}

/**
  Print one 64-bit entry as "rva=" or "not-code".

  @param[in] Tag    Dump tag.
  @param[in] Table  Table name, e.g. "raw", "iface" or "cand[0]".
  @param[in] Index  Entry index.
  @param[in] Value  Entry value.
**/
STATIC
VOID
KrpPrintEntry (
  IN CONST CHAR8  *Tag,
  IN CONST CHAR8  *Table,
  IN UINT32       Index,
  IN UINT64       Value
  )
{
  UINT64  Rva;

  if (KrpRvaOf (Value, &Rva)) {
    DEBUG ((DEBUG_ERROR, "KRP: %a %a[%u] 0x%lx rva=0x%lx\n",
      Tag, Table, Index, Value, Rva));
  } else {
    DEBUG ((DEBUG_ERROR, "KRP: %a %a[%u] 0x%lx not-code\n",
      Tag, Table, Index, Value));
  }
}

/**
  Dump the protocol once under the given tag.

  @param[in] Tag  "t5" for the timer dump, "eod" for EndOfDxe.
**/
STATIC
VOID
KrpDump (
  IN CONST CHAR8  *Tag
  )
{
  EFI_STATUS  Status;
  VOID        *Interface;
  UINT64      Words[KRP_RAW_WORDS];
  UINT64      Table;
  UINT64      Rva;
  UINT32      Index;
  UINT32      Candidate;
  UINT32      Entry;
  UINT32      Count;

  Interface = NULL;
  Status = gBS->LocateProtocol (&mKebabRpmhProtocolGuid, NULL, &Interface);
  DEBUG ((DEBUG_ERROR, "KRP: %a guid=%g status=%r (0x%lx) iface=0x%lx\n",
    Tag, &mKebabRpmhProtocolGuid, Status, (UINT64)Status,
    (UINT64)(UINTN)Interface));
  if (EFI_ERROR (Status) || Interface == NULL) {
    DEBUG ((DEBUG_ERROR, "KRP: %a locate failed, nothing to dump\n", Tag));
    return;
  }

  KrpResolveImage (Interface, Tag);

  if (mImageResolved) {
    DEBUG ((DEBUG_ERROR, "KRP: %a image base=0x%lx size=0x%lx\n",
      Tag, mImageBase, mImageSize));
    if (KrpRvaOf ((UINT64)(UINTN)Interface, &Rva)) {
      DEBUG ((DEBUG_ERROR, "KRP: %a iface rva=0x%lx\n", Tag, Rva));
    } else {
      DEBUG ((DEBUG_ERROR, "KRP: %a iface is outside the cached image\n",
        Tag));
    }
  } else {
    DEBUG ((DEBUG_ERROR,
      "KRP: %a image=unknown, no pointer can be classified as code\n", Tag));
  }

  DEBUG ((DEBUG_ERROR, "KRP: %a raw words (8)\n", Tag));
  for (Index = 0; Index < KRP_RAW_WORDS; Index++) {
    Words[Index] = ((UINT64 *)Interface)[Index];
    KrpPrintEntry (Tag, "raw", Index, Words[Index]);
  }

  Count = KrpEntryCount ((UINT64)(UINTN)Interface);
  DEBUG ((DEBUG_ERROR, "KRP: %a iface as table, %u entries\n", Tag, Count));
  for (Index = 0; Index < Count; Index++) {
    KrpPrintEntry (Tag, "iface", Index, ((UINT64 *)Interface)[Index]);
  }

  /*
   * The whole point of the raw dump: if word[0] or word[1] points at another
   * table, dump that table as well instead of assuming it is a function.
   */
  for (Candidate = 0; Candidate < 2; Candidate++) {
    Table = Words[Candidate];
    if (!KrpRvaOf (Table, &Rva)) {
      DEBUG ((DEBUG_ERROR, "KRP: %a cand[%u] 0x%lx not-code, skipped\n",
        Tag, Candidate, Table));
      continue;
    }
    Count = KrpEntryCount (Table);
    DEBUG ((DEBUG_ERROR,
      "KRP: %a cand[%u] table=0x%lx rva=0x%lx entries=%u\n",
      Tag, Candidate, Table, Rva, Count));
    for (Entry = 0; Entry < Count; Entry++) {
      KrpPrintEntry (Tag, Candidate == 0 ? "cand[0]" : "cand[1]", Entry,
        ((UINT64 *)Table)[Entry]);
    }
  }
}

/**
  One-shot timer notification: dump as "t5".
**/
STATIC
VOID
EFIAPI
KrpTimerNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  gBS->CloseEvent (Event);
  mTimerEvent = NULL;
  KrpDump ("t5");
}

/**
  EndOfDxe event-group notification: dump as "eod".
**/
STATIC
VOID
EFIAPI
KrpEndOfDxeNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  KrpDump ("eod");
}

/**
  Arm both dumps: a 5 s one-shot timer and the EndOfDxe event group.

  @param[in] ImageHandle  Standard DXE entry parameter.
  @param[in] SystemTable  Standard DXE entry parameter.

  @retval EFI_SUCCESS  Both dumps are at least attempted.
  @retval Others       The timer could not be created or armed.
**/
EFI_STATUS
EFIAPI
KebabRpmhProbeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  Status = gBS->CreateEvent (EVT_TIMER, TPL_CALLBACK, KrpTimerNotify, NULL,
                  &mTimerEvent);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "KRP: CreateEvent(timer) failed: %r\n", Status));
    return Status;
  }

  Status = gBS->SetTimer (mTimerEvent, TimerRelative, KRP_TIMER_5S);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "KRP: SetTimer failed: %r\n", Status));
    gBS->CloseEvent (mTimerEvent);
    mTimerEvent = NULL;
    return Status;
  }

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  KrpEndOfDxeNotify,
                  NULL,
                  &gEfiEndOfDxeEventGroupGuid,
                  &mEndOfDxeEvent
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "KRP: CreateEventEx(EndOfDxe) failed: %r\n",
      Status));
    /* The t5 dump still runs; say that eod is missing and keep the probe. */
  }

  DEBUG ((DEBUG_ERROR, "KRP: probe armed: t5 in 5 s, eod on EndOfDxe\n"));
  return EFI_SUCCESS;
}
