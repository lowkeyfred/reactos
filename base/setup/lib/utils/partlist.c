/*
 * PROJECT:     ReactOS Setup Library
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Partition list functions
 * COPYRIGHT:   Copyright 2003-2019 Casper S. Hornstrup (chorns@users.sourceforge.net)
 *              Copyright 2018-2019 Hermes Belusca-Maito
 */

#include "precomp.h"
#include <ntddscsi.h>

#include "partlist.h"
#include "fsrec.h"
#include "registry.h"

#define NDEBUG
#include <debug.h>

//#define DUMP_PARTITION_TABLE

#include <pshpack1.h>

typedef struct _REG_DISK_MOUNT_INFO
{
    ULONG Signature;
    LARGE_INTEGER StartingOffset;
} REG_DISK_MOUNT_INFO, *PREG_DISK_MOUNT_INFO;

#include <poppack.h>


/* FUNCTIONS ****************************************************************/

#ifdef DUMP_PARTITION_TABLE
static
VOID
DumpPartitionTable(
    PDISKENTRY DiskEntry)
{
    PPARTITION_INFORMATION PartitionInfo;
    ULONG i;

    DbgPrint("\n");
    DbgPrint("Index  Start         Length        Hidden      Nr  Type  Boot  RW\n");
    DbgPrint("-----  ------------  ------------  ----------  --  ----  ----  --\n");

    for (i = 0; i < DiskEntry->LayoutBuffer->PartitionCount; i++)
    {
        PartitionInfo = &DiskEntry->LayoutBuffer->PartitionEntry[i];
        DbgPrint("  %3lu  %12I64u  %12I64u  %10lu  %2lu    %2x     %c   %c\n",
                 i,
                 PartitionInfo->StartingOffset.QuadPart / DiskEntry->BytesPerSector,
                 PartitionInfo->PartitionLength.QuadPart / DiskEntry->BytesPerSector,
                 PartitionInfo->HiddenSectors,
                 PartitionInfo->PartitionNumber,
                 PartitionInfo->PartitionType,
                 PartitionInfo->BootIndicator ? '*': ' ',
                 PartitionInfo->RewritePartition ? 'Y': 'N');
    }

    DbgPrint("\n");
}
#endif


ULONGLONG
AlignDown(
    _In_ ULONGLONG Value,
    _In_ ULONG Alignment)
{
    ULONGLONG Temp;

    Temp = Value / Alignment;

    return Temp * Alignment;
}

// Unused
ULONGLONG
AlignUp(
    _In_ ULONGLONG Value,
    _In_ ULONG Alignment)
{
    ULONGLONG Temp, Result;

    Temp = Value / Alignment;

    Result = Temp * Alignment;
    if (Value % Alignment)
        Result += Alignment;

    return Result;
}

// Unused
ULONGLONG
RoundingDivide(
    _In_ ULONGLONG Dividend,
    _In_ ULONGLONG Divisor)
{
    return (Dividend + Divisor / 2) / Divisor;
}



#define ADJ_LIST_ENTRY(ListEntry, Direction) \
    ((Direction) ? (ListEntry)->Flink : (ListEntry)->Blink)

#define GET_ADJ_RECORD_IMPL(pAdjRecord, ListHead, Record, RecordType, Field, Direction) \
do { \
    PLIST_ENTRY _ListEntry = ((Record) ? &(Record)->Field : (ListHead)); \
    _ListEntry = ADJ_LIST_ENTRY(_ListEntry, (Direction)); \
    (pAdjRecord) = ((_ListEntry == (ListHead)) \
        ? NULL : CONTAINING_RECORD(_ListEntry, RecordType, Field)); \
} while (0)

/**
 * @brief
 * Retrieves the adjacent (next or previous) disk in a given list.
 *
 * @param[in]   DiskListHead
 * The list where to search for the adjacent disk.
 *
 * @param[in]   DiskEntry
 * The disk where to continue the enumeration from (or NULL
 * to retrieve the first or last one).
 *
 * @param[in]   Direction
 * TRUE or FALSE to search the next or previous disk, respectively.
 *
 * @return  The disk, or NULL if not found.
 **/
static
PDISKENTRY
GetAdjDiskListEntry(
    _In_ PLIST_ENTRY DiskListHead,
    _In_opt_ PDISKENTRY DiskEntry,
    _In_ BOOLEAN Direction)
{
    GET_ADJ_RECORD_IMPL(DiskEntry, DiskListHead, DiskEntry, DISKENTRY, ListEntry, Direction);
    return DiskEntry;
}

/**
 * @brief
 * Retrieves the adjacent (next or previous) disk region in a given list.
 *
 * @param[in]   PartListHead
 * The list where to search for the adjacent disk region.
 *
 * @param[in]   PartEntry
 * The disk region where to continue the enumeration from
 * (or NULL to retrieve the first or last one).
 *
 * @param[in]   Direction
 * TRUE or FALSE to search the next or previous region, respectively.
 *
 * @return  The disk region, or NULL if not found.
 **/
static
PPARTENTRY
GetAdjPartListEntry( // GetAdjRegionEntry
    _In_ PLIST_ENTRY PartListHead,
    _In_opt_ PPARTENTRY PartEntry,
    _In_ BOOLEAN Direction)
{
    GET_ADJ_RECORD_IMPL(PartEntry, PartListHead, PartEntry, PARTENTRY, ListEntry, Direction);
    return PartEntry;
}


/**
 * @brief
 * Finds the next disk region in order of appearance on a given MBR disk,
 * starting at the specified region.
 *
 * The function goes into extended partitions and enumerate
 * the logical regions inside.
 *
 * @param[in]   CurrentDisk
 * Specifies the disk where to find the next disk region.
 * If CurrentPart != NULL, the disk CurrentPart belongs must be the same.
 *
 * @param[in]   CurrentPart
 * The disk region where to restart the search, or NULL for
 * starting from the beginning of the specified disk.
 *
 * @return
 * The next disk region, if any, or NULL when all regions on the disk
 * have been enumerated.
 *
 * @see GetPrevMBRDiskRegionByOrder().
 **/
static
PPARTENTRY
GetNextMBRDiskRegionByOrder(
    _In_ PDISKENTRY CurrentDisk,
    _In_opt_ PPARTENTRY CurrentPart)
{
    /* This helper is for MBR disks only! */
    ASSERT(CurrentDisk->DiskStyle == PARTITION_STYLE_MBR);

    /* If no region is given, restart the search at the top of the primary list */
    if (CurrentPart)
    {
        ASSERT(CurrentDisk == CurrentPart->DiskEntry);

        /* Check for extended and logical partitions */
        if ((CurrentPart == CurrentDisk->ExtendedPartition) ||
             CurrentPart->LogicalPartition)
        {
            /* If this is the single extended partition,
             * go to the first logical region */
            if (CurrentPart == CurrentDisk->ExtendedPartition)
                CurrentPart = NULL;
            /* Else, this is a logical region */

            /* The first or next region is in the logical list */
            CurrentPart = GetAdjPartListEntry(&CurrentDisk->LogicalPartListHead,
                                              CurrentPart, TRUE);
            if (CurrentPart)
                return CurrentPart;

            /* We are at the end of the logical list: go to the next
             * (primary) region following the extended partition */
            CurrentPart = CurrentDisk->ExtendedPartition;
        }
    }

    /* This is a primary region, go to the next one within the primary list */
    return GetAdjPartListEntry(&CurrentDisk->PrimaryPartListHead,
                               CurrentPart, TRUE);
}

/**
 * @brief
 * Finds the previous disk region in order of appearance on a given MBR disk,
 * starting at the specified partition.
 *
 * The function goes into extended partitions and enumerate
 * the logical regions inside.
 *
 * @param[in]   CurrentDisk
 * Specifies the disk where to find the next disk region.
 * If CurrentPart != NULL, the disk CurrentPart belongs must be the same.
 *
 * @param[in]   CurrentPart
 * The disk region where to restart the search, or NULL for
 * starting from the end of the specified disk.
 *
 * @return
 * The previous disk region, if any, or NULL when all regions on the disk
 * have been enumerated.
 *
 * @see GetNextMBRDiskRegionByOrder().
 **/
static
PPARTENTRY
GetPrevMBRDiskRegionByOrder(
    _In_ PDISKENTRY CurrentDisk,
    _In_opt_ PPARTENTRY CurrentPart)
{
    /* This helper is for MBR disks only! */
    ASSERT(CurrentDisk->DiskStyle == PARTITION_STYLE_MBR);

    /* If no region is given, restart the search at the bottom of the primary list */
    if (CurrentPart)
    {
        ASSERT(CurrentDisk == CurrentPart->DiskEntry);

        /* Check for logical partitions */
        if (CurrentPart->LogicalPartition)
        {
            /* The previous region is in the logical list */
            CurrentPart = GetAdjPartListEntry(&CurrentDisk->LogicalPartListHead,
                                              CurrentPart, FALSE);
            if (!CurrentPart)
            {
                /* We are at the beginning of the logical list:
                 * go back to the extended partition (it must be
                 * there since we had a logical region) */
                CurrentPart = CurrentDisk->ExtendedPartition;
                ASSERT(CurrentPart);
            }
            /* Else, we are getting the previous logical region */

            return CurrentPart;
        }
    }

    /* This is a primary region, go to the previous one within the primary list */
    CurrentPart = GetAdjPartListEntry(&CurrentDisk->PrimaryPartListHead,
                                      CurrentPart, FALSE);

    /* Check for extended partition and look at the last logical region.
     * If there are no logical regions, stay on the extended partition. */
    if (CurrentPart && (CurrentPart == CurrentDisk->ExtendedPartition))
    {
        PPARTENTRY LastLogical =
            GetAdjPartListEntry(&CurrentDisk->LogicalPartListHead,
                                NULL, FALSE);
        if (LastLogical)
            CurrentPart = LastLogical;
    }

    return CurrentPart;
}


static
PPARTENTRY
GetNextMBRDiskRegionByType(
    _In_ PDISKENTRY CurrentDisk,
    _In_opt_ PPARTENTRY CurrentPart)
{
    /* This helper is for MBR disks only! */
    ASSERT(CurrentDisk->DiskStyle == PARTITION_STYLE_MBR);

    if (CurrentPart)
        ASSERT(CurrentDisk == CurrentPart->DiskEntry);

    /* Check for primary regions first */
    if (!CurrentPart || !CurrentPart->LogicalPartition)
    {
        CurrentPart = GetAdjPartListEntry(&CurrentDisk->PrimaryPartListHead,
                                          CurrentPart, TRUE);
        if (CurrentPart)
            return CurrentPart;
    }

    /* If this was a logical region, or if no more primary regions:
     * now, check for logical regions */
    if (CurrentPart && CurrentPart->LogicalPartition)
        ASSERT(CurrentDisk->ExtendedPartition);

    /* If the new CurrentPart == NULL, we exhausted
     * all the regions and we are done for this disk */
    // if (CurrentDisk->ExtendedPartition)
    return GetAdjPartListEntry(&CurrentDisk->LogicalPartListHead,
                               CurrentPart, TRUE);
}

static
PPARTENTRY
GetPrevMBRDiskRegionByType(
    _In_ PDISKENTRY CurrentDisk,
    _In_opt_ PPARTENTRY CurrentPart)
{
    /* This helper is for MBR disks only! */
    ASSERT(CurrentDisk->DiskStyle == PARTITION_STYLE_MBR);

    if (CurrentPart)
        ASSERT(CurrentDisk == CurrentPart->DiskEntry);

    /* Check for logical regions first */
    if (!CurrentPart || CurrentPart->LogicalPartition)
    {
        if (CurrentPart && CurrentPart->LogicalPartition)
            ASSERT(CurrentDisk->ExtendedPartition);

        // if (CurrentDisk->ExtendedPartition)
        CurrentPart = GetAdjPartListEntry(&CurrentDisk->LogicalPartListHead,
                                          CurrentPart, FALSE);
        if (CurrentPart)
            return CurrentPart;
    }

    /* If this was a primary region, or if no more logical regions:
     * now, check for primary regions */

    /* If the new CurrentPart == NULL, we exhausted
     * all the regions and we are done for this disk */
    return GetAdjPartListEntry(&CurrentDisk->PrimaryPartListHead,
                               CurrentPart, FALSE);
}


/**
 * @brief
 * Finds the adjacent (next or previous) disk region in order of
 * appearance on a given disk, starting at the specified partition.
 *
 * For MBR disks, the function will go into extended partitions and
 * enumerate the logical partitions inside.
 *
 * @param[in]   CurrentDisk
 * If CurrentPart == NULL, specifies the disk where to find the first
 * disk region. If CurrentPart != NULL, this parameter becomes optional:
 * - If CurrentDisk == NULL, the enumeration restarts at the specified
 *   CurrentPart using the disk where it resides.
 * - If CurrentDisk != NULL, the enumeration restarts either at
 *   CurrentPart, if it belongs to the same disk; otherwise, the
 *   enumeration restarts at the first region of the specified disk.
 *
 * @param[in]   CurrentPart
 * The disk region where to restart the search, or NULL for
 * starting from the beginning of the specified disk.
 *
 * @param[in]   EnumFlags
 * Enumeration flags.
 *
 * @return
 * The disk region, if any, or NULL when all regions on the disk
 * have been enumerated.
 *
 * @see GetAdjPartListEntry(), GetNextMBRDiskRegionByOrder(), GetPrevMBRDiskRegionByOrder(),
 *      GetNextMBRDiskRegionByType(), GetPrevMBRDiskRegionByType().
 **/
PPARTENTRY
GetAdjDiskRegion(
    _In_opt_ PDISKENTRY CurrentDisk,
    _In_opt_ PPARTENTRY CurrentPart,
    _In_ ULONG EnumFlags)
{
    BOOLEAN Direction = !(EnumFlags & ENUM_REGION_PREV); // TRUE: Next, FALSE: Previous

    /* Bail out if no parameters are given: cannot start search */
    if (!CurrentDisk && !CurrentPart)
        return NULL;

    if (CurrentDisk && (!CurrentPart || (CurrentDisk != CurrentPart->DiskEntry)))
    {
        /* We have a disk but either, no current region, or it is present on
         * a different disk: restart the search at the first or last region */
        CurrentPart = NULL;
    }
    else
    {
        /* else: (CurrentPart && (!CurrentDisk || (CurrentDisk == CurrentPart->DiskEntry)))
         * and continue the search with the next/previous region */

        /* Get the current region's disk */
        ASSERT(CurrentPart);
        CurrentDisk = CurrentPart->DiskEntry;
    }

Retry:
    /* If the disk is MBR, use the specific helpers; otherwise use the faster one */
    if (CurrentDisk->DiskStyle == PARTITION_STYLE_MBR)
    {
        /* Invalid combination */
        ASSERT(!((EnumFlags & ENUM_REGION_MBR_PRIMARY_ONLY) &&
                 (EnumFlags & ENUM_REGION_MBR_LOGICAL_ONLY)));

        if (EnumFlags & ENUM_REGION_MBR_PRIMARY_ONLY)
        {
            // ASSERT(!CurrentPart || !CurrentPart->LogicalPartition);
            CurrentPart = GetAdjPartListEntry(&CurrentDisk->PrimaryPartListHead,
                                              CurrentPart, Direction);
        }
        else
        if (EnumFlags & ENUM_REGION_MBR_LOGICAL_ONLY)
        {
            // ASSERT(CurrentDisk->ExtendedPartition);
            // ASSERT(!CurrentPart || !CurrentPart->LogicalPartition);
            CurrentPart = GetAdjPartListEntry(&CurrentDisk->LogicalPartListHead,
                                              CurrentPart, Direction);
        }
        else
        if (EnumFlags & ENUM_REGION_MBR_BY_ORDER)
        {
            // GetAdjMBRDiskRegion
            CurrentPart = (Direction ? GetNextMBRDiskRegionByOrder
                                     : GetPrevMBRDiskRegionByOrder)(CurrentDisk, CurrentPart);
        }
        else
        {
            CurrentPart = (Direction ? GetNextMBRDiskRegionByType
                                     : GetPrevMBRDiskRegionByType)(CurrentDisk, CurrentPart);
        }
    }
    else
    {
        // ASSERT(CurrentDisk->ExtendedPartition == NULL);
        // ASSERT(!CurrentPart || !CurrentPart->LogicalPartition);
        CurrentPart = GetAdjPartListEntry(&CurrentDisk->PrimaryPartListHead,
                                          CurrentPart, Direction);
    }

    /* If we need to check for partitioned regions
     * but the current one is not, retry again */
    if ((EnumFlags & ENUM_REGION_PARTITIONED) &&
        CurrentPart && !CurrentPart->IsPartitioned)
    {
        goto Retry;
    }
    return CurrentPart;
}

/**
 * @brief
 * Finds the adjacent (next or previous) disk region in order of appearance
 * on a given disk in the list, starting at the given current partition.
 *
 * For MBR disks, the function will go into extended partitions
 * for enumerating the logical partitions inside.
 *
 * @param[in]   List
 * The list of disks and partitions on the system.
 *
 * @param[in]   CurrentPart
 * The disk region where to restart the search, or NULL for starting
 * from either the beginning (first region in the first disk) or
 * from the end (last region in the last disk).
 *
 * @param[in]   EnumFlags
 * Enumeration flags.
 *
 * @return
 * The next or the previous disk region, if any, or NULL when all
 * regions have been enumerated.
 **/
PPARTENTRY
GetAdjPartition(
    _In_ PPARTLIST List,
    _In_opt_ PPARTENTRY CurrentPart,
    _In_ ULONG EnumFlags)
{
    PLIST_ENTRY DiskListHead = &List->DiskListHead;
    PDISKENTRY CurrentDisk;

    if (CurrentPart)
    {
        /* Check for the adjacent entry on the current partition's disk */
        CurrentDisk = CurrentPart->DiskEntry;
        CurrentPart = GetAdjDiskRegion(/*CurrentDisk*/ NULL, CurrentPart, EnumFlags);
        if (CurrentPart)
            goto Quit;

        /* Otherwise, check the next or previous disk */
        ASSERT(!IsListEmpty(DiskListHead));
    }
    else
    {
        /* Fail if no disks are available; otherwise,
         * check the first or last disk */
        if (IsListEmpty(DiskListHead))
            return NULL;
        CurrentDisk = NULL;
    }

    /* Search for the first (last) partition entry on the next (previous) disk */
    while ((CurrentDisk = GetAdjDiskListEntry(DiskListHead, CurrentDisk, EnumFlags)))
    {
        DPRINT("Disk #%d\n", CurrentDisk->DiskNumber);
        if (CurrentDisk->DiskStyle == PARTITION_STYLE_GPT)
            DPRINT("GPT-partitioned disk detected, not currently supported by SETUP!\n");

        CurrentPart = GetAdjDiskRegion(CurrentDisk, NULL, EnumFlags);
        if (CurrentPart)
            break;
    }

Quit:
    if (CurrentPart)
    {
        DPRINT("   %s Partition #%d, index %d - Type 0x%02x, IsPartitioned = %s, IsNew = %s, FormatState = %lu\n",
              (CurrentPart->LogicalPartition ? "Logical" : "Primary"),
               CurrentPart->PartitionNumber, CurrentPart->PartitionIndex,
               CurrentPart->PartitionType,
               CurrentPart->IsPartitioned ? "TRUE" : "FALSE",
               CurrentPart->New ? "Yes" : "No",
               CurrentPart->Volume.FormatState);
    }
    return CurrentPart;
}



static
VOID
GetDriverName(
    _In_ PDISKENTRY DiskEntry)
{
    RTL_QUERY_REGISTRY_TABLE QueryTable[2];
    WCHAR KeyName[32];
    NTSTATUS Status;

    RtlInitUnicodeString(&DiskEntry->DriverName, NULL);

    RtlStringCchPrintfW(KeyName, ARRAYSIZE(KeyName),
                        L"\\Scsi\\Scsi Port %hu",
                        DiskEntry->Port);

    RtlZeroMemory(&QueryTable, sizeof(QueryTable));

    QueryTable[0].Name = L"Driver";
    QueryTable[0].Flags = RTL_QUERY_REGISTRY_DIRECT;
    QueryTable[0].EntryContext = &DiskEntry->DriverName;

    /* This will allocate DiskEntry->DriverName if needed */
    Status = RtlQueryRegistryValues(RTL_REGISTRY_DEVICEMAP,
                                    KeyName,
                                    QueryTable,
                                    NULL,
                                    NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RtlQueryRegistryValues() failed (Status %lx)\n", Status);
    }
}

/****
 ** VOLUME-specific
 ****/
/*
 * FIXME: Rely on the MOUNTMGR to assign the drive letters.
 *
 * For the moment, we do it ourselves, by assigning drives to partitions
 * that are *only on MBR disks*. We first assign letters to each active
 * partition on each disk, then assign letters to each logical partition,
 * and finish by assigning letters to the remaining primary partitions.
 * (This algorithm is the one that can be observed in the Windows installer.)
 */
// static
VOID
AssignDriveLetters(
    IN PPARTLIST List)
{
    PDISKENTRY DiskEntry;
    PPARTENTRY PartEntry;
    WCHAR Letter;

    Letter = L'C';

    /* Assign drive letters to primary partitions */
    DiskEntry = NULL;
    while ((DiskEntry = GetAdjDiskListEntry(&List->DiskListHead, DiskEntry, TRUE)))
    {
        PartEntry = NULL;
        // while ((PartEntry = GetAdjPartListEntry(&DiskEntry->PrimaryPartListHead, PartEntry, TRUE)))
        while ((PartEntry = GetAdjDiskRegion(DiskEntry, PartEntry,
                                             ENUM_REGION_NEXT /*| ENUM_REGION_PARTITIONED*/ | ENUM_REGION_MBR_PRIMARY_ONLY)))
        {
            PartEntry->Volume.DriveLetter = 0;

            if (PartEntry->IsPartitioned &&
                !IsContainerPartition(PartEntry->PartitionType) &&
                (IsRecognizedPartition(PartEntry->PartitionType) ||
                 PartEntry->SectorCount.QuadPart != 0LL))
            {
                if (Letter <= L'Z')
                    PartEntry->Volume.DriveLetter = Letter++;
            }
        }
    }

    /* Assign drive letters to logical drives */
    DiskEntry = NULL;
    while ((DiskEntry = GetAdjDiskListEntry(&List->DiskListHead, DiskEntry, TRUE)))
    {
        PartEntry = NULL;
        // while ((PartEntry = GetAdjPartListEntry(&DiskEntry->LogicalPartListHead, PartEntry, TRUE)))
        while ((PartEntry = GetAdjDiskRegion(DiskEntry, PartEntry,
                                             ENUM_REGION_NEXT /*| ENUM_REGION_PARTITIONED*/ | ENUM_REGION_MBR_LOGICAL_ONLY)))
        {
            PartEntry->Volume.DriveLetter = 0;

            if (PartEntry->IsPartitioned &&
                (IsRecognizedPartition(PartEntry->PartitionType) ||
                 PartEntry->SectorCount.QuadPart != 0LL))
            {
                if (Letter <= L'Z')
                    PartEntry->Volume.DriveLetter = Letter++;
            }
        }
    }
}

static NTSTATUS
NTAPI
DiskIdentifierQueryRoutine(
    PWSTR ValueName,
    ULONG ValueType,
    PVOID ValueData,
    ULONG ValueLength,
    PVOID Context,
    PVOID EntryContext)
{
    PBIOSDISKENTRY BiosDiskEntry = (PBIOSDISKENTRY)Context;
    UNICODE_STRING NameU;

    if (ValueType == REG_SZ &&
        ValueLength == 20 * sizeof(WCHAR) &&
        ((PWCHAR)ValueData)[8] == L'-')
    {
        NameU.Buffer = (PWCHAR)ValueData;
        NameU.Length = NameU.MaximumLength = 8 * sizeof(WCHAR);
        RtlUnicodeStringToInteger(&NameU, 16, &BiosDiskEntry->Checksum);

        NameU.Buffer = (PWCHAR)ValueData + 9;
        RtlUnicodeStringToInteger(&NameU, 16, &BiosDiskEntry->Signature);

        return STATUS_SUCCESS;
    }

    return STATUS_UNSUCCESSFUL;
}

static NTSTATUS
NTAPI
DiskConfigurationDataQueryRoutine(
    PWSTR ValueName,
    ULONG ValueType,
    PVOID ValueData,
    ULONG ValueLength,
    PVOID Context,
    PVOID EntryContext)
{
    PBIOSDISKENTRY BiosDiskEntry = (PBIOSDISKENTRY)Context;
    PCM_FULL_RESOURCE_DESCRIPTOR FullResourceDescriptor;
    PCM_DISK_GEOMETRY_DEVICE_DATA DiskGeometry;
    ULONG i;

    if (ValueType != REG_FULL_RESOURCE_DESCRIPTOR ||
        ValueLength < sizeof(CM_FULL_RESOURCE_DESCRIPTOR))
        return STATUS_UNSUCCESSFUL;

    FullResourceDescriptor = (PCM_FULL_RESOURCE_DESCRIPTOR)ValueData;

    /* Hm. Version and Revision are not set on Microsoft Windows XP... */
#if 0
    if (FullResourceDescriptor->PartialResourceList.Version != 1 ||
        FullResourceDescriptor->PartialResourceList.Revision != 1)
        return STATUS_UNSUCCESSFUL;
#endif

    for (i = 0; i < FullResourceDescriptor->PartialResourceList.Count; i++)
    {
        if (FullResourceDescriptor->PartialResourceList.PartialDescriptors[i].Type != CmResourceTypeDeviceSpecific ||
            FullResourceDescriptor->PartialResourceList.PartialDescriptors[i].u.DeviceSpecificData.DataSize != sizeof(CM_DISK_GEOMETRY_DEVICE_DATA))
            continue;

        DiskGeometry = (PCM_DISK_GEOMETRY_DEVICE_DATA)&FullResourceDescriptor->PartialResourceList.PartialDescriptors[i + 1];
        BiosDiskEntry->DiskGeometry = *DiskGeometry;

        return STATUS_SUCCESS;
    }

    return STATUS_UNSUCCESSFUL;
}

static NTSTATUS
NTAPI
SystemConfigurationDataQueryRoutine(
    PWSTR ValueName,
    ULONG ValueType,
    PVOID ValueData,
    ULONG ValueLength,
    PVOID Context,
    PVOID EntryContext)
{
    PCM_FULL_RESOURCE_DESCRIPTOR FullResourceDescriptor;
    PCM_INT13_DRIVE_PARAMETER* Int13Drives = (PCM_INT13_DRIVE_PARAMETER*)Context;
    ULONG i;

    if (ValueType != REG_FULL_RESOURCE_DESCRIPTOR ||
        ValueLength < sizeof(CM_FULL_RESOURCE_DESCRIPTOR))
        return STATUS_UNSUCCESSFUL;

    FullResourceDescriptor = (PCM_FULL_RESOURCE_DESCRIPTOR)ValueData;

    /* Hm. Version and Revision are not set on Microsoft Windows XP... */
#if 0
    if (FullResourceDescriptor->PartialResourceList.Version != 1 ||
        FullResourceDescriptor->PartialResourceList.Revision != 1)
        return STATUS_UNSUCCESSFUL;
#endif

    for (i = 0; i < FullResourceDescriptor->PartialResourceList.Count; i++)
    {
        if (FullResourceDescriptor->PartialResourceList.PartialDescriptors[i].Type != CmResourceTypeDeviceSpecific ||
            FullResourceDescriptor->PartialResourceList.PartialDescriptors[i].u.DeviceSpecificData.DataSize % sizeof(CM_INT13_DRIVE_PARAMETER) != 0)
            continue;

        *Int13Drives = (CM_INT13_DRIVE_PARAMETER*)RtlAllocateHeap(ProcessHeap, 0,
                       FullResourceDescriptor->PartialResourceList.PartialDescriptors[i].u.DeviceSpecificData.DataSize);
        if (*Int13Drives == NULL)
            return STATUS_NO_MEMORY;

        memcpy(*Int13Drives,
               &FullResourceDescriptor->PartialResourceList.PartialDescriptors[i + 1],
               FullResourceDescriptor->PartialResourceList.PartialDescriptors[i].u.DeviceSpecificData.DataSize);
        return STATUS_SUCCESS;
    }

    return STATUS_UNSUCCESSFUL;
}


static VOID
EnumerateBiosDiskEntries(
    IN PPARTLIST PartList)
{
    RTL_QUERY_REGISTRY_TABLE QueryTable[3];
    WCHAR Name[120];
    ULONG AdapterCount;
    ULONG ControllerCount;
    ULONG DiskCount;
    NTSTATUS Status;
    PCM_INT13_DRIVE_PARAMETER Int13Drives;
    PBIOSDISKENTRY BiosDiskEntry;

#define ROOT_NAME   L"\\Registry\\Machine\\HARDWARE\\DESCRIPTION\\System\\MultifunctionAdapter"

    memset(QueryTable, 0, sizeof(QueryTable));

    QueryTable[1].Name = L"Configuration Data";
    QueryTable[1].QueryRoutine = SystemConfigurationDataQueryRoutine;
    Int13Drives = NULL;
    Status = RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE,
                                    L"\\Registry\\Machine\\HARDWARE\\DESCRIPTION\\System",
                                    &QueryTable[1],
                                    (PVOID)&Int13Drives,
                                    NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Unable to query the 'Configuration Data' key in '\\Registry\\Machine\\HARDWARE\\DESCRIPTION\\System', status=%lx\n", Status);
        return;
    }

    for (AdapterCount = 0; ; ++AdapterCount)
    {
        RtlStringCchPrintfW(Name, ARRAYSIZE(Name),
                            L"%s\\%lu",
                            ROOT_NAME, AdapterCount);
        Status = RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE,
                                        Name,
                                        &QueryTable[2],
                                        NULL,
                                        NULL);
        if (!NT_SUCCESS(Status))
        {
            break;
        }

        RtlStringCchPrintfW(Name, ARRAYSIZE(Name),
                            L"%s\\%lu\\DiskController",
                            ROOT_NAME, AdapterCount);
        Status = RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE,
                                        Name,
                                        &QueryTable[2],
                                        NULL,
                                        NULL);
        if (NT_SUCCESS(Status))
        {
            for (ControllerCount = 0; ; ++ControllerCount)
            {
                RtlStringCchPrintfW(Name, ARRAYSIZE(Name),
                                    L"%s\\%lu\\DiskController\\%lu",
                                    ROOT_NAME, AdapterCount, ControllerCount);
                Status = RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE,
                                                Name,
                                                &QueryTable[2],
                                                NULL,
                                                NULL);
                if (!NT_SUCCESS(Status))
                {
                    RtlFreeHeap(ProcessHeap, 0, Int13Drives);
                    return;
                }

                RtlStringCchPrintfW(Name, ARRAYSIZE(Name),
                                    L"%s\\%lu\\DiskController\\%lu\\DiskPeripheral",
                                    ROOT_NAME, AdapterCount, ControllerCount);
                Status = RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE,
                                                Name,
                                                &QueryTable[2],
                                                NULL,
                                                NULL);
                if (NT_SUCCESS(Status))
                {
                    QueryTable[0].Name = L"Identifier";
                    QueryTable[0].QueryRoutine = DiskIdentifierQueryRoutine;
                    QueryTable[1].Name = L"Configuration Data";
                    QueryTable[1].QueryRoutine = DiskConfigurationDataQueryRoutine;

                    for (DiskCount = 0; ; ++DiskCount)
                    {
                        BiosDiskEntry = (BIOSDISKENTRY*)RtlAllocateHeap(ProcessHeap, HEAP_ZERO_MEMORY, sizeof(BIOSDISKENTRY));
                        if (BiosDiskEntry == NULL)
                        {
                            RtlFreeHeap(ProcessHeap, 0, Int13Drives);
                            return;
                        }

                        RtlStringCchPrintfW(Name, ARRAYSIZE(Name),
                                            L"%s\\%lu\\DiskController\\%lu\\DiskPeripheral\\%lu",
                                            ROOT_NAME, AdapterCount, ControllerCount, DiskCount);
                        Status = RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE,
                                                        Name,
                                                        QueryTable,
                                                        (PVOID)BiosDiskEntry,
                                                        NULL);
                        if (!NT_SUCCESS(Status))
                        {
                            RtlFreeHeap(ProcessHeap, 0, BiosDiskEntry);
                            RtlFreeHeap(ProcessHeap, 0, Int13Drives);
                            return;
                        }

                        BiosDiskEntry->AdapterNumber = 0; // And NOT "AdapterCount" as it needs to be hardcoded for BIOS!
                        BiosDiskEntry->ControllerNumber = ControllerCount;
                        BiosDiskEntry->DiskNumber = DiskCount;
                        BiosDiskEntry->DiskEntry = NULL;

                        if (DiskCount < Int13Drives[0].NumberDrives)
                        {
                            BiosDiskEntry->Int13DiskData = Int13Drives[DiskCount];
                        }
                        else
                        {
                            DPRINT1("Didn't find Int13 drive data for disk %u\n", DiskCount);
                        }

                        InsertTailList(&PartList->BiosDiskListHead, &BiosDiskEntry->ListEntry);

                        DPRINT("--->\n");
                        DPRINT("AdapterNumber:     %lu\n", BiosDiskEntry->AdapterNumber);
                        DPRINT("ControllerNumber:  %lu\n", BiosDiskEntry->ControllerNumber);
                        DPRINT("DiskNumber:        %lu\n", BiosDiskEntry->DiskNumber);
                        DPRINT("Signature:         %08lx\n", BiosDiskEntry->Signature);
                        DPRINT("Checksum:          %08lx\n", BiosDiskEntry->Checksum);
                        DPRINT("BytesPerSector:    %lu\n", BiosDiskEntry->DiskGeometry.BytesPerSector);
                        DPRINT("NumberOfCylinders: %lu\n", BiosDiskEntry->DiskGeometry.NumberOfCylinders);
                        DPRINT("NumberOfHeads:     %lu\n", BiosDiskEntry->DiskGeometry.NumberOfHeads);
                        DPRINT("DriveSelect:       %02x\n", BiosDiskEntry->Int13DiskData.DriveSelect);
                        DPRINT("MaxCylinders:      %lu\n", BiosDiskEntry->Int13DiskData.MaxCylinders);
                        DPRINT("SectorsPerTrack:   %d\n", BiosDiskEntry->Int13DiskData.SectorsPerTrack);
                        DPRINT("MaxHeads:          %d\n", BiosDiskEntry->Int13DiskData.MaxHeads);
                        DPRINT("NumberDrives:      %d\n", BiosDiskEntry->Int13DiskData.NumberDrives);
                        DPRINT("<---\n");
                    }
                }
            }
        }
    }

    RtlFreeHeap(ProcessHeap, 0, Int13Drives);

#undef ROOT_NAME
}


/*
 * Detects whether a disk reports as a "super-floppy", i.e. an unpartitioned
 * disk with a valid VBR, following the criteria used by IoReadPartitionTable()
 * and IoWritePartitionTable():
 * only one single partition starting at the beginning of the disk; the reported
 * defaults are: partition number being zero and its type being FAT16 non-bootable.
 * Note also that accessing \Device\HarddiskN\Partition0 or Partition1 returns
 * the same data.
 */
// static
BOOLEAN
IsSuperFloppy(
    IN PDISKENTRY DiskEntry)
{
    PPARTITION_INFORMATION PartitionInfo;
    ULONGLONG PartitionLengthEstimate;

    /* No layout buffer: we cannot say anything yet */
    if (DiskEntry->LayoutBuffer == NULL)
        return FALSE;

    /* We must have only one partition */
    if (DiskEntry->LayoutBuffer->PartitionCount != 1)
        return FALSE;

    /* Get the single partition entry */
    PartitionInfo = DiskEntry->LayoutBuffer->PartitionEntry;

    /* The single partition must start at the beginning of the disk */
    if (!(PartitionInfo->StartingOffset.QuadPart == 0 &&
          PartitionInfo->HiddenSectors == 0))
    {
        return FALSE;
    }

    /* The disk signature is usually set to one; warn in case it's not */
    if (DiskEntry->LayoutBuffer->Signature != 1)
    {
        DPRINT1("Super-Floppy disk %lu signature %08x != 1!\n",
                DiskEntry->DiskNumber, DiskEntry->LayoutBuffer->Signature);
    }

    /*
     * The partition number must be zero or one, be recognized,
     * have FAT16 type and report as non-bootable.
     */
    if ((PartitionInfo->PartitionNumber != 0 &&
         PartitionInfo->PartitionNumber != 1) ||
        PartitionInfo->RecognizedPartition != TRUE ||
        PartitionInfo->PartitionType != PARTITION_FAT_16 ||
        PartitionInfo->BootIndicator != FALSE)
    {
        DPRINT1("Super-Floppy disk %lu does not return default settings!\n"
                "    PartitionNumber = %lu, expected 0\n"
                "    RecognizedPartition = %s, expected TRUE\n"
                "    PartitionType = 0x%02x, expected 0x04 (PARTITION_FAT_16)\n"
                "    BootIndicator = %s, expected FALSE\n",
                DiskEntry->DiskNumber,
                PartitionInfo->PartitionNumber,
                PartitionInfo->RecognizedPartition ? "TRUE" : "FALSE",
                PartitionInfo->PartitionType,
                PartitionInfo->BootIndicator ? "TRUE" : "FALSE");
    }

    /* The partition lengths should agree */
    PartitionLengthEstimate = GetDiskSizeInBytes(DiskEntry);
    if (PartitionInfo->PartitionLength.QuadPart != PartitionLengthEstimate)
    {
        DPRINT1("PartitionLength = %I64u is different from PartitionLengthEstimate = %I64u\n",
                PartitionInfo->PartitionLength.QuadPart, PartitionLengthEstimate);
    }

    return TRUE;
}


/*
 * Inserts the disk region represented by PartEntry into either the primary
 * or the logical partition list of the given disk.
 * The lists are kept sorted by increasing order of start sectors.
 * Of course no disk region should overlap at all with one another.
 */
static
BOOLEAN
InsertDiskRegion(
    IN PDISKENTRY DiskEntry,
    IN PPARTENTRY PartEntry,
    IN BOOLEAN LogicalPartition)
{
    PLIST_ENTRY List;
    PLIST_ENTRY Entry;
    PPARTENTRY PartEntry2;

    /* Use the correct partition list */
    if (LogicalPartition)
        List = &DiskEntry->LogicalPartListHead;
    else
        List = &DiskEntry->PrimaryPartListHead;

    /* Find the first disk region before which we need to insert the new one */
    for (Entry = List->Flink; Entry != List; Entry = Entry->Flink)
    {
        PartEntry2 = CONTAINING_RECORD(Entry, PARTENTRY, ListEntry);

        /* Ignore any unused empty region */
        if ((PartEntry2->PartitionType == PARTITION_ENTRY_UNUSED &&
             PartEntry2->StartSector.QuadPart == 0) || PartEntry2->SectorCount.QuadPart == 0)
        {
            continue;
        }

        /* If the current region ends before the one to be inserted, try again */
        if (PartEntry2->StartSector.QuadPart + PartEntry2->SectorCount.QuadPart - 1 < PartEntry->StartSector.QuadPart)
            continue;

        /*
         * One of the disk region boundaries crosses the desired region
         * (it starts after the desired region, or ends before the end
         * of the desired region): this is an impossible situation because
         * disk regions (partitions) cannot overlap!
         * Throw an error and bail out.
         */
        if (max(PartEntry->StartSector.QuadPart, PartEntry2->StartSector.QuadPart)
            <=
            min( PartEntry->StartSector.QuadPart +  PartEntry->SectorCount.QuadPart - 1,
                PartEntry2->StartSector.QuadPart + PartEntry2->SectorCount.QuadPart - 1))
        {
            DPRINT1("Disk region overlap problem, stopping there!\n"
                    "Partition to be inserted:\n"
                    "    StartSector = %I64u ; EndSector = %I64u\n"
                    "Existing disk region:\n"
                    "    StartSector = %I64u ; EndSector = %I64u\n",
                     PartEntry->StartSector.QuadPart,
                     PartEntry->StartSector.QuadPart +  PartEntry->SectorCount.QuadPart - 1,
                    PartEntry2->StartSector.QuadPart,
                    PartEntry2->StartSector.QuadPart + PartEntry2->SectorCount.QuadPart - 1);
            return FALSE;
        }

        /* We have found the first region before which the new one has to be inserted */
        break;
    }

    /* Insert the disk region */
    InsertTailList(Entry, &PartEntry->ListEntry);
    return TRUE;
}

/****
 ** VOLUME-specific partly (for the FS-specific fields)
 ****/
static
PPARTENTRY
CreateInsertBlankRegion(
    IN PDISKENTRY DiskEntry,
    IN OUT PLIST_ENTRY ListHead,
    IN ULONGLONG StartSector,
    IN ULONGLONG SectorCount,
    IN BOOLEAN LogicalSpace)
{
    PPARTENTRY NewPartEntry;

    NewPartEntry = RtlAllocateHeap(ProcessHeap,
                                   HEAP_ZERO_MEMORY,
                                   sizeof(PARTENTRY));
    if (NewPartEntry == NULL)
        return NULL;

    NewPartEntry->DiskEntry = DiskEntry;

    NewPartEntry->StartSector.QuadPart = StartSector;
    NewPartEntry->SectorCount.QuadPart = SectorCount;

    NewPartEntry->LogicalPartition = LogicalSpace;
    NewPartEntry->IsPartitioned = FALSE;
    NewPartEntry->PartitionType = PARTITION_ENTRY_UNUSED;
    RtlZeroMemory(&NewPartEntry->Volume, sizeof(NewPartEntry->Volume));

    DPRINT1("First Sector : %I64u\n", NewPartEntry->StartSector.QuadPart);
    DPRINT1("Last Sector  : %I64u\n", NewPartEntry->StartSector.QuadPart + NewPartEntry->SectorCount.QuadPart - 1);
    DPRINT1("Total Sectors: %I64u\n", NewPartEntry->SectorCount.QuadPart);

    /* Insert the new entry into the list */
    InsertTailList(ListHead, &NewPartEntry->ListEntry);

    return NewPartEntry;
}

/****
 ** VOLUME-specific partly (for the FS-specific fields)
 ****/
static
BOOLEAN
InitializePartitionEntry(
    _Inout_ PPARTENTRY PartEntry,
    _In_opt_ ULONGLONG SectorCount)
{
    PDISKENTRY DiskEntry = PartEntry->DiskEntry;

    DPRINT1("Current partition sector count: %I64u\n", PartEntry->SectorCount.QuadPart);

    /* Fail if we try to initialize this partition entry
     * with more sectors than what it actually contains */
    if (SectorCount > PartEntry->SectorCount.QuadPart)
        return FALSE;

    /* Fail if the partition is already in use */
    ASSERT(!PartEntry->IsPartitioned);

    if ((SectorCount == 0) ||
        (AlignDown(PartEntry->StartSector.QuadPart + SectorCount, DiskEntry->SectorAlignment) -
                   PartEntry->StartSector.QuadPart == PartEntry->SectorCount.QuadPart))
    {
        /* Reuse the whole current entry */
    }
    else
    {
        ULONGLONG StartSector;
        ULONGLONG SectorCount2;
        PPARTENTRY NewPartEntry;

        /* Create a partition entry that represents the remaining space
         * after the partition to be initialized */

        StartSector = AlignDown(PartEntry->StartSector.QuadPart + SectorCount, DiskEntry->SectorAlignment);
        SectorCount2 = PartEntry->StartSector.QuadPart + PartEntry->SectorCount.QuadPart - StartSector;

        NewPartEntry = CreateInsertBlankRegion(DiskEntry,
                                               PartEntry->ListEntry.Flink,
                                               StartSector,
                                               SectorCount2,
                                               PartEntry->LogicalPartition);
        if (NewPartEntry == NULL)
        {
            DPRINT1("Failed to create a new empty region for disk space!\n");
            return FALSE;
        }

        /* Resize down the partition entry; its StartSector remains the same */
        PartEntry->SectorCount.QuadPart = StartSector - PartEntry->StartSector.QuadPart;
    }

    /* Convert to a new partition entry */
    PartEntry->New = TRUE;
    PartEntry->IsPartitioned = TRUE;

// FIXME: Use FileSystemToMBRPartitionType() only for MBR, otherwise use PARTITION_BASIC_DATA_GUID.
    PartEntry->PartitionType = FileSystemToMBRPartitionType(L"RAW",
                                                            PartEntry->StartSector.QuadPart,
                                                            PartEntry->SectorCount.QuadPart);
    ASSERT(PartEntry->PartitionType != PARTITION_ENTRY_UNUSED);

    RtlZeroMemory(&PartEntry->Volume, sizeof(PartEntry->Volume));
    /**/PartEntry->Volume.New = TRUE;/**/
    PartEntry->BootIndicator = FALSE;

    DPRINT1("First Sector : %I64u\n", PartEntry->StartSector.QuadPart);
    DPRINT1("Last Sector  : %I64u\n", PartEntry->StartSector.QuadPart + PartEntry->SectorCount.QuadPart - 1);
    DPRINT1("Total Sectors: %I64u\n", PartEntry->SectorCount.QuadPart);

    return TRUE;
}


NTSTATUS
MountVolume(
    _Inout_ PVOLINFO VolumeEntry,
    _In_opt_ UCHAR MbrPartitionType)
{
    NTSTATUS Status;
    UNICODE_STRING Name;
    HANDLE PartitionHandle;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    UCHAR LabelBuffer[sizeof(FILE_FS_VOLUME_INFORMATION) + 256 * sizeof(WCHAR)];
    PFILE_FS_VOLUME_INFORMATION LabelInfo = (PFILE_FS_VOLUME_INFORMATION)LabelBuffer;

#if 0
    /* Reset some volume information */
    VolumeEntry->DriveLetter = L'\0';
    VolumeEntry->FormatState = Unformatted;
    VolumeEntry->FileSystem[0] = L'\0';
    RtlZeroMemory(VolumeEntry->VolumeLabel, sizeof(VolumeEntry->VolumeLabel));
    // VolumeEntry->New = FALSE;
    VolumeEntry->NeedsCheck = FALSE;
#endif

    /* Specify the partition as initially unformatted */
    VolumeEntry->FormatState = Unformatted;
    VolumeEntry->FileSystem[0] = L'\0';

    /* Initialize the partition volume label */
    RtlZeroMemory(VolumeEntry->VolumeLabel, sizeof(VolumeEntry->VolumeLabel));

#if 0
    if (!IsRecognizedPartition(MbrPartitionType))
    {
        /* Unknown partition, hence unknown format (may or may not be actually formatted) */
        VolumeEntry->FormatState = UnknownFormat;
        return STATUS_SUCCESS;
    }
#else
    if (!*VolumeEntry->DeviceName)
    {
        /* No volume attached, bail out */
        return STATUS_SUCCESS;
    }
#endif

    /* Try to open the volume so as to mount it */
    RtlInitUnicodeString(&Name, VolumeEntry->DeviceName);
    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);

    PartitionHandle = NULL;
    Status = NtOpenFile(&PartitionHandle,
                        FILE_READ_DATA | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtOpenFile() failed, Status 0x%08lx\n", Status);
    }

    if (PartitionHandle)
    {
        ASSERT(NT_SUCCESS(Status));

        /* Try to guess the mounted FS */
        Status = InferFileSystem(NULL, PartitionHandle,
                                 VolumeEntry->FileSystem,
                                 sizeof(VolumeEntry->FileSystem));
        if (!NT_SUCCESS(Status))
            DPRINT1("InferFileSystem() failed, Status 0x%08lx\n", Status);
    }
    if (*VolumeEntry->FileSystem)
    {
        ASSERT(PartitionHandle);

        /*
         * Handle partition mounted with RawFS: it is
         * either unformatted or has an unknown format.
         */
        if (wcsicmp(VolumeEntry->FileSystem, L"RAW") == 0)
        {
            /*
             * True unformatted partitions on NT are created with their
             * partition type set to either one of the following values,
             * and are mounted with RawFS. This is done this way since we
             * are assured to have FAT support, which is the only FS that
             * uses these partition types. Therefore, having a partition
             * mounted with RawFS and with these partition types means that
             * the FAT FS was unable to mount it beforehand and thus the
             * partition is unformatted.
             * However, any partition mounted by RawFS that does NOT have
             * any of these partition types must be considered as having
             * an unknown format.
             */
            if (MbrPartitionType == PARTITION_FAT_12 ||
                MbrPartitionType == PARTITION_FAT_16 ||
                MbrPartitionType == PARTITION_HUGE   ||
                MbrPartitionType == PARTITION_XINT13 ||
                MbrPartitionType == PARTITION_FAT32  ||
                MbrPartitionType == PARTITION_FAT32_XINT13)
            {
                VolumeEntry->FormatState = Unformatted;
            }
            else
            {
                /* Close the partition before dismounting */
                NtClose(PartitionHandle);
                PartitionHandle = NULL;
                /*
                 * Dismount the partition since RawFS owns it, and set its
                 * format to unknown (may or may not be actually formatted).
                 */
                DismountVolume(VolumeEntry);
                VolumeEntry->FormatState = UnknownFormat;
                VolumeEntry->FileSystem[0] = L'\0';
            }
        }
        else
        {
            VolumeEntry->FormatState = Formatted;
        }
    }
    else
    {
        VolumeEntry->FormatState = UnknownFormat;
    }

    /* Retrieve the partition volume label */
    if (PartitionHandle)
    {
        Status = NtQueryVolumeInformationFile(PartitionHandle,
                                              &IoStatusBlock,
                                              &LabelBuffer,
                                              sizeof(LabelBuffer),
                                              FileFsVolumeInformation);
        if (NT_SUCCESS(Status))
        {
            /* Copy the (possibly truncated) volume label and NULL-terminate it */
            RtlStringCbCopyNW(VolumeEntry->VolumeLabel, sizeof(VolumeEntry->VolumeLabel),
                              LabelInfo->VolumeLabel, LabelInfo->VolumeLabelLength);
        }
        else
        {
            DPRINT1("NtQueryVolumeInformationFile() failed, Status 0x%08lx\n", Status);
        }
    }

    /* Close the partition */
    if (PartitionHandle)
        NtClose(PartitionHandle);

    return STATUS_SUCCESS;
}


/****
 ** VOLUME-specific partly (for the FS recognition)
 ****/
static
VOID
AddPartitionToDisk(
    IN ULONG DiskNumber,
    IN PDISKENTRY DiskEntry,
    IN ULONG PartitionIndex,
    IN BOOLEAN LogicalPartition)
{
    PPARTITION_INFORMATION PartitionInfo;
    PPARTENTRY PartEntry;

    PartitionInfo = &DiskEntry->LayoutBuffer->PartitionEntry[PartitionIndex];

    if (PartitionInfo->PartitionType == PARTITION_ENTRY_UNUSED ||
        ((LogicalPartition != FALSE) && IsContainerPartition(PartitionInfo->PartitionType)))
    {
        return;
    }

    PartEntry = RtlAllocateHeap(ProcessHeap,
                                HEAP_ZERO_MEMORY,
                                sizeof(PARTENTRY));
    if (PartEntry == NULL)
        return;

    PartEntry->DiskEntry = DiskEntry;

    PartEntry->StartSector.QuadPart = (ULONGLONG)PartitionInfo->StartingOffset.QuadPart / DiskEntry->BytesPerSector;
    PartEntry->SectorCount.QuadPart = (ULONGLONG)PartitionInfo->PartitionLength.QuadPart / DiskEntry->BytesPerSector;

    PartEntry->BootIndicator = PartitionInfo->BootIndicator;
    PartEntry->PartitionType = PartitionInfo->PartitionType;

    PartEntry->LogicalPartition = LogicalPartition;
    PartEntry->IsPartitioned = TRUE;
    PartEntry->OnDiskPartitionNumber = PartitionInfo->PartitionNumber;
    PartEntry->PartitionNumber = PartitionInfo->PartitionNumber;
    PartEntry->PartitionIndex = PartitionIndex;

    /* No volume initially */
    RtlZeroMemory(&PartEntry->Volume, sizeof(PartEntry->Volume));

    if (IsContainerPartition(PartEntry->PartitionType))
    {
        if (!LogicalPartition && DiskEntry->ExtendedPartition == NULL)
        {
            ASSERT(PartEntry->IsPartitioned &&
                   !PartEntry->LogicalPartition &&
                   IsContainerPartition(PartEntry->PartitionType));

            DiskEntry->ExtendedPartition = PartEntry;
        }
    }
    else if (IsRecognizedPartition(PartEntry->PartitionType))
    {
        NTSTATUS Status;

        ASSERT(PartitionInfo->RecognizedPartition);
        ASSERT(PartEntry->IsPartitioned && PartEntry->PartitionNumber != 0);

        // FIXME: Make a device name for the volume
        RtlStringCchPrintfW(PartEntry->Volume.DeviceName,
                            ARRAYSIZE(PartEntry->Volume.DeviceName),
                            L"\\Device\\Harddisk%lu\\Partition%lu",
                            DiskEntry->DiskNumber,
                            PartEntry->PartitionNumber);

        /* Attach and mount the volume */
        Status = MountVolume(&PartEntry->Volume,
                             PartEntry->PartitionType);
        UNREFERENCED_PARAMETER(Status); // FIXME
    }
    else
    {
        /* Unknown partition, hence unknown format (may or may not be actually formatted) */
        PartEntry->Volume.FormatState = UnknownFormat;
    }

    InsertDiskRegion(DiskEntry, PartEntry, LogicalPartition);
}

static
VOID
ScanForUnpartitionedDiskSpace(
    IN PDISKENTRY DiskEntry)
{
    ULONGLONG StartSector;
    ULONGLONG SectorCount;
    ULONGLONG LastStartSector;
    ULONGLONG LastSectorCount;
    ULONGLONG LastUnusedSectorCount;
    PPARTENTRY PartEntry;
    PPARTENTRY NewPartEntry;
    PLIST_ENTRY Entry;

    DPRINT("ScanForUnpartitionedDiskSpace()\n");

    if (IsListEmpty(&DiskEntry->PrimaryPartListHead))
    {
        DPRINT1("No primary partition!\n");

        /* Create a partition entry that represents the empty disk */

        if (DiskEntry->SectorAlignment < 2048)
            StartSector = 2048ULL;
        else
            StartSector = (ULONGLONG)DiskEntry->SectorAlignment;
        SectorCount = AlignDown(DiskEntry->SectorCount.QuadPart, DiskEntry->SectorAlignment) - StartSector;

        NewPartEntry = CreateInsertBlankRegion(DiskEntry,
                                               &DiskEntry->PrimaryPartListHead,
                                               StartSector,
                                               SectorCount,
                                               FALSE);
        if (NewPartEntry == NULL)
            DPRINT1("Failed to create a new empty region for full disk space!\n");

        return;
    }

    /* Start partition at head 1, cylinder 0 */
    if (DiskEntry->SectorAlignment < 2048)
        LastStartSector = 2048ULL;
    else
        LastStartSector = (ULONGLONG)DiskEntry->SectorAlignment;
    LastSectorCount = 0ULL;
    LastUnusedSectorCount = 0ULL;

    for (Entry = DiskEntry->PrimaryPartListHead.Flink;
         Entry != &DiskEntry->PrimaryPartListHead;
         Entry = Entry->Flink)
    {
        PartEntry = CONTAINING_RECORD(Entry, PARTENTRY, ListEntry);

        if (PartEntry->PartitionType != PARTITION_ENTRY_UNUSED ||
            PartEntry->SectorCount.QuadPart != 0ULL)
        {
            LastUnusedSectorCount =
                PartEntry->StartSector.QuadPart - (LastStartSector + LastSectorCount);

            if (PartEntry->StartSector.QuadPart > (LastStartSector + LastSectorCount) &&
                LastUnusedSectorCount >= (ULONGLONG)DiskEntry->SectorAlignment)
            {
                DPRINT("Unpartitioned disk space %I64u sectors\n", LastUnusedSectorCount);

                StartSector = LastStartSector + LastSectorCount;
                SectorCount = AlignDown(StartSector + LastUnusedSectorCount, DiskEntry->SectorAlignment) - StartSector;

                /* Insert the table into the list */
                NewPartEntry = CreateInsertBlankRegion(DiskEntry,
                                                       &PartEntry->ListEntry,
                                                       StartSector,
                                                       SectorCount,
                                                       FALSE);
                if (NewPartEntry == NULL)
                {
                    DPRINT1("Failed to create a new empty region for disk space!\n");
                    return;
                }
            }

            LastStartSector = PartEntry->StartSector.QuadPart;
            LastSectorCount = PartEntry->SectorCount.QuadPart;
        }
    }

    /* Check for trailing unpartitioned disk space */
    if ((LastStartSector + LastSectorCount) < DiskEntry->SectorCount.QuadPart)
    {
        LastUnusedSectorCount = AlignDown(DiskEntry->SectorCount.QuadPart - (LastStartSector + LastSectorCount), DiskEntry->SectorAlignment);

        if (LastUnusedSectorCount >= (ULONGLONG)DiskEntry->SectorAlignment)
        {
            DPRINT("Unpartitioned disk space: %I64u sectors\n", LastUnusedSectorCount);

            StartSector = LastStartSector + LastSectorCount;
            SectorCount = AlignDown(StartSector + LastUnusedSectorCount, DiskEntry->SectorAlignment) - StartSector;

            /* Append the table to the list */
            NewPartEntry = CreateInsertBlankRegion(DiskEntry,
                                                   &DiskEntry->PrimaryPartListHead,
                                                   StartSector,
                                                   SectorCount,
                                                   FALSE);
            if (NewPartEntry == NULL)
            {
                DPRINT1("Failed to create a new empty region for trailing disk space!\n");
                return;
            }
        }
    }

    if (DiskEntry->ExtendedPartition != NULL)
    {
        if (IsListEmpty(&DiskEntry->LogicalPartListHead))
        {
            DPRINT1("No logical partition!\n");

            /* Create a partition entry that represents the empty extended partition */

            StartSector = DiskEntry->ExtendedPartition->StartSector.QuadPart + (ULONGLONG)DiskEntry->SectorAlignment;
            SectorCount = DiskEntry->ExtendedPartition->SectorCount.QuadPart - (ULONGLONG)DiskEntry->SectorAlignment;

            NewPartEntry = CreateInsertBlankRegion(DiskEntry,
                                                   &DiskEntry->LogicalPartListHead,
                                                   StartSector,
                                                   SectorCount,
                                                   TRUE);
            if (NewPartEntry == NULL)
                DPRINT1("Failed to create a new empty region for full extended partition space!\n");

            return;
        }

        /* Start partition at head 1, cylinder 0 */
        LastStartSector = DiskEntry->ExtendedPartition->StartSector.QuadPart + (ULONGLONG)DiskEntry->SectorAlignment;
        LastSectorCount = 0ULL;
        LastUnusedSectorCount = 0ULL;

        for (Entry = DiskEntry->LogicalPartListHead.Flink;
             Entry != &DiskEntry->LogicalPartListHead;
             Entry = Entry->Flink)
        {
            PartEntry = CONTAINING_RECORD(Entry, PARTENTRY, ListEntry);

            if (PartEntry->PartitionType != PARTITION_ENTRY_UNUSED ||
                PartEntry->SectorCount.QuadPart != 0ULL)
            {
                LastUnusedSectorCount =
                    PartEntry->StartSector.QuadPart - (ULONGLONG)DiskEntry->SectorAlignment - (LastStartSector + LastSectorCount);

                if ((PartEntry->StartSector.QuadPart - (ULONGLONG)DiskEntry->SectorAlignment) > (LastStartSector + LastSectorCount) &&
                    LastUnusedSectorCount >= (ULONGLONG)DiskEntry->SectorAlignment)
                {
                    DPRINT("Unpartitioned disk space %I64u sectors\n", LastUnusedSectorCount);

                    StartSector = LastStartSector + LastSectorCount;
                    SectorCount = AlignDown(StartSector + LastUnusedSectorCount, DiskEntry->SectorAlignment) - StartSector;

                    /* Insert the table into the list */
                    NewPartEntry = CreateInsertBlankRegion(DiskEntry,
                                                           &PartEntry->ListEntry,
                                                           StartSector,
                                                           SectorCount,
                                                           TRUE);
                    if (NewPartEntry == NULL)
                    {
                        DPRINT1("Failed to create a new empty region for extended partition space!\n");
                        return;
                    }
                }

                LastStartSector = PartEntry->StartSector.QuadPart;
                LastSectorCount = PartEntry->SectorCount.QuadPart;
            }
        }

        /* Check for trailing unpartitioned disk space */
        if ((LastStartSector + LastSectorCount) < DiskEntry->ExtendedPartition->StartSector.QuadPart + DiskEntry->ExtendedPartition->SectorCount.QuadPart)
        {
            LastUnusedSectorCount = AlignDown(DiskEntry->ExtendedPartition->StartSector.QuadPart +
                                              DiskEntry->ExtendedPartition->SectorCount.QuadPart - (LastStartSector + LastSectorCount),
                                              DiskEntry->SectorAlignment);

            if (LastUnusedSectorCount >= (ULONGLONG)DiskEntry->SectorAlignment)
            {
                DPRINT("Unpartitioned disk space: %I64u sectors\n", LastUnusedSectorCount);

                StartSector = LastStartSector + LastSectorCount;
                SectorCount = AlignDown(StartSector + LastUnusedSectorCount, DiskEntry->SectorAlignment) - StartSector;

                /* Append the table to the list */
                NewPartEntry = CreateInsertBlankRegion(DiskEntry,
                                                       &DiskEntry->LogicalPartListHead,
                                                       StartSector,
                                                       SectorCount,
                                                       TRUE);
                if (NewPartEntry == NULL)
                {
                    DPRINT1("Failed to create a new empty region for extended partition space!\n");
                    return;
                }
            }
        }
    }

    DPRINT("ScanForUnpartitionedDiskSpace() done\n");
}

static
VOID
SetDiskSignature(
    IN PPARTLIST List,
    IN PDISKENTRY DiskEntry)
{
    LARGE_INTEGER SystemTime;
    TIME_FIELDS TimeFields;
    PDISKENTRY DiskEntry2;
    PUCHAR Buffer;

    if (DiskEntry->DiskStyle == PARTITION_STYLE_GPT)
    {
        DPRINT("GPT-partitioned disk detected, not currently supported by SETUP!\n");
        return;
    }

    Buffer = (PUCHAR)&DiskEntry->LayoutBuffer->Signature;

    while (TRUE)
    {
        NtQuerySystemTime(&SystemTime);
        RtlTimeToTimeFields(&SystemTime, &TimeFields);

        Buffer[0] = (UCHAR)(TimeFields.Year & 0xFF) + (UCHAR)(TimeFields.Hour & 0xFF);
        Buffer[1] = (UCHAR)(TimeFields.Year >> 8) + (UCHAR)(TimeFields.Minute & 0xFF);
        Buffer[2] = (UCHAR)(TimeFields.Month & 0xFF) + (UCHAR)(TimeFields.Second & 0xFF);
        Buffer[3] = (UCHAR)(TimeFields.Day & 0xFF) + (UCHAR)(TimeFields.Milliseconds & 0xFF);

        if (DiskEntry->LayoutBuffer->Signature == 0)
        {
            continue;
        }

        /* Check if the signature already exist */
        /* FIXME:
         *   Check also signatures from disks, which are
         *   not visible (bootable) by the bios.
         */
        DiskEntry2 = NULL;
        while ((DiskEntry2 = GetAdjDiskListEntry(&List->DiskListHead, DiskEntry2, TRUE)))
        {
            if (DiskEntry2->DiskStyle == PARTITION_STYLE_GPT)
            {
                DPRINT("GPT-partitioned disk detected, not currently supported by SETUP!\n");
                continue;
            }

            if (DiskEntry != DiskEntry2 &&
                DiskEntry->LayoutBuffer->Signature == DiskEntry2->LayoutBuffer->Signature)
                break;
        }

        if (!DiskEntry2)
            break;
    }
}

static
VOID
UpdateDiskSignatures(
    IN PPARTLIST List)
{
    /* Update each disk */
    PDISKENTRY DiskEntry = NULL;
    while ((DiskEntry = GetAdjDiskListEntry(&List->DiskListHead, DiskEntry, TRUE)))
    {
        if (DiskEntry->DiskStyle == PARTITION_STYLE_GPT)
        {
            DPRINT("GPT-partitioned disk detected, not currently supported by SETUP!\n");
            continue;
        }

        if (DiskEntry->LayoutBuffer &&
            DiskEntry->LayoutBuffer->Signature == 0)
        {
            SetDiskSignature(List, DiskEntry);
            DiskEntry->LayoutBuffer->PartitionEntry[0].RewritePartition = TRUE;
        }
    }
}

static
VOID
UpdateHwDiskNumbers(
    IN PPARTLIST List)
{
    PLIST_ENTRY ListEntry;
    PBIOSDISKENTRY BiosDiskEntry;
    PDISKENTRY DiskEntry;
    ULONG HwAdapterNumber = 0;
    ULONG HwControllerNumber = 0;
    ULONG RemovableDiskCount = 0;

    /*
     * Enumerate the disks recognized by the BIOS and recompute the disk
     * numbers on the system when *ALL* removable disks are not connected.
     * The entries are inserted in increasing order of AdapterNumber,
     * ControllerNumber and DiskNumber.
     */
    for (ListEntry = List->BiosDiskListHead.Flink;
         ListEntry != &List->BiosDiskListHead;
         ListEntry = ListEntry->Flink)
    {
        BiosDiskEntry = CONTAINING_RECORD(ListEntry, BIOSDISKENTRY, ListEntry);
        DiskEntry = BiosDiskEntry->DiskEntry;

        /*
         * If the adapter or controller numbers change, update them and reset
         * the number of removable disks on this adapter/controller.
         */
        if (HwAdapterNumber != BiosDiskEntry->AdapterNumber ||
            HwControllerNumber != BiosDiskEntry->ControllerNumber)
        {
            HwAdapterNumber = BiosDiskEntry->AdapterNumber;
            HwControllerNumber = BiosDiskEntry->ControllerNumber;
            RemovableDiskCount = 0;
        }

        /* Adjust the actual hardware disk number */
        if (DiskEntry)
        {
            ASSERT(DiskEntry->HwDiskNumber == BiosDiskEntry->DiskNumber);

            if (DiskEntry->MediaType == RemovableMedia)
            {
                /* Increase the number of removable disks and set the disk number to zero */
                ++RemovableDiskCount;
                DiskEntry->HwFixedDiskNumber = 0;
            }
            else // if (DiskEntry->MediaType == FixedMedia)
            {
                /* Adjust the fixed disk number, offset by the number of removable disks found before this one */
                DiskEntry->HwFixedDiskNumber = BiosDiskEntry->DiskNumber - RemovableDiskCount;
            }
        }
        else
        {
            DPRINT1("BIOS disk %lu is not recognized by NTOS!\n", BiosDiskEntry->DiskNumber);
        }
    }
}

/****
 ** VOLUME-specific partly (for the FS recognition)
 ****/
static
VOID
AddDiskToList(
    IN HANDLE FileHandle,
    IN ULONG DiskNumber,
    IN PPARTLIST List)
{
    DISK_GEOMETRY DiskGeometry;
    SCSI_ADDRESS ScsiAddress;
    PDISKENTRY DiskEntry;
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;
    PPARTITION_SECTOR Mbr;
    PULONG Buffer;
    LARGE_INTEGER FileOffset;
    WCHAR Identifier[20];
    ULONG Checksum;
    ULONG Signature;
    ULONG i;
    PLIST_ENTRY ListEntry;
    PBIOSDISKENTRY BiosDiskEntry;
    ULONG LayoutBufferSize;
    PDRIVE_LAYOUT_INFORMATION NewLayoutBuffer;

    /* Retrieve the drive geometry */
    Status = NtDeviceIoControlFile(FileHandle,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &Iosb,
                                   IOCTL_DISK_GET_DRIVE_GEOMETRY,
                                   NULL,
                                   0,
                                   &DiskGeometry,
                                   sizeof(DiskGeometry));
    if (!NT_SUCCESS(Status))
        return;

    if (DiskGeometry.MediaType != FixedMedia &&
        DiskGeometry.MediaType != RemovableMedia)
    {
        return;
    }

    /*
     * FIXME: Here we suppose the disk is always SCSI. What if it is
     * of another type? To check this we need to retrieve the name of
     * the driver the disk device belongs to.
     */
    Status = NtDeviceIoControlFile(FileHandle,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &Iosb,
                                   IOCTL_SCSI_GET_ADDRESS,
                                   NULL,
                                   0,
                                   &ScsiAddress,
                                   sizeof(ScsiAddress));
    if (!NT_SUCCESS(Status))
        return;

    /*
     * Check whether the disk is initialized, by looking at its MBR.
     * NOTE that this must be generalized to GPT disks as well!
     */

    Mbr = (PARTITION_SECTOR*)RtlAllocateHeap(ProcessHeap,
                                             0,
                                             DiskGeometry.BytesPerSector);
    if (Mbr == NULL)
        return;

    FileOffset.QuadPart = 0;
    Status = NtReadFile(FileHandle,
                        NULL,
                        NULL,
                        NULL,
                        &Iosb,
                        (PVOID)Mbr,
                        DiskGeometry.BytesPerSector,
                        &FileOffset,
                        NULL);
    if (!NT_SUCCESS(Status))
    {
        RtlFreeHeap(ProcessHeap, 0, Mbr);
        DPRINT1("NtReadFile failed, status=%x\n", Status);
        return;
    }
    Signature = Mbr->Signature;

    /* Calculate the MBR checksum */
    Checksum = 0;
    Buffer = (PULONG)Mbr;
    for (i = 0; i < 128; i++)
    {
        Checksum += Buffer[i];
    }
    Checksum = ~Checksum + 1;

    RtlStringCchPrintfW(Identifier, ARRAYSIZE(Identifier),
                        L"%08x-%08x-%c",
                        Checksum, Signature,
                        (Mbr->Magic == PARTITION_MAGIC) ? L'A' : L'X');
    DPRINT("Identifier: %S\n", Identifier);

    DiskEntry = RtlAllocateHeap(ProcessHeap,
                                HEAP_ZERO_MEMORY,
                                sizeof(DISKENTRY));
    if (DiskEntry == NULL)
    {
        RtlFreeHeap(ProcessHeap, 0, Mbr);
        DPRINT1("Failed to allocate a new disk entry.\n");
        return;
    }

    DiskEntry->PartList = List;

#if 0
    {
        FILE_FS_DEVICE_INFORMATION FileFsDevice;

        /* Query the device for its type */
        Status = NtQueryVolumeInformationFile(FileHandle,
                                              &Iosb,
                                              &FileFsDevice,
                                              sizeof(FileFsDevice),
                                              FileFsDeviceInformation);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Couldn't detect device type for disk %lu of identifier '%S'...\n", DiskNumber, Identifier);
        }
        else
        {
            DPRINT1("Disk %lu : DeviceType: 0x%08x ; Characteristics: 0x%08x\n", DiskNumber, FileFsDevice.DeviceType, FileFsDevice.Characteristics);
        }
    }
    // NOTE: We may also use NtQueryVolumeInformationFile(FileFsDeviceInformation).
#endif
    DiskEntry->MediaType = DiskGeometry.MediaType;
    if (DiskEntry->MediaType == RemovableMedia)
    {
        DPRINT1("Disk %lu of identifier '%S' is removable\n", DiskNumber, Identifier);
    }
    else // if (DiskEntry->MediaType == FixedMedia)
    {
        DPRINT1("Disk %lu of identifier '%S' is fixed\n", DiskNumber, Identifier);
    }

//    DiskEntry->Checksum = Checksum;
//    DiskEntry->Signature = Signature;
    DiskEntry->BiosFound = FALSE;

    /*
     * Check if this disk has a valid MBR: verify its signature,
     * and whether its two first bytes are a valid instruction
     * (related to this, see IsThereAValidBootSector() in partlist.c).
     *
     * See also ntoskrnl/fstub/fstubex.c!FstubDetectPartitionStyle().
     */

    // DiskEntry->NoMbr = (Mbr->Magic != PARTITION_MAGIC || (*(PUSHORT)Mbr->BootCode) == 0x0000);

    /* If we have not the 0xAA55 then it's raw partition */
    if (Mbr->Magic != PARTITION_MAGIC)
    {
        DiskEntry->DiskStyle = PARTITION_STYLE_RAW;
    }
    /* Check partitions types: if first is 0xEE and all the others 0, we have GPT */
    else if (Mbr->Partition[0].PartitionType == EFI_PMBR_OSTYPE_EFI &&
             Mbr->Partition[1].PartitionType == 0 &&
             Mbr->Partition[2].PartitionType == 0 &&
             Mbr->Partition[3].PartitionType == 0)
    {
        DiskEntry->DiskStyle = PARTITION_STYLE_GPT;
    }
    /* Otherwise, partition table is in MBR */
    else
    {
        DiskEntry->DiskStyle = PARTITION_STYLE_MBR;
    }

    /* Free the MBR sector buffer */
    RtlFreeHeap(ProcessHeap, 0, Mbr);


    for (ListEntry = List->BiosDiskListHead.Flink;
         ListEntry != &List->BiosDiskListHead;
         ListEntry = ListEntry->Flink)
    {
        BiosDiskEntry = CONTAINING_RECORD(ListEntry, BIOSDISKENTRY, ListEntry);
        /* FIXME:
         *   Compare the size from bios and the reported size from driver.
         *   If we have more than one disk with a zero or with the same signature
         *   we must create new signatures and reboot. After the reboot,
         *   it is possible to identify the disks.
         */
        if (BiosDiskEntry->Signature == Signature &&
            BiosDiskEntry->Checksum == Checksum &&
            BiosDiskEntry->DiskEntry == NULL)
        {
            if (!DiskEntry->BiosFound)
            {
                DiskEntry->HwAdapterNumber = BiosDiskEntry->AdapterNumber;
                DiskEntry->HwControllerNumber = BiosDiskEntry->ControllerNumber;
                DiskEntry->HwDiskNumber = BiosDiskEntry->DiskNumber;

                if (DiskEntry->MediaType == RemovableMedia)
                {
                    /* Set the removable disk number to zero */
                    DiskEntry->HwFixedDiskNumber = 0;
                }
                else // if (DiskEntry->MediaType == FixedMedia)
                {
                    /* The fixed disk number will later be adjusted using the number of removable disks */
                    DiskEntry->HwFixedDiskNumber = BiosDiskEntry->DiskNumber;
                }

                DiskEntry->BiosFound = TRUE;
                BiosDiskEntry->DiskEntry = DiskEntry;
                break;
            }
            else
            {
                // FIXME: What to do?
                DPRINT1("Disk %lu of identifier '%S' has already been found?!\n", DiskNumber, Identifier);
            }
        }
    }

    if (!DiskEntry->BiosFound)
    {
        DPRINT1("WARNING: Setup could not find a matching BIOS disk entry. Disk %lu may not be bootable by the BIOS!\n", DiskNumber);
    }

    DiskEntry->Cylinders = DiskGeometry.Cylinders.QuadPart;
    DiskEntry->TracksPerCylinder = DiskGeometry.TracksPerCylinder;
    DiskEntry->SectorsPerTrack = DiskGeometry.SectorsPerTrack;
    DiskEntry->BytesPerSector = DiskGeometry.BytesPerSector;

    DPRINT("Cylinders %I64u\n", DiskEntry->Cylinders);
    DPRINT("TracksPerCylinder %lu\n", DiskEntry->TracksPerCylinder);
    DPRINT("SectorsPerTrack %lu\n", DiskEntry->SectorsPerTrack);
    DPRINT("BytesPerSector %lu\n", DiskEntry->BytesPerSector);

    DiskEntry->SectorCount.QuadPart = DiskGeometry.Cylinders.QuadPart *
                                      (ULONGLONG)DiskGeometry.TracksPerCylinder *
                                      (ULONGLONG)DiskGeometry.SectorsPerTrack;

#if 1
    DiskEntry->SectorAlignment = DiskGeometry.SectorsPerTrack;
    DiskEntry->CylinderAlignment = DiskGeometry.TracksPerCylinder *
                                   DiskGeometry.SectorsPerTrack;
#else
    // In diskpart... is it correct?
    DiskEntry->SectorAlignment = (1024 * 1024) / DiskGeometry.BytesPerSector;
    DiskEntry->CylinderAlignment = (1024 * 1024) / DiskGeometry.BytesPerSector;
#endif

    DPRINT("SectorCount %I64u\n", DiskEntry->SectorCount.QuadPart);
    DPRINT("SectorAlignment %lu\n", DiskEntry->SectorAlignment);
    DPRINT("CylinderAlignment: %lu\n", DiskEntry->CylinderAlignment);

    DiskEntry->DiskNumber = DiskNumber;
    DiskEntry->Port = ScsiAddress.PortNumber;
#if 1
    DiskEntry->Bus = ScsiAddress.PathId;
    DiskEntry->Id = ScsiAddress.TargetId;
#else
    // In diskpart... Do we want to store the full SCSI address?
    DiskEntry->PathId = ScsiAddress.PathId;
    DiskEntry->TargetId = ScsiAddress.TargetId;
    DiskEntry->Lun = ScsiAddress.Lun;
#endif

    GetDriverName(DiskEntry);
    /*
     * Actually it would be more correct somehow to use:
     *
     * OBJECT_NAME_INFORMATION NameInfo; // ObjectNameInfo;
     * ULONG ReturnedLength;
     *
     * Status = NtQueryObject(SomeHandleToTheDisk,
     *                        ObjectNameInformation,
     *                        &NameInfo,
     *                        sizeof(NameInfo),
     *                        &ReturnedLength);
     * etc...
     *
     * See examples in https://git.reactos.org/?p=reactos.git;a=blob;f=reactos/ntoskrnl/io/iomgr/error.c;hb=2f3a93ee9cec8322a86bf74b356f1ad83fc912dc#l267
     */

    InitializeListHead(&DiskEntry->PrimaryPartListHead);
    InitializeListHead(&DiskEntry->LogicalPartListHead);

    InsertAscendingList(&List->DiskListHead, DiskEntry, DISKENTRY, ListEntry, DiskNumber);


    /*
     * We now retrieve the disk partition layout
     */

    /*
     * Stop there now if the disk is GPT-partitioned,
     * since we currently do not support such disks.
     */
    if (DiskEntry->DiskStyle == PARTITION_STYLE_GPT)
    {
        DPRINT1("GPT-partitioned disk detected, not currently supported by SETUP!\n");
        return;
    }

    /* Allocate a layout buffer with 4 partition entries first */
    LayoutBufferSize = sizeof(DRIVE_LAYOUT_INFORMATION) +
                       ((4 - ANYSIZE_ARRAY) * sizeof(PARTITION_INFORMATION));
    DiskEntry->LayoutBuffer = RtlAllocateHeap(ProcessHeap,
                                              HEAP_ZERO_MEMORY,
                                              LayoutBufferSize);
    if (DiskEntry->LayoutBuffer == NULL)
    {
        DPRINT1("Failed to allocate the disk layout buffer!\n");
        return;
    }

    /* Keep looping while the drive layout buffer is too small */
    for (;;)
    {
        DPRINT1("Buffer size: %lu\n", LayoutBufferSize);
        Status = NtDeviceIoControlFile(FileHandle,
                                       NULL,
                                       NULL,
                                       NULL,
                                       &Iosb,
                                       IOCTL_DISK_GET_DRIVE_LAYOUT,
                                       NULL,
                                       0,
                                       DiskEntry->LayoutBuffer,
                                       LayoutBufferSize);
        if (NT_SUCCESS(Status))
            break;

        if (Status != STATUS_BUFFER_TOO_SMALL)
        {
            DPRINT1("NtDeviceIoControlFile() failed (Status: 0x%08lx)\n", Status);
            return;
        }

        LayoutBufferSize += 4 * sizeof(PARTITION_INFORMATION);
        NewLayoutBuffer = RtlReAllocateHeap(ProcessHeap,
                                            HEAP_ZERO_MEMORY,
                                            DiskEntry->LayoutBuffer,
                                            LayoutBufferSize);
        if (NewLayoutBuffer == NULL)
        {
            DPRINT1("Failed to reallocate the disk layout buffer!\n");
            return;
        }

        DiskEntry->LayoutBuffer = NewLayoutBuffer;
    }

    DPRINT1("PartitionCount: %lu\n", DiskEntry->LayoutBuffer->PartitionCount);

#ifdef DUMP_PARTITION_TABLE
    DumpPartitionTable(DiskEntry);
#endif

    if (IsSuperFloppy(DiskEntry))
        DPRINT1("Disk %lu is a super-floppy\n", DiskNumber);

    if (DiskEntry->LayoutBuffer->PartitionEntry[0].StartingOffset.QuadPart != 0 &&
        DiskEntry->LayoutBuffer->PartitionEntry[0].PartitionLength.QuadPart != 0 &&
        DiskEntry->LayoutBuffer->PartitionEntry[0].PartitionType != PARTITION_ENTRY_UNUSED)
    {
        if ((DiskEntry->LayoutBuffer->PartitionEntry[0].StartingOffset.QuadPart / DiskEntry->BytesPerSector) % DiskEntry->SectorsPerTrack == 0)
        {
            DPRINT("Use %lu Sector alignment!\n", DiskEntry->SectorsPerTrack);
        }
        else if (DiskEntry->LayoutBuffer->PartitionEntry[0].StartingOffset.QuadPart % (1024 * 1024) == 0)
        {
            DPRINT1("Use megabyte (%lu Sectors) alignment!\n", (1024 * 1024) / DiskEntry->BytesPerSector);
        }
        else
        {
            DPRINT1("No matching alignment found! Partition 1 starts at %I64u\n", DiskEntry->LayoutBuffer->PartitionEntry[0].StartingOffset.QuadPart);
        }
    }
    else
    {
        DPRINT1("No valid partition table found! Use megabyte (%lu Sectors) alignment!\n", (1024 * 1024) / DiskEntry->BytesPerSector);
    }

    if (DiskEntry->LayoutBuffer->PartitionCount == 0)
    {
        DiskEntry->NewDisk = TRUE;
        DiskEntry->LayoutBuffer->PartitionCount = 4;

        for (i = 0; i < 4; i++)
        {
            DiskEntry->LayoutBuffer->PartitionEntry[i].RewritePartition = TRUE;
        }
    }
    else
    {
        /* Enumerate and add the first four primary partitions */
        for (i = 0; i < 4; i++)
        {
            AddPartitionToDisk(DiskNumber, DiskEntry, i, FALSE);
        }

        /* Enumerate and add the remaining partitions as logical ones */
        for (i = 4; i < DiskEntry->LayoutBuffer->PartitionCount; i += 4)
        {
            AddPartitionToDisk(DiskNumber, DiskEntry, i, TRUE);
        }
    }

    ScanForUnpartitionedDiskSpace(DiskEntry);
}

/*
 * Retrieve the system disk, i.e. the fixed disk that is accessible by the
 * firmware during boot time and where the system partition resides.
 * If no system partition has been determined, we retrieve the first disk
 * that verifies the mentioned criteria above.
 */
static
PDISKENTRY
GetSystemDisk(
    IN PPARTLIST List)
{
    PDISKENTRY DiskEntry;

    /* Check for empty disk list */
    if (IsListEmpty(&List->DiskListHead))
        return NULL;

    /*
     * If we already have a system partition, the system disk
     * is the one on which the system partition resides.
     */
    if (List->SystemPartition)
        return List->SystemPartition->DiskEntry;

    /* Loop over the disks and find the correct one */
    DiskEntry = NULL;
    while ((DiskEntry = GetAdjDiskListEntry(&List->DiskListHead, DiskEntry, TRUE)))
    {
        /* The disk must be a fixed disk and be found by the firmware */
        if (DiskEntry->MediaType == FixedMedia && DiskEntry->BiosFound)
            break;
    }
    if (!DiskEntry)
    {
        /* We haven't encountered any suitable disk */
        return NULL;
    }

    if (DiskEntry->DiskStyle == PARTITION_STYLE_GPT)
    {
        DPRINT1("System disk -- GPT-partitioned disk detected, not currently supported by SETUP!\n");
    }

    return DiskEntry;
}

/*
 * Retrieve the actual "active" partition of the given disk.
 * On MBR disks, partition with the Active/Boot flag set;
 * on GPT disks, partition with the correct GUID.
 */
BOOLEAN
IsPartitionActive(
    IN PPARTENTRY PartEntry)
{
    // TODO: Support for GPT disks!

    if (IsContainerPartition(PartEntry->PartitionType))
        return FALSE;

    /* Check if the partition is partitioned, used and active */
    if (PartEntry->IsPartitioned &&
        // !IsContainerPartition(PartEntry->PartitionType) &&
        PartEntry->BootIndicator)
    {
        /* Yes it is */
        ASSERT(PartEntry->PartitionType != PARTITION_ENTRY_UNUSED);
        return TRUE;
    }

    return FALSE;
}

static
PPARTENTRY
GetActiveDiskPartition(
    IN PDISKENTRY DiskEntry)
{
    PPARTENTRY PartEntry;
    PPARTENTRY ActivePartition = NULL;

    /* Check for empty disk list */
    // ASSERT(DiskEntry);
    if (!DiskEntry)
        return NULL;

    /* Check for empty partition list */
    if (IsListEmpty(&DiskEntry->PrimaryPartListHead))
        return NULL;

    if (DiskEntry->DiskStyle == PARTITION_STYLE_GPT)
    {
        DPRINT1("GPT-partitioned disk detected, not currently supported by SETUP!\n");
        return NULL;
    }

    /* Scan all (primary) partitions to find the active disk partition */
    PartEntry = NULL;
    while ((PartEntry = GetAdjPartListEntry(&DiskEntry->PrimaryPartListHead, PartEntry, TRUE)))
    {
        if (IsPartitionActive(PartEntry))
        {
            /* Yes, we've found it */
            ASSERT(DiskEntry == PartEntry->DiskEntry);
            ASSERT(PartEntry->IsPartitioned);

            ActivePartition = PartEntry;

            DPRINT1("Found active system partition %lu in disk %lu, drive letter %C\n",
                    PartEntry->PartitionNumber, DiskEntry->DiskNumber,
                    (PartEntry->Volume.DriveLetter == 0)
                        ? L'-' : PartEntry->Volume.DriveLetter);
            break;
        }
    }

    /* Check if the disk is new and if so, use its first partition as the active system partition */
    if (DiskEntry->NewDisk && ActivePartition != NULL)
    {
        // FIXME: What to do??
        DPRINT1("NewDisk TRUE but already existing active partition?\n");
    }

    /* Return the active partition found (or none) */
    return ActivePartition;
}

PPARTLIST
CreatePartitionList(VOID)
{
    PPARTLIST List;
    PDISKENTRY SystemDisk;
    OBJECT_ATTRIBUTES ObjectAttributes;
    SYSTEM_DEVICE_INFORMATION Sdi;
    IO_STATUS_BLOCK Iosb;
    ULONG ReturnSize;
    NTSTATUS Status;
    ULONG DiskNumber;
    HANDLE FileHandle;
    UNICODE_STRING Name;
    WCHAR Buffer[MAX_PATH];

    List = (PPARTLIST)RtlAllocateHeap(ProcessHeap,
                                      0,
                                      sizeof(PARTLIST));
    if (List == NULL)
        return NULL;

    List->SystemPartition = NULL;

    InitializeListHead(&List->DiskListHead);
    InitializeListHead(&List->BiosDiskListHead);

    /*
     * Enumerate the disks seen by the BIOS; this will be used later
     * to map drives seen by NTOS with their corresponding BIOS names.
     */
    EnumerateBiosDiskEntries(List);

    /* Enumerate disks seen by NTOS */
    Status = NtQuerySystemInformation(SystemDeviceInformation,
                                      &Sdi,
                                      sizeof(Sdi),
                                      &ReturnSize);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtQuerySystemInformation() failed, Status 0x%08lx\n", Status);
        RtlFreeHeap(ProcessHeap, 0, List);
        return NULL;
    }

    for (DiskNumber = 0; DiskNumber < Sdi.NumberOfDisks; DiskNumber++)
    {
        RtlStringCchPrintfW(Buffer, ARRAYSIZE(Buffer),
                            L"\\Device\\Harddisk%lu\\Partition0",
                            DiskNumber);
        RtlInitUnicodeString(&Name, Buffer);

        InitializeObjectAttributes(&ObjectAttributes,
                                   &Name,
                                   OBJ_CASE_INSENSITIVE,
                                   NULL,
                                   NULL);

        Status = NtOpenFile(&FileHandle,
                            FILE_READ_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                            &ObjectAttributes,
                            &Iosb,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            FILE_SYNCHRONOUS_IO_NONALERT);
        if (NT_SUCCESS(Status))
        {
            AddDiskToList(FileHandle, DiskNumber, List);
            NtClose(FileHandle);
        }
    }

    UpdateDiskSignatures(List);
    UpdateHwDiskNumbers(List);
    AssignDriveLetters(List);

    /*
     * Retrieve the system partition: the active partition on the system
     * disk (the one that will be booted by default by the hardware).
     */
    SystemDisk = GetSystemDisk(List);
    List->SystemPartition = (SystemDisk ? GetActiveDiskPartition(SystemDisk) : NULL);

    return List;
}

VOID
DestroyPartitionList(
    IN PPARTLIST List)
{
    PDISKENTRY DiskEntry;
    PBIOSDISKENTRY BiosDiskEntry;
    PPARTENTRY PartEntry;
    PLIST_ENTRY Entry;

    /* Release disk and partition info */
    while (!IsListEmpty(&List->DiskListHead))
    {
        Entry = RemoveHeadList(&List->DiskListHead);
        DiskEntry = CONTAINING_RECORD(Entry, DISKENTRY, ListEntry);

        /* Release driver name */
        RtlFreeUnicodeString(&DiskEntry->DriverName);

        /* Release primary partition list */
        while (!IsListEmpty(&DiskEntry->PrimaryPartListHead))
        {
            Entry = RemoveHeadList(&DiskEntry->PrimaryPartListHead);
            PartEntry = CONTAINING_RECORD(Entry, PARTENTRY, ListEntry);

            RtlFreeHeap(ProcessHeap, 0, PartEntry);
        }

        /* Release logical partition list */
        while (!IsListEmpty(&DiskEntry->LogicalPartListHead))
        {
            Entry = RemoveHeadList(&DiskEntry->LogicalPartListHead);
            PartEntry = CONTAINING_RECORD(Entry, PARTENTRY, ListEntry);

            RtlFreeHeap(ProcessHeap, 0, PartEntry);
        }

        /* Release layout buffer */
        if (DiskEntry->LayoutBuffer != NULL)
            RtlFreeHeap(ProcessHeap, 0, DiskEntry->LayoutBuffer);

        /* Release disk entry */
        RtlFreeHeap(ProcessHeap, 0, DiskEntry);
    }

    /* Release the bios disk info */
    while (!IsListEmpty(&List->BiosDiskListHead))
    {
        Entry = RemoveHeadList(&List->BiosDiskListHead);
        BiosDiskEntry = CONTAINING_RECORD(Entry, BIOSDISKENTRY, ListEntry);

        RtlFreeHeap(ProcessHeap, 0, BiosDiskEntry);
    }

    /* Release list head */
    RtlFreeHeap(ProcessHeap, 0, List);
}

PDISKENTRY
GetDiskByBiosNumber(
    _In_ PPARTLIST List,
    _In_ ULONG HwDiskNumber)
{
    /* Loop over the disks and find the correct one */
    PDISKENTRY DiskEntry = NULL;
    while ((DiskEntry = GetAdjDiskListEntry(&List->DiskListHead, DiskEntry, TRUE)))
    {
        if (DiskEntry->HwDiskNumber == HwDiskNumber)
            break; /* Disk found */
    }

    /* Return the found disk, or NULL if none */
    return DiskEntry;
}

PDISKENTRY
GetDiskByNumber(
    _In_ PPARTLIST List,
    _In_ ULONG DiskNumber)
{
    /* Loop over the disks and find the correct one */
    PDISKENTRY DiskEntry = NULL;
    while ((DiskEntry = GetAdjDiskListEntry(&List->DiskListHead, DiskEntry, TRUE)))
    {
        if (DiskEntry->DiskNumber == DiskNumber)
            break; /* Disk found */
    }

    /* Return the found disk, or NULL if none */
    return DiskEntry;
}

PDISKENTRY
GetDiskBySCSI(
    _In_ PPARTLIST List,
    _In_ USHORT Port,
    _In_ USHORT Bus,
    _In_ USHORT Id)
{
    /* Loop over the disks and find the correct one */
    PDISKENTRY DiskEntry = NULL;
    while ((DiskEntry = GetAdjDiskListEntry(&List->DiskListHead, DiskEntry, TRUE)))
    {
        if (DiskEntry->Port == Port &&
            DiskEntry->Bus  == Bus  &&
            DiskEntry->Id   == Id)
        {
            break; /* Disk found */
        }
    }

    /* Return the found disk, or NULL if none */
    return DiskEntry;
}

PDISKENTRY
GetDiskBySignature(
    _In_ PPARTLIST List,
    _In_ ULONG Signature)
{
    /* Loop over the disks and find the correct one */
    PDISKENTRY DiskEntry = NULL;
    while ((DiskEntry = GetAdjDiskListEntry(&List->DiskListHead, DiskEntry, TRUE)))
    {
        if (DiskEntry->LayoutBuffer->Signature == Signature)
            break; /* Disk found */
    }

    /* Return the found disk, or NULL if none */
    return DiskEntry;
}

PPARTENTRY
GetPartition(
    // _In_ PPARTLIST List,
    _In_ PDISKENTRY DiskEntry,
    _In_ ULONG PartitionNumber)
{
    PPARTENTRY PartEntry;

    if (DiskEntry->DiskStyle == PARTITION_STYLE_GPT)
    {
        DPRINT("GPT-partitioned disk detected, not currently supported by SETUP!\n");
        return NULL;
    }

    /* Disk found, loop over the primary partitions first... */
    PartEntry = NULL;
    while ((PartEntry = GetAdjPartListEntry(&DiskEntry->PrimaryPartListHead, PartEntry, TRUE)))
    {
        if (PartEntry->PartitionNumber == PartitionNumber)
            return PartEntry; /* Partition found */
    }

    /* ... then over the logical partitions if needed */
    PartEntry = NULL;
    while ((PartEntry = GetAdjPartListEntry(&DiskEntry->LogicalPartListHead, PartEntry, TRUE)))
    {
        if (PartEntry->PartitionNumber == PartitionNumber)
            return PartEntry; /* Partition found */
    }

    /* The partition was not found on the disk, stop there */
    return NULL;
}

BOOLEAN
GetDiskOrPartition(
    IN PPARTLIST List,
    IN ULONG DiskNumber,
    IN ULONG PartitionNumber OPTIONAL,
    OUT PDISKENTRY* pDiskEntry,
    OUT PPARTENTRY* pPartEntry OPTIONAL)
{
    PDISKENTRY DiskEntry;
    PPARTENTRY PartEntry = NULL;

    /* Find the disk */
    DiskEntry = GetDiskByNumber(List, DiskNumber);
    if (!DiskEntry)
        return FALSE;

    /* If we have a partition (PartitionNumber != 0), find it */
    if (PartitionNumber != 0)
    {
        if (DiskEntry->DiskStyle == PARTITION_STYLE_GPT)
        {
            DPRINT("GPT-partitioned disk detected, not currently supported by SETUP!\n");
            return FALSE;
        }

        PartEntry = GetPartition(/*List,*/ DiskEntry, PartitionNumber);
        if (!PartEntry)
            return FALSE;
        ASSERT(PartEntry->DiskEntry == DiskEntry);
    }

    /* Return the disk (and optionally the partition) */
    *pDiskEntry = DiskEntry;
    if (pPartEntry) *pPartEntry = PartEntry;
    return TRUE;
}

//
// NOTE: Was introduced broken in r6258 by Casper
//
PPARTENTRY
SelectPartition(
    IN PPARTLIST List,
    IN ULONG DiskNumber,
    IN ULONG PartitionNumber)
{
    PDISKENTRY DiskEntry;
    PPARTENTRY PartEntry;

    DiskEntry = GetDiskByNumber(List, DiskNumber);
    if (!DiskEntry)
        return NULL;

    PartEntry = GetPartition(/*List,*/ DiskEntry, PartitionNumber);
    if (!PartEntry)
        return NULL;

    ASSERT(PartEntry->DiskEntry == DiskEntry);
    ASSERT(DiskEntry->DiskNumber == DiskNumber);
    ASSERT(PartEntry->PartitionNumber == PartitionNumber);

    return PartEntry;
}


static inline
BOOLEAN
IsEmptyLayoutEntry(
    _In_ PPARTITION_INFORMATION PartitionInfo)
{
    return (PartitionInfo->StartingOffset.QuadPart == 0 &&
            PartitionInfo->PartitionLength.QuadPart == 0);
}

static inline
BOOLEAN
IsSamePrimaryLayoutEntry(
    _In_ PPARTITION_INFORMATION PartitionInfo,
    _In_ PPARTENTRY PartEntry)
{
    return ((PartitionInfo->StartingOffset.QuadPart == GetPartEntryOffsetInBytes(PartEntry)) &&
            (PartitionInfo->PartitionLength.QuadPart == GetPartEntrySizeInBytes(PartEntry)));
//        PartitionInfo->PartitionType == PartEntry->PartitionType
}


/**
 * @brief
 * Counts the number of partitioned disk regions in a given disk partition list.
 **/
static
ULONG
GetPartitionCount(
    _In_ PLIST_ENTRY PartListHead)
{
    PPARTENTRY PartEntry;
    ULONG Count = 0;

    PartEntry = NULL;
    while ((PartEntry = GetAdjPartListEntry(PartListHead, PartEntry, TRUE)))
    {
        if (PartEntry->IsPartitioned)
            ++Count;
    }

    return Count;
}

#define GetPrimaryPartitionCount(DiskEntry) \
    GetPartitionCount(&(DiskEntry)->PrimaryPartListHead)

#define GetLogicalPartitionCount(DiskEntry) \
    (((DiskEntry)->DiskStyle == PARTITION_STYLE_MBR) \
        ? GetPartitionCount(&(DiskEntry)->LogicalPartListHead) : 0)


static
BOOLEAN
ReAllocateLayoutBuffer(
    IN PDISKENTRY DiskEntry)
{
    PDRIVE_LAYOUT_INFORMATION NewLayoutBuffer;
    ULONG NewPartitionCount;
    ULONG CurrentPartitionCount = 0;
    ULONG LayoutBufferSize;
    ULONG i;

    DPRINT1("ReAllocateLayoutBuffer()\n");

    NewPartitionCount = 4 + GetLogicalPartitionCount(DiskEntry) * 4;

    if (DiskEntry->LayoutBuffer)
        CurrentPartitionCount = DiskEntry->LayoutBuffer->PartitionCount;

    DPRINT1("CurrentPartitionCount: %lu ; NewPartitionCount: %lu\n",
            CurrentPartitionCount, NewPartitionCount);

    if (CurrentPartitionCount == NewPartitionCount)
        return TRUE;

    LayoutBufferSize = sizeof(DRIVE_LAYOUT_INFORMATION) +
                       ((NewPartitionCount - ANYSIZE_ARRAY) * sizeof(PARTITION_INFORMATION));
    NewLayoutBuffer = RtlReAllocateHeap(ProcessHeap,
                                        HEAP_ZERO_MEMORY,
                                        DiskEntry->LayoutBuffer,
                                        LayoutBufferSize);
    if (NewLayoutBuffer == NULL)
    {
        DPRINT1("Failed to allocate the new layout buffer (size: %lu)\n", LayoutBufferSize);
        return FALSE;
    }

    NewLayoutBuffer->PartitionCount = NewPartitionCount;

    /* If the layout buffer grows, make sure the new (empty) entries are written to the disk */
    if (NewPartitionCount > CurrentPartitionCount)
    {
        for (i = CurrentPartitionCount; i < NewPartitionCount; i++)
        {
            NewLayoutBuffer->PartitionEntry[i].RewritePartition = TRUE;
        }
    }

    DiskEntry->LayoutBuffer = NewLayoutBuffer;

    return TRUE;
}

static
VOID
UpdateDiskLayout(
    IN PDISKENTRY DiskEntry)
{
    PPARTITION_INFORMATION PartitionInfo;
    PPARTITION_INFORMATION LinkInfo = NULL;
    PPARTENTRY PartEntry;
    LARGE_INTEGER HiddenSectors64;
    ULONG Index;
    ULONG PartitionNumber = 1;

    DPRINT1("UpdateDiskLayout()\n");

    if (DiskEntry->DiskStyle == PARTITION_STYLE_GPT)
    {
        DPRINT1("GPT-partitioned disk detected, not currently supported by SETUP!\n");
        return;
    }

    /* Resize the layout buffer if necessary */
    if (ReAllocateLayoutBuffer(DiskEntry) == FALSE)
    {
        DPRINT("ReAllocateLayoutBuffer() failed.\n");
        return;
    }

    /* Update the primary partition table */
    Index = 0;
    PartEntry = NULL;
    while ((PartEntry = GetAdjPartListEntry(&DiskEntry->PrimaryPartListHead, PartEntry, TRUE)))
    {
        if (PartEntry->IsPartitioned)
        {
            ASSERT(PartEntry->PartitionType != PARTITION_ENTRY_UNUSED);

            PartitionInfo = &DiskEntry->LayoutBuffer->PartitionEntry[Index];
            PartEntry->PartitionIndex = Index;

            /* Reset the current partition number only for not-yet written partitions */
            if (PartEntry->New)
                PartEntry->PartitionNumber = 0;

            PartEntry->OnDiskPartitionNumber = (!IsContainerPartition(PartEntry->PartitionType) ? PartitionNumber : 0);

            if (!IsSamePrimaryLayoutEntry(PartitionInfo, PartEntry))
            {
                DPRINT1("Updating primary partition entry %lu\n", Index);

                PartitionInfo->StartingOffset.QuadPart = GetPartEntryOffsetInBytes(PartEntry);
                PartitionInfo->PartitionLength.QuadPart = GetPartEntrySizeInBytes(PartEntry);
                PartitionInfo->HiddenSectors = PartEntry->StartSector.LowPart;
                PartitionInfo->PartitionNumber = PartEntry->PartitionNumber;
                PartitionInfo->PartitionType = PartEntry->PartitionType;
                PartitionInfo->BootIndicator = PartEntry->BootIndicator;
                PartitionInfo->RecognizedPartition = IsRecognizedPartition(PartEntry->PartitionType);
                PartitionInfo->RewritePartition = TRUE;
            }

            if (!IsContainerPartition(PartEntry->PartitionType))
                PartitionNumber++;

            Index++;
        }
    }

    ASSERT(Index <= 4);

    /* Update the logical partition table */
    Index = 4;
    PartEntry = NULL;
    while ((PartEntry = GetAdjPartListEntry(&DiskEntry->LogicalPartListHead, PartEntry, TRUE)))
    {
        if (PartEntry->IsPartitioned)
        {
            ASSERT(PartEntry->PartitionType != PARTITION_ENTRY_UNUSED);

            PartitionInfo = &DiskEntry->LayoutBuffer->PartitionEntry[Index];
            PartEntry->PartitionIndex = Index;

            /* Reset the current partition number only for not-yet written partitions */
            if (PartEntry->New)
                PartEntry->PartitionNumber = 0;

            PartEntry->OnDiskPartitionNumber = PartitionNumber;

            DPRINT1("Updating logical partition entry %lu\n", Index);

            PartitionInfo->StartingOffset.QuadPart = GetPartEntryOffsetInBytes(PartEntry);
            PartitionInfo->PartitionLength.QuadPart = GetPartEntrySizeInBytes(PartEntry);
            PartitionInfo->HiddenSectors = DiskEntry->SectorAlignment;
            PartitionInfo->PartitionNumber = PartEntry->PartitionNumber;
            PartitionInfo->PartitionType = PartEntry->PartitionType;
            PartitionInfo->BootIndicator = FALSE;
            PartitionInfo->RecognizedPartition = IsRecognizedPartition(PartEntry->PartitionType);
            PartitionInfo->RewritePartition = TRUE;

            /* Fill the link entry of the previous partition entry */
            if (LinkInfo != NULL)
            {
                LinkInfo->StartingOffset.QuadPart = (PartEntry->StartSector.QuadPart - DiskEntry->SectorAlignment) * DiskEntry->BytesPerSector;
                LinkInfo->PartitionLength.QuadPart = (PartEntry->StartSector.QuadPart + DiskEntry->SectorAlignment) * DiskEntry->BytesPerSector;
                HiddenSectors64.QuadPart = PartEntry->StartSector.QuadPart - DiskEntry->SectorAlignment - DiskEntry->ExtendedPartition->StartSector.QuadPart;
                LinkInfo->HiddenSectors = HiddenSectors64.LowPart;
                LinkInfo->PartitionNumber = 0;

                if (PartEntry->StartSector.QuadPart < 1450560)
                {
                    /* Partition starts below the 8.4GB boundary ==> CHS partition */
                    LinkInfo->PartitionType = PARTITION_EXTENDED;
                }
                else
                {
                    /* Partition starts above the 8.4GB boundary ==> LBA partition */
                    LinkInfo->PartitionType = PARTITION_XINT13_EXTENDED;
                }

                LinkInfo->BootIndicator = FALSE;
                LinkInfo->RecognizedPartition = FALSE;
                LinkInfo->RewritePartition = TRUE;
            }

            /* Save a pointer to the link entry of the current partition entry */
            LinkInfo = &DiskEntry->LayoutBuffer->PartitionEntry[Index + 1];

            PartitionNumber++;
            Index += 4;
        }
    }

    /* Wipe unused primary partition entries */
    for (Index = GetPrimaryPartitionCount(DiskEntry); Index < 4; Index++)
    {
        DPRINT1("Primary partition entry %lu\n", Index);

        PartitionInfo = &DiskEntry->LayoutBuffer->PartitionEntry[Index];

        if (!IsEmptyLayoutEntry(PartitionInfo))
        {
            DPRINT1("Wiping primary partition entry %lu\n", Index);

            PartitionInfo->StartingOffset.QuadPart = 0;
            PartitionInfo->PartitionLength.QuadPart = 0;
            PartitionInfo->HiddenSectors = 0;
            PartitionInfo->PartitionNumber = 0;
            PartitionInfo->PartitionType = PARTITION_ENTRY_UNUSED;
            PartitionInfo->BootIndicator = FALSE;
            PartitionInfo->RecognizedPartition = FALSE;
            PartitionInfo->RewritePartition = TRUE;
        }
    }

    /* Wipe unused logical partition entries */
    for (Index = 4; Index < DiskEntry->LayoutBuffer->PartitionCount; Index++)
    {
        if (Index % 4 >= 2)
        {
            DPRINT1("Logical partition entry %lu\n", Index);

            PartitionInfo = &DiskEntry->LayoutBuffer->PartitionEntry[Index];

            if (!IsEmptyLayoutEntry(PartitionInfo))
            {
                DPRINT1("Wiping partition entry %lu\n", Index);

                PartitionInfo->StartingOffset.QuadPart = 0;
                PartitionInfo->PartitionLength.QuadPart = 0;
                PartitionInfo->HiddenSectors = 0;
                PartitionInfo->PartitionNumber = 0;
                PartitionInfo->PartitionType = PARTITION_ENTRY_UNUSED;
                PartitionInfo->BootIndicator = FALSE;
                PartitionInfo->RecognizedPartition = FALSE;
                PartitionInfo->RewritePartition = TRUE;
            }
        }
    }

    // HACK: See the FIXMEs in WritePartitions(): (Re)set the PartitionStyle to MBR.
    DiskEntry->DiskStyle = PARTITION_STYLE_MBR;

    DiskEntry->Dirty = TRUE;

#ifdef DUMP_PARTITION_TABLE
    DumpPartitionTable(DiskEntry);
#endif
}

/**
 * @brief
 * Retrieves the adjacent (next or previous) disk region,
 * in case it is unpartitioned.
 *
 * @param[in]   Direction
 * TRUE or FALSE to search the next or previous entry, respectively.
 *
 * @return  The adjacent unpartitioned entry, if it exists, or NULL.
 **/
static inline
PPARTENTRY
GetAdjUnpartitionedEntry(
    _In_ PPARTENTRY PartEntry,
    _In_ BOOLEAN Direction)
{
    PDISKENTRY DiskEntry = PartEntry->DiskEntry;
    PLIST_ENTRY PartListHead;

    /* In case of MBR disks only, check for logical partitions */
    if ((DiskEntry->DiskStyle == PARTITION_STYLE_MBR) &&
        (PartEntry->LogicalPartition))
    {
        PartListHead = &DiskEntry->LogicalPartListHead;
    }
    else
    {
        PartListHead = &DiskEntry->PrimaryPartListHead;
    }

    PartEntry = GetAdjPartListEntry(PartListHead, PartEntry, Direction);
    if (PartEntry && !PartEntry->IsPartitioned)
    {
        ASSERT(PartEntry->PartitionType == PARTITION_ENTRY_UNUSED);
        return PartEntry;
    }
    return NULL;
}

ERROR_NUMBER
PartitionCreationChecks(
    _In_ PPARTENTRY PartEntry)
{
    PDISKENTRY DiskEntry = PartEntry->DiskEntry;

    if (DiskEntry->DiskStyle == PARTITION_STYLE_GPT)
    {
        DPRINT1("GPT-partitioned disk detected, not currently supported by SETUP!\n");
        return ERROR_WARN_PARTITION;
    }

    /* Fail if the partition is already in use */
    if (PartEntry->IsPartitioned)
        return ERROR_NEW_PARTITION;

    /*
     * For primary partitions
     */
    if (!PartEntry->LogicalPartition)
    {
        /* Only one primary partition is allowed on super-floppy */
        if (IsSuperFloppy(DiskEntry))
            return ERROR_PARTITION_TABLE_FULL;

        /* Fail if there are already 4 primary partitions in the list */
        if (GetPrimaryPartitionCount(DiskEntry) >= 4)
            return ERROR_PARTITION_TABLE_FULL;
    }
    /*
     * For logical partitions
     */
    else
    {
        // TODO: Check that we are inside an extended partition!!
        // Then the following check will be useless.

        /* Only one (primary) partition is allowed on super-floppy */
        if (IsSuperFloppy(DiskEntry))
            return ERROR_PARTITION_TABLE_FULL;
    }

    return ERROR_SUCCESS;
}

ERROR_NUMBER
ExtendedPartitionCreationChecks(
    _In_ PPARTENTRY PartEntry)
{
    PDISKENTRY DiskEntry = PartEntry->DiskEntry;

    if (DiskEntry->DiskStyle == PARTITION_STYLE_GPT)
    {
        DPRINT1("GPT-partitioned disk detected, not currently supported by SETUP!\n");
        return ERROR_WARN_PARTITION;
    }

    /* Fail if the partition is already in use */
    if (PartEntry->IsPartitioned)
        return ERROR_NEW_PARTITION;

    /* Only one primary partition is allowed on super-floppy */
    if (IsSuperFloppy(DiskEntry))
        return ERROR_PARTITION_TABLE_FULL;

    /* Fail if there are already 4 primary partitions in the list */
    if (GetPrimaryPartitionCount(DiskEntry) >= 4)
        return ERROR_PARTITION_TABLE_FULL;

    /* Fail if there is another extended partition in the list */
    if (DiskEntry->ExtendedPartition != NULL)
        return ERROR_ONLY_ONE_EXTENDED;

    return ERROR_SUCCESS;
}

BOOLEAN
CreatePartition(
    _In_ PPARTLIST List,
    _Inout_ PPARTENTRY PartEntry,
    _In_opt_ ULONGLONG SizeBytes)
{
    ERROR_NUMBER Error;
    ULONGLONG SectorCount;

    DPRINT1("CreatePartition(%I64u bytes)\n", SizeBytes);

    if (List == NULL || PartEntry == NULL ||
        PartEntry->DiskEntry == NULL || PartEntry->IsPartitioned)
    {
        return FALSE;
    }

    Error = PartitionCreationChecks(PartEntry);
    if (Error != NOT_AN_ERROR)
    {
        DPRINT1("PartitionCreationChecks() failed with error %lu\n", Error);
        return FALSE;
    }

    ASSERT(PartEntry->SectorCount.QuadPart);

    /* Convert to sector count. SizeBytes being zero means
     * the caller wants to use all the empty space. */
    if ((SizeBytes == 0) || (SizeBytes == GetPartEntrySizeInBytes(PartEntry)))
    {
        /* Use all of the unpartitioned disk space */
        SectorCount = PartEntry->SectorCount.QuadPart;
    }
    else
    {
        /* Calculate the sector count from the size in bytes,
         * but never get larger than the unpartitioned disk space */
        SectorCount = SizeBytes / PartEntry->DiskEntry->BytesPerSector;
        SectorCount = min(SectorCount, PartEntry->SectorCount.QuadPart);
        if (SectorCount == 0)
        {
            /* SizeBytes was certainly less than the minimal size, so fail */
            DPRINT1("Partition size %I64u too small\n", SizeBytes);
            return FALSE;
        }
    }
    DPRINT1("    SectorCount: %I64u\n", SectorCount);

    /* Initialize the partition entry, inserting a new blank region if needed */
    if (!InitializePartitionEntry(PartEntry, SectorCount))
        return FALSE;

    UpdateDiskLayout(PartEntry->DiskEntry);
    AssignDriveLetters(List);

    return TRUE;
}

static
VOID
AddLogicalDiskSpace(
    _In_ PDISKENTRY DiskEntry)
{
    ULONGLONG StartSector;
    ULONGLONG SectorCount;
    PPARTENTRY NewPartEntry;

    DPRINT1("AddLogicalDiskSpace()\n");

    /* Create a partition entry that represents the empty space in the container partition */

    StartSector = DiskEntry->ExtendedPartition->StartSector.QuadPart + (ULONGLONG)DiskEntry->SectorAlignment;
    SectorCount = DiskEntry->ExtendedPartition->SectorCount.QuadPart - (ULONGLONG)DiskEntry->SectorAlignment;

    NewPartEntry = CreateInsertBlankRegion(DiskEntry,
                                           &DiskEntry->LogicalPartListHead,
                                           StartSector,
                                           SectorCount,
                                           TRUE);
    if (NewPartEntry == NULL)
    {
        DPRINT1("Failed to create a new empty region for extended partition space!\n");
        return;
    }
}

BOOLEAN
CreateExtendedPartition(
    _In_ PPARTLIST List,
    _Inout_ PPARTENTRY PartEntry,
    _In_opt_ ULONGLONG SizeBytes)
{
    ERROR_NUMBER Error;
    ULONGLONG SectorCount;

    DPRINT1("CreateExtendedPartition(%I64u bytes)\n", SizeBytes);

    if (List == NULL || PartEntry == NULL ||
        PartEntry->DiskEntry == NULL || PartEntry->IsPartitioned)
    {
        return FALSE;
    }

    Error = ExtendedPartitionCreationChecks(PartEntry);
    if (Error != NOT_AN_ERROR)
    {
        DPRINT1("ExtendedPartitionCreationChecks() failed with error %lu\n", Error);
        return FALSE;
    }

    ASSERT(PartEntry->SectorCount.QuadPart);

    /* Convert to sector count. SizeBytes being zero means
     * the caller wants to use all the empty space. */
    if ((SizeBytes == 0) || (SizeBytes == GetPartEntrySizeInBytes(PartEntry)))
    {
        /* Use all of the unpartitioned disk space */
        SectorCount = PartEntry->SectorCount.QuadPart;
    }
    else
    {
        /* Calculate the sector count from the size in bytes,
         * but never get larger than the unpartitioned disk space */
        SectorCount = SizeBytes / PartEntry->DiskEntry->BytesPerSector;
        SectorCount = min(SectorCount, PartEntry->SectorCount.QuadPart);
        if (SectorCount == 0)
        {
            /* SizeBytes was certainly less than the minimal size, so fail */
            DPRINT1("Partition size %I64u too small\n", SizeBytes);
            return FALSE;
        }
    }
    DPRINT1("    SectorCount: %I64u\n", SectorCount);

    /* Initialize the partition entry, inserting a new blank region if needed */
    if (!InitializePartitionEntry(PartEntry, SectorCount))
        return FALSE;

    ASSERT(PartEntry->LogicalPartition == FALSE);

    if (PartEntry->StartSector.QuadPart < 1450560)
    {
        /* Partition starts below the 8.4GB boundary ==> CHS partition */
        PartEntry->PartitionType = PARTITION_EXTENDED;
    }
    else
    {
        /* Partition starts above the 8.4GB boundary ==> LBA partition */
        PartEntry->PartitionType = PARTITION_XINT13_EXTENDED;
    }

    ASSERT(PartEntry->IsPartitioned &&
           !PartEntry->LogicalPartition &&
           IsContainerPartition(PartEntry->PartitionType));

    PartEntry->DiskEntry->ExtendedPartition = PartEntry;

    AddLogicalDiskSpace(PartEntry->DiskEntry);

    UpdateDiskLayout(PartEntry->DiskEntry);
    AssignDriveLetters(List);

    return TRUE;
}

NTSTATUS
DismountVolume(
    _In_ PVOLINFO VolumeEntry)
{
    NTSTATUS Status;
    NTSTATUS LockStatus;
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE PartitionHandle;

    /* Check whether the volume was mounted by the system */
    if (!*VolumeEntry->DeviceName ||
        VolumeEntry->FormatState == UnknownFormat ||
        // NOTE: If FormatState == Unformatted but *FileSystem != 0 this means
        // it has been usually mounted with RawFS and thus needs to be dismounted.
        !*VolumeEntry->FileSystem)
    {
        /* The volume is not mounted, just return success */
        return STATUS_SUCCESS;
    }

    /* Open the volume */
    RtlInitUnicodeString(&Name, VolumeEntry->DeviceName);
    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);

    Status = NtOpenFile(&PartitionHandle,
                        GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR: Cannot open volume %wZ for dismounting! (Status 0x%lx)\n", &Name, Status);
        return Status;
    }

    // FIXME: Should we do that **ONLY** if the dismount command succeeded?
    /* Reset some volume information */
    VolumeEntry->DriveLetter = L'\0';
    VolumeEntry->FormatState = Unformatted; // UnknownFormat
    VolumeEntry->FileSystem[0] = L'\0';
    RtlZeroMemory(VolumeEntry->VolumeLabel, sizeof(VolumeEntry->VolumeLabel));
    // VolumeEntry->New = FALSE;
    VolumeEntry->NeedsCheck = FALSE;

    /* Lock the volume */
    LockStatus = NtFsControlFile(PartitionHandle,
                                 NULL,
                                 NULL,
                                 NULL,
                                 &IoStatusBlock,
                                 FSCTL_LOCK_VOLUME,
                                 NULL,
                                 0,
                                 NULL,
                                 0);
    if (!NT_SUCCESS(LockStatus))
    {
        DPRINT1("WARNING: Failed to lock volume! Operations may fail! (Status 0x%lx)\n", LockStatus);
    }

    /* Dismount the volume */
    Status = NtFsControlFile(PartitionHandle,
                             NULL,
                             NULL,
                             NULL,
                             &IoStatusBlock,
                             FSCTL_DISMOUNT_VOLUME,
                             NULL,
                             0,
                             NULL,
                             0);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to unmount volume (Status 0x%lx)\n", Status);
    }

    /* Unlock the volume */
    LockStatus = NtFsControlFile(PartitionHandle,
                                 NULL,
                                 NULL,
                                 NULL,
                                 &IoStatusBlock,
                                 FSCTL_UNLOCK_VOLUME,
                                 NULL,
                                 0,
                                 NULL,
                                 0);
    if (!NT_SUCCESS(LockStatus))
    {
        DPRINT1("Failed to unlock volume (Status 0x%lx)\n", LockStatus);
    }

    /* Close the volume */
    NtClose(PartitionHandle);

    return Status;
}

BOOLEAN
DeletePartition(
    IN PPARTLIST List,
    IN PPARTENTRY PartEntry,
    OUT PPARTENTRY* FreeRegion OPTIONAL)
{
    PDISKENTRY DiskEntry;
    PPARTENTRY PrevPartEntry;
    PPARTENTRY NextPartEntry;
    PPARTENTRY LogicalPartEntry;
    PLIST_ENTRY Entry;

    if (List == NULL || PartEntry == NULL ||
        PartEntry->DiskEntry == NULL || PartEntry->IsPartitioned == FALSE)
    {
        return FALSE;
    }

    ASSERT(PartEntry->PartitionType != PARTITION_ENTRY_UNUSED);

    /* Clear the system partition if it is being deleted */
    if (List->SystemPartition == PartEntry)
    {
        ASSERT(List->SystemPartition);
        List->SystemPartition = NULL;
    }

    DiskEntry = PartEntry->DiskEntry;

    /* Check which type of partition (primary/logical or extended) is being deleted */
    if (DiskEntry->ExtendedPartition == PartEntry)
    {
        /* An extended partition is being deleted: delete all logical partition entries */
        while (!IsListEmpty(&DiskEntry->LogicalPartListHead))
        {
            Entry = RemoveHeadList(&DiskEntry->LogicalPartListHead);
            LogicalPartEntry = CONTAINING_RECORD(Entry, PARTENTRY, ListEntry);

            //
            // FIXME: This whole check thing will be useless soon...
            //
            /* Check whether the partition is valid and was mounted by the system */
            if (PartEntry->IsPartitioned &&
                !IsContainerPartition(PartEntry->PartitionType) &&
                IsRecognizedPartition(PartEntry->PartitionType) &&
                (PartEntry->Volume.FormatState != UnknownFormat) &&
                // NOTE: If FormatState == Unformatted but *FileSystem != 0 this means
                // it has been usually mounted with RawFS and thus needs to be dismounted.
                *PartEntry->Volume.FileSystem &&
                PartEntry->PartitionNumber != 0)
            {
                /* The partition is mounted */
                ASSERT(PartEntry->PartitionType != PARTITION_ENTRY_UNUSED);

                /* Dismount the logical partition */
                DismountVolume(&LogicalPartEntry->Volume);
            }

            /* Delete it */
            RtlFreeHeap(ProcessHeap, 0, LogicalPartEntry);
        }

        DiskEntry->ExtendedPartition = NULL;
    }
    else
    {
        //
        // FIXME: This whole check thing will be useless soon...
        //
        /* Check whether the partition is valid and was mounted by the system */
        if (PartEntry->IsPartitioned &&
            !IsContainerPartition(PartEntry->PartitionType) &&
            IsRecognizedPartition(PartEntry->PartitionType) &&
            (PartEntry->Volume.FormatState != UnknownFormat) &&
            // NOTE: If FormatState == Unformatted but *FileSystem != 0 this means
            // it has been usually mounted with RawFS and thus needs to be dismounted.
            *PartEntry->Volume.FileSystem &&
            PartEntry->PartitionNumber != 0)
        {
            /* The partition is mounted */
            ASSERT(PartEntry->PartitionType != PARTITION_ENTRY_UNUSED);

            /* A primary partition is being deleted: dismount it */
            DismountVolume(&PartEntry->Volume);
        }
    }

    /* Adjust the unpartitioned disk space entries */

    /* Get pointer to previous and next unpartitioned entries */
    PrevPartEntry = GetAdjUnpartitionedEntry(PartEntry, FALSE);
    NextPartEntry = GetAdjUnpartitionedEntry(PartEntry, TRUE);

    if (PrevPartEntry != NULL && NextPartEntry != NULL)
    {
        /* Merge the previous, current and next unpartitioned entries */

        /* Adjust the previous entry length */
        PrevPartEntry->SectorCount.QuadPart += (PartEntry->SectorCount.QuadPart + NextPartEntry->SectorCount.QuadPart);

        /* Remove the current and next entries */
        RemoveEntryList(&PartEntry->ListEntry);
        RtlFreeHeap(ProcessHeap, 0, PartEntry);
        RemoveEntryList(&NextPartEntry->ListEntry);
        RtlFreeHeap(ProcessHeap, 0, NextPartEntry);

        /* Optionally return the freed region */
        if (FreeRegion)
            *FreeRegion = PrevPartEntry;
    }
    else if (PrevPartEntry != NULL && NextPartEntry == NULL)
    {
        /* Merge the current and the previous unpartitioned entries */

        /* Adjust the previous entry length */
        PrevPartEntry->SectorCount.QuadPart += PartEntry->SectorCount.QuadPart;

        /* Remove the current entry */
        RemoveEntryList(&PartEntry->ListEntry);
        RtlFreeHeap(ProcessHeap, 0, PartEntry);

        /* Optionally return the freed region */
        if (FreeRegion)
            *FreeRegion = PrevPartEntry;
    }
    else if (PrevPartEntry == NULL && NextPartEntry != NULL)
    {
        /* Merge the current and the next unpartitioned entries */

        /* Adjust the next entry offset and length */
        NextPartEntry->StartSector.QuadPart = PartEntry->StartSector.QuadPart;
        NextPartEntry->SectorCount.QuadPart += PartEntry->SectorCount.QuadPart;

        /* Remove the current entry */
        RemoveEntryList(&PartEntry->ListEntry);
        RtlFreeHeap(ProcessHeap, 0, PartEntry);

        /* Optionally return the freed region */
        if (FreeRegion)
            *FreeRegion = NextPartEntry;
    }
    else
    {
        /* Nothing to merge but change the current entry */
        PartEntry->IsPartitioned = FALSE;
        PartEntry->OnDiskPartitionNumber = 0;
        PartEntry->PartitionNumber = 0;
        // PartEntry->PartitionIndex = 0;
        PartEntry->BootIndicator = FALSE;
        PartEntry->PartitionType = PARTITION_ENTRY_UNUSED;

        RtlZeroMemory(&PartEntry->Volume, sizeof(PartEntry->Volume));

        /* Optionally return the freed region */
        if (FreeRegion)
            *FreeRegion = PartEntry;
    }

    UpdateDiskLayout(DiskEntry);
    AssignDriveLetters(List);

    return TRUE;
}

static
BOOLEAN
IsSupportedActivePartition(
    IN PPARTENTRY PartEntry)
{
    PVOLINFO VolumeEntry;

    /* Check the type and the file system of this partition */

    /*
     * We do not support extended partition containers (on MBR disks) marked
     * as active, and containing code inside their extended boot records.
     */
    if (IsContainerPartition(PartEntry->PartitionType))
    {
        DPRINT1("System partition %lu in disk %lu is an extended partition container?!\n",
                PartEntry->PartitionNumber, PartEntry->DiskEntry->DiskNumber);
        return FALSE;
    }

    VolumeEntry = &PartEntry->Volume;
    if (!VolumeEntry)
    {
        /* Still no recognizable volume mounted: partition not supported */
        return FALSE;
    }

    /*
     * ADDITIONAL CHECKS / BIG HACK:
     *
     * Retrieve its file system and check whether we have
     * write support for it. If that is the case we are fine
     * and we can use it directly. However if we don't have
     * write support we will need to change the active system
     * partition.
     *
     * NOTE that this is completely useless on architectures
     * where a real system partition is required, as on these
     * architectures the partition uses the FAT FS, for which
     * we do have write support.
     * NOTE also that for those architectures looking for a
     * partition boot indicator is insufficient.
     */
    if (VolumeEntry->FormatState == Unformatted)
    {
        /* If this partition is mounted, it would use RawFS ("RAW") */
        return TRUE;
    }
    else if (VolumeEntry->FormatState == Formatted)
    {
        ASSERT(*VolumeEntry->FileSystem);

        /* NOTE: Please keep in sync with the RegisteredFileSystems list! */
        if (wcsicmp(VolumeEntry->FileSystem, L"FAT")   == 0 ||
            wcsicmp(VolumeEntry->FileSystem, L"FAT32") == 0 ||
         // wcsicmp(VolumeEntry->FileSystem, L"NTFS")  == 0 ||
            wcsicmp(VolumeEntry->FileSystem, L"BTRFS") == 0)
        {
            return TRUE;
        }
        else
        {
            // WARNING: We cannot write on this FS yet!
            DPRINT1("Recognized file system '%S' that doesn't have write support yet!\n",
                    VolumeEntry->FileSystem);
            return FALSE;
        }
    }
    else // if (VolumeEntry->FormatState == UnknownFormat)
    {
        ASSERT(!*VolumeEntry->FileSystem);

        DPRINT1("System partition %lu in disk %lu with no or unknown FS?!\n",
                PartEntry->PartitionNumber, PartEntry->DiskEntry->DiskNumber);
        return FALSE;
    }

    // HACK: WARNING: We cannot write on this FS yet!
    // See fsutil.c:InferFileSystem()
    if (PartEntry->PartitionType == PARTITION_IFS)
    {
        DPRINT1("Recognized file system '%S' that doesn't have write support yet!\n",
                VolumeEntry->FileSystem);
        return FALSE;
    }

    return TRUE;
}

PPARTENTRY
FindSupportedSystemPartition(
    IN PPARTLIST List,
    IN BOOLEAN ForceSelect,
    IN PDISKENTRY AlternativeDisk OPTIONAL,
    IN PPARTENTRY AlternativePart OPTIONAL)
{
    // PLIST_ENTRY ListEntry;
    PDISKENTRY DiskEntry;
    PPARTENTRY PartEntry;
    PPARTENTRY ActivePartition;
    PPARTENTRY CandidatePartition = NULL;

    /* Check for empty disk list */
    if (IsListEmpty(&List->DiskListHead))
    {
        /* No system partition! */
        ASSERT(List->SystemPartition == NULL);
        goto NoSystemPartition;
    }

    /* Adjust the optional alternative disk if needed */
    if (!AlternativeDisk && AlternativePart)
        AlternativeDisk = AlternativePart->DiskEntry;

    /* Ensure that the alternative partition is on the alternative disk */
    if (AlternativePart)
        ASSERT(AlternativeDisk && (AlternativePart->DiskEntry == AlternativeDisk));

    /* Ensure that the alternative disk is in the list */
    if (AlternativeDisk)
        ASSERT(AlternativeDisk->PartList == List);

    /* Start fresh */
    CandidatePartition = NULL;

//
// Step 1 : Check the system disk.
//

    /*
     * First, check whether the system disk, i.e. the one that will be booted
     * by default by the hardware, contains an active partition. If so this
     * should be our system partition.
     */
    DiskEntry = GetSystemDisk(List);

    if (DiskEntry->DiskStyle == PARTITION_STYLE_GPT)
    {
        DPRINT1("System disk -- GPT-partitioned disk detected, not currently supported by SETUP!\n");
        goto UseAlternativeDisk;
    }

    /* If we have a system partition (in the system disk), validate it */
    ActivePartition = List->SystemPartition;
    if (ActivePartition && IsSupportedActivePartition(ActivePartition))
    {
        CandidatePartition = ActivePartition;

        DPRINT1("Use the current system partition %lu in disk %lu, drive letter %C\n",
                CandidatePartition->PartitionNumber,
                CandidatePartition->DiskEntry->DiskNumber,
                (CandidatePartition->Volume.DriveLetter == 0)
                    ? L'-' : CandidatePartition->Volume.DriveLetter);

        /* Return the candidate system partition */
        return CandidatePartition;
    }

    /* If the system disk is not the optional alternative disk, perform the minimal checks */
    if (DiskEntry != AlternativeDisk)
    {
        /*
         * No active partition has been recognized. Enumerate all the (primary)
         * partitions in the system disk, excluding the possible current active
         * partition, to find a new candidate.
         */
        PartEntry = NULL;
        while ((PartEntry = GetAdjDiskRegion(DiskEntry, PartEntry,
                                             ENUM_REGION_NEXT | ENUM_REGION_MBR_PRIMARY_ONLY)))
        // for (ListEntry = DiskEntry->PrimaryPartListHead.Flink;
        //      ListEntry != &DiskEntry->PrimaryPartListHead;
        //      ListEntry = ListEntry->Flink)
        {
            // /* Retrieve the partition */
            // PartEntry = CONTAINING_RECORD(ListEntry, PARTENTRY, ListEntry);

            /* Skip the current active partition */
            if (PartEntry == ActivePartition)
                continue;

            /* Check if the partition is partitioned and used */
            if (PartEntry->IsPartitioned &&
                !IsContainerPartition(PartEntry->PartitionType))
            {
                ASSERT(PartEntry->PartitionType != PARTITION_ENTRY_UNUSED);

                /* If we get a candidate active partition in the disk, validate it */
                if (IsSupportedActivePartition(PartEntry))
                {
                    CandidatePartition = PartEntry;
                    goto UseAlternativePartition;
                }
            }

#if 0
            /* Check if the partition is partitioned and used */
            if (!PartEntry->IsPartitioned)
            {
                ASSERT(PartEntry->PartitionType == PARTITION_ENTRY_UNUSED);

                // TODO: Check for minimal size!!
                CandidatePartition = PartEntry;
                goto UseAlternativePartition;
            }
#endif
        }

        /*
         * Still nothing, look whether there is some free space that we can use
         * for the new system partition. We must be sure that the total number
         * of partition is less than the maximum allowed, and that the minimal
         * size is fine.
         */
//
// TODO: Fix the handling of system partition being created in unpartitioned space!!
// --> When to partition it? etc...
//
        if (GetPrimaryPartitionCount(DiskEntry) < 4)
        {
            PartEntry = NULL;
            while ((PartEntry = GetAdjDiskRegion(DiskEntry, PartEntry,
                                                 ENUM_REGION_NEXT | ENUM_REGION_MBR_PRIMARY_ONLY)))
            // for (ListEntry = DiskEntry->PrimaryPartListHead.Flink;
            //      ListEntry != &DiskEntry->PrimaryPartListHead;
            //      ListEntry = ListEntry->Flink)
            {
                // /* Retrieve the partition */
                // PartEntry = CONTAINING_RECORD(ListEntry, PARTENTRY, ListEntry);

                /* Skip the current active partition */
                if (PartEntry == ActivePartition)
                    continue;

                /* Check for unpartitioned space */
                if (!PartEntry->IsPartitioned)
                {
                    ASSERT(PartEntry->PartitionType == PARTITION_ENTRY_UNUSED);

                    // TODO: Check for minimal size!!
                    CandidatePartition = PartEntry;
                    goto UseAlternativePartition;
                }
            }
        }
    }


//
// Step 2 : No active partition found: Check the alternative disk if specified.
//

UseAlternativeDisk:
    if (!AlternativeDisk || (!ForceSelect && (DiskEntry != AlternativeDisk)))
        goto NoSystemPartition;

    if (AlternativeDisk->DiskStyle == PARTITION_STYLE_GPT)
    {
        DPRINT1("Alternative disk -- GPT-partitioned disk detected, not currently supported by SETUP!\n");
        goto NoSystemPartition;
    }

    if (DiskEntry != AlternativeDisk)
    {
        /* Choose the alternative disk */
        DiskEntry = AlternativeDisk;

        /* If we get a candidate active partition, validate it */
        ActivePartition = GetActiveDiskPartition(DiskEntry);
        if (ActivePartition && IsSupportedActivePartition(ActivePartition))
        {
            CandidatePartition = ActivePartition;
            goto UseAlternativePartition;
        }
    }

    /* We now may have an unsupported active partition, or none */

/***
 *** TODO: Improve the selection:
 *** - If we want a really separate system partition from the partition where
 ***   we install, do something similar to what's done below in the code.
 *** - Otherwise if we allow for the system partition to be also the partition
 ***   where we install, just directly fall down to using AlternativePart.
 ***/

    /* Retrieve the first partition of the disk */
    // PartEntry = CONTAINING_RECORD(DiskEntry->PrimaryPartListHead.Flink,
    //                               PARTENTRY, ListEntry);
    PartEntry = GetAdjDiskRegion(DiskEntry, NULL,
                                 ENUM_REGION_NEXT | ENUM_REGION_MBR_PRIMARY_ONLY);
    ASSERT(DiskEntry == PartEntry->DiskEntry);

    CandidatePartition = PartEntry;

    //
    // See: https://svn.reactos.org/svn/reactos/trunk/reactos/base/setup/usetup/partlist.c?r1=63355&r2=63354&pathrev=63355#l2318
    //

    /* Check if the disk is new and if so, use its first partition as the active system partition */
    if (DiskEntry->NewDisk)
    {
        // !IsContainerPartition(PartEntry->PartitionType);
        if (!CandidatePartition->IsPartitioned || !CandidatePartition->BootIndicator) /* CandidatePartition != ActivePartition */
        {
            ASSERT(DiskEntry == CandidatePartition->DiskEntry);

            DPRINT1("Use new first active system partition %lu in disk %lu, drive letter %C\n",
                    CandidatePartition->PartitionNumber,
                    CandidatePartition->DiskEntry->DiskNumber,
                    (CandidatePartition->Volume.DriveLetter == 0)
                        ? L'-' : CandidatePartition->Volume.DriveLetter);

            /* Return the candidate system partition */
            return CandidatePartition;
        }

        // FIXME: What to do??
        DPRINT1("NewDisk TRUE but first partition is used?\n");
    }

    /*
     * The disk is not new, check if any partition is initialized;
     * if not, the first one becomes the system partition.
     */
    PartEntry = NULL;
    while ((PartEntry = GetAdjDiskRegion(DiskEntry, PartEntry,
                                         ENUM_REGION_NEXT | ENUM_REGION_MBR_PRIMARY_ONLY)))
    // for (ListEntry = DiskEntry->PrimaryPartListHead.Flink;
    //      ListEntry != &DiskEntry->PrimaryPartListHead;
    //      ListEntry = ListEntry->Flink)
    {
        // /* Retrieve the partition */
        // PartEntry = CONTAINING_RECORD(ListEntry, PARTENTRY, ListEntry);

        /* Check if the partition is partitioned and is used */
        // !IsContainerPartition(PartEntry->PartitionType);
        if (/* PartEntry->IsPartitioned && */
            PartEntry->PartitionType != PARTITION_ENTRY_UNUSED || PartEntry->BootIndicator)
        {
            break;
        }
    }
    // if (ListEntry == &DiskEntry->PrimaryPartListHead)
    if (!PartEntry)
    {
        /*
         * OK we haven't encountered any used and active partition,
         * so use the first one as the system partition.
         */
        ASSERT(DiskEntry == CandidatePartition->DiskEntry);

        DPRINT1("Use first active system partition %lu in disk %lu, drive letter %C\n",
                CandidatePartition->PartitionNumber,
                CandidatePartition->DiskEntry->DiskNumber,
                (CandidatePartition->Volume.DriveLetter == 0)
                    ? L'-' : CandidatePartition->Volume.DriveLetter);

        /* Return the candidate system partition */
        return CandidatePartition;
    }

    /*
     * The disk is not new, we did not find any actual active partition,
     * or the one we found was not supported, or any possible other candidate
     * is not supported. We then use the alternative partition if specified.
     */
    if (AlternativePart)
    {
        DPRINT1("No valid or supported system partition has been found, use the alternative partition!\n");
        CandidatePartition = AlternativePart;
        goto UseAlternativePartition;
    }
    else
    {
NoSystemPartition:
        DPRINT1("No valid or supported system partition has been found on this system!\n");
        return NULL;
    }

UseAlternativePartition:
    /*
     * We are here because we did not find any (active) candidate system
     * partition that we know how to support. What we are going to do is
     * to change the existing system partition and use the alternative partition
     * (e.g. on which we install ReactOS) as the new system partition.
     * Then we will need to add in FreeLdr's boot menu an entry for booting
     * from the original system partition.
     */
    ASSERT(CandidatePartition);

    DPRINT1("Use alternative active system partition %lu in disk %lu, drive letter %C\n",
            CandidatePartition->PartitionNumber,
            CandidatePartition->DiskEntry->DiskNumber,
            (CandidatePartition->Volume.DriveLetter == 0)
                ? L'-' : CandidatePartition->Volume.DriveLetter);

    /* Return the candidate system partition */
    return CandidatePartition;
}

BOOLEAN
SetActivePartition(
    IN PPARTLIST List,
    IN PPARTENTRY PartEntry,
    IN PPARTENTRY OldActivePart OPTIONAL)
{
    /* Check for empty disk list */
    if (IsListEmpty(&List->DiskListHead))
        return FALSE;

    /* Validate the partition entry */
    if (!PartEntry)
        return FALSE;

    /*
     * If the partition entry is already the system partition, or if it is
     * the same as the old active partition hint the user provided (and if
     * it is already active), just return success.
     */
    if ((PartEntry == List->SystemPartition) ||
        ((PartEntry == OldActivePart) && IsPartitionActive(OldActivePart)))
    {
        return TRUE;
    }

    ASSERT(PartEntry->DiskEntry);

    /* Ensure that the partition's disk is in the list */
    ASSERT(PartEntry->DiskEntry->PartList == List);

    /*
     * If the user provided an old active partition hint, verify that it is
     * indeeed active and belongs to the same disk where the new partition
     * belongs. Otherwise determine the current active partition on the disk
     * where the new partition belongs.
     */
    if (!(OldActivePart && IsPartitionActive(OldActivePart) && (OldActivePart->DiskEntry == PartEntry->DiskEntry)))
    {
        /* It's not, determine the current active partition for the disk */
        OldActivePart = GetActiveDiskPartition(PartEntry->DiskEntry);
    }

    /* Unset the old active partition if it exists */
    if (OldActivePart)
    {
        OldActivePart->BootIndicator = FALSE;
        OldActivePart->DiskEntry->LayoutBuffer->PartitionEntry[OldActivePart->PartitionIndex].BootIndicator = FALSE;
        OldActivePart->DiskEntry->LayoutBuffer->PartitionEntry[OldActivePart->PartitionIndex].RewritePartition = TRUE;
        OldActivePart->DiskEntry->Dirty = TRUE;
    }

    /* Modify the system partition if the new partition is on the system disk */
    if (PartEntry->DiskEntry == GetSystemDisk(List))
        List->SystemPartition = PartEntry;

    /* Set the new active partition */
    PartEntry->BootIndicator = TRUE;
    PartEntry->DiskEntry->LayoutBuffer->PartitionEntry[PartEntry->PartitionIndex].BootIndicator = TRUE;
    PartEntry->DiskEntry->LayoutBuffer->PartitionEntry[PartEntry->PartitionIndex].RewritePartition = TRUE;
    PartEntry->DiskEntry->Dirty = TRUE;

    return TRUE;
}

NTSTATUS
WritePartitions(
    IN PDISKENTRY DiskEntry)
{
    NTSTATUS Status;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING Name;
    HANDLE FileHandle;
    IO_STATUS_BLOCK Iosb;
    ULONG BufferSize;
    PPARTITION_INFORMATION PartitionInfo;
    ULONG PartitionCount;
    PPARTENTRY PartEntry;
    WCHAR DstPath[MAX_PATH];

    DPRINT("WritePartitions() Disk: %lu\n", DiskEntry->DiskNumber);

    /* If the disk is not dirty, there is nothing to do */
    if (!DiskEntry->Dirty)
        return STATUS_SUCCESS;

    RtlStringCchPrintfW(DstPath, ARRAYSIZE(DstPath),
                        L"\\Device\\Harddisk%lu\\Partition0",
                        DiskEntry->DiskNumber);
    RtlInitUnicodeString(&Name, DstPath);

    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);

    Status = NtOpenFile(&FileHandle,
                        GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE,
                        &ObjectAttributes,
                        &Iosb,
                        0,
                        FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtOpenFile() failed (Status %lx)\n", Status);
        return Status;
    }

#ifdef DUMP_PARTITION_TABLE
    DumpPartitionTable(DiskEntry);
#endif

    //
    // FIXME: We first *MUST* use IOCTL_DISK_CREATE_DISK to initialize
    // the disk in MBR or GPT format in case the disk was not initialized!!
    // For this we must ask the user which format to use.
    //

    /* Save the original partition count to be restored later (see comment below) */
    PartitionCount = DiskEntry->LayoutBuffer->PartitionCount;

    /* Set the new disk layout and retrieve its updated version with possibly modified partition numbers */
    BufferSize = sizeof(DRIVE_LAYOUT_INFORMATION) +
                 ((PartitionCount - 1) * sizeof(PARTITION_INFORMATION));
    Status = NtDeviceIoControlFile(FileHandle,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &Iosb,
                                   IOCTL_DISK_SET_DRIVE_LAYOUT,
                                   DiskEntry->LayoutBuffer,
                                   BufferSize,
                                   DiskEntry->LayoutBuffer,
                                   BufferSize);
    NtClose(FileHandle);

    /*
     * IOCTL_DISK_SET_DRIVE_LAYOUT calls IoWritePartitionTable(), which converts
     * DiskEntry->LayoutBuffer->PartitionCount into a partition *table* count,
     * where such a table is expected to enumerate up to 4 partitions:
     * partition *table* count == ROUND_UP(PartitionCount, 4) / 4 .
     * Due to this we need to restore the original PartitionCount number.
     */
    DiskEntry->LayoutBuffer->PartitionCount = PartitionCount;

    /* Check whether the IOCTL_DISK_SET_DRIVE_LAYOUT call succeeded */
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IOCTL_DISK_SET_DRIVE_LAYOUT failed (Status 0x%08lx)\n", Status);
        return Status;
    }

#ifdef DUMP_PARTITION_TABLE
    DumpPartitionTable(DiskEntry);
#endif

    /* Update the partition numbers */

#if 0
    /* Update the primary partition table */
    PartEntry = NULL;
    while ((PartEntry = GetAdjPartListEntry(&DiskEntry->PrimaryPartListHead, PartEntry, TRUE)))
    {
        PartEntry->New = FALSE;
        if (PartEntry->IsPartitioned)
        {
            ASSERT(PartEntry->PartitionType != PARTITION_ENTRY_UNUSED);
            PartitionInfo = &DiskEntry->LayoutBuffer->PartitionEntry[PartEntry->PartitionIndex];
            PartEntry->PartitionNumber = PartitionInfo->PartitionNumber;
        }
    }

    /* Update the logical partition table */
    PartEntry = NULL;
    while ((PartEntry = GetAdjPartListEntry(&DiskEntry->LogicalPartListHead, PartEntry, TRUE)))
    {
        PartEntry->New = FALSE;
        if (PartEntry->IsPartitioned)
        {
            ASSERT(PartEntry->PartitionType != PARTITION_ENTRY_UNUSED);
            PartitionInfo = &DiskEntry->LayoutBuffer->PartitionEntry[PartEntry->PartitionIndex];
            PartEntry->PartitionNumber = PartitionInfo->PartitionNumber;
        }
    }
#else
    /* Update the partition table */
    PartEntry = NULL;
    while ((PartEntry = GetAdjDiskRegion(DiskEntry, PartEntry,
                                         ENUM_REGION_NEXT | ENUM_REGION_PARTITIONED)))
    {
        // ASSERT(PartEntry->IsPartitioned);
        // ASSERT(PartEntry->PartitionType != PARTITION_ENTRY_UNUSED);
        PartEntry->New = FALSE;
        PartitionInfo = &DiskEntry->LayoutBuffer->PartitionEntry[PartEntry->PartitionIndex];
        PartEntry->PartitionNumber = PartitionInfo->PartitionNumber;
    }
#endif

    //
    // NOTE: Originally (see r40437), we used to install here also a new MBR
    // for this disk (by calling InstallMbrBootCodeToDisk), only if:
    // DiskEntry->NewDisk == TRUE and DiskEntry->HwDiskNumber == 0.
    // Then after that, both DiskEntry->NewDisk and DiskEntry->NoMbr were set
    // to FALSE. In the other place (in usetup.c) where InstallMbrBootCodeToDisk
    // was called too, the installation test was modified by checking whether
    // DiskEntry->NoMbr was TRUE (instead of NewDisk).
    //

    // HACK: Parts of FIXMEs described above: (Re)set the PartitionStyle to MBR.
    DiskEntry->DiskStyle = PARTITION_STYLE_MBR;

    /* The layout has been successfully updated, the disk is not dirty anymore */
    DiskEntry->Dirty = FALSE;

    return Status;
}

BOOLEAN
WritePartitionsToDisk(
    IN PPARTLIST List)
{
    NTSTATUS Status;
    PDISKENTRY DiskEntry;

    if (List == NULL)
        return TRUE;

    DiskEntry = NULL;
    while ((DiskEntry = GetAdjDiskListEntry(&List->DiskListHead, DiskEntry, TRUE)))
    {
        if (DiskEntry->DiskStyle == PARTITION_STYLE_GPT)
        {
            DPRINT("GPT-partitioned disk detected, not currently supported by SETUP!\n");
            continue;
        }

        if (DiskEntry->Dirty != FALSE)
        {
            Status = WritePartitions(DiskEntry);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("WritePartitionsToDisk() failed to update disk %lu, Status 0x%08lx\n",
                        DiskEntry->DiskNumber, Status);
            }
        }
    }

    return TRUE;
}

BOOLEAN
SetMountedDeviceValue(
    IN WCHAR Letter,
    IN ULONG Signature,
    IN LARGE_INTEGER StartingOffset)
{
    NTSTATUS Status;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING KeyName = RTL_CONSTANT_STRING(L"SYSTEM\\MountedDevices");
    UNICODE_STRING ValueName;
    WCHAR ValueNameBuffer[16];
    HANDLE KeyHandle;
    REG_DISK_MOUNT_INFO MountInfo;

    RtlStringCchPrintfW(ValueNameBuffer, ARRAYSIZE(ValueNameBuffer),
                        L"\\DosDevices\\%c:", Letter);
    RtlInitUnicodeString(&ValueName, ValueNameBuffer);

    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE,
                               GetRootKeyByPredefKey(HKEY_LOCAL_MACHINE, NULL),
                               NULL);

    Status = NtOpenKey(&KeyHandle,
                       KEY_ALL_ACCESS,
                       &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        Status = NtCreateKey(&KeyHandle,
                             KEY_ALL_ACCESS,
                             &ObjectAttributes,
                             0,
                             NULL,
                             REG_OPTION_NON_VOLATILE,
                             NULL);
    }
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtCreateKey() failed (Status %lx)\n", Status);
        return FALSE;
    }

    MountInfo.Signature = Signature;
    MountInfo.StartingOffset = StartingOffset;
    Status = NtSetValueKey(KeyHandle,
                           &ValueName,
                           0,
                           REG_BINARY,
                           (PVOID)&MountInfo,
                           sizeof(MountInfo));
    NtClose(KeyHandle);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtSetValueKey() failed (Status %lx)\n", Status);
        return FALSE;
    }

    return TRUE;
}

BOOLEAN
SetMountedDeviceValues(
    IN PPARTLIST List)
{
    PDISKENTRY DiskEntry;
    PPARTENTRY PartEntry;
    LARGE_INTEGER StartingOffset;

    if (List == NULL)
        return FALSE;

    DiskEntry = NULL;
    while ((DiskEntry = GetAdjDiskListEntry(&List->DiskListHead, DiskEntry, TRUE)))
    {
        if (DiskEntry->DiskStyle == PARTITION_STYLE_GPT)
        {
            DPRINT("GPT-partitioned disk detected, not currently supported by SETUP!\n");
            continue;
        }

#if 0
        PartEntry = NULL;
        while ((PartEntry = GetAdjPartListEntry(&DiskEntry->PrimaryPartListHead, PartEntry, TRUE)))
        {
            if (PartEntry->IsPartitioned) // && !IsContainerPartition(PartEntry->PartitionType)
            {
                ASSERT(PartEntry->PartitionType != PARTITION_ENTRY_UNUSED);

                /* Assign a "\DosDevices\#:" mount point to this partition */
                if (PartEntry->Volume.DriveLetter)
                {
                    StartingOffset.QuadPart = GetPartEntryOffsetInBytes(PartEntry);
                    if (!SetMountedDeviceValue(PartEntry->Volume.DriveLetter,
                                               DiskEntry->LayoutBuffer->Signature,
                                               StartingOffset))
                    {
                        return FALSE;
                    }
                }
            }
        }

        PartEntry = NULL;
        while ((PartEntry = GetAdjPartListEntry(&DiskEntry->LogicalPartListHead, PartEntry, TRUE)))
        {
            if (PartEntry->IsPartitioned) // && !IsContainerPartition(PartEntry->PartitionType)
            {
                ASSERT(PartEntry->PartitionType != PARTITION_ENTRY_UNUSED);

                /* Assign a "\DosDevices\#:" mount point to this partition */
                if (PartEntry->Volume.DriveLetter)
                {
                    StartingOffset.QuadPart = GetPartEntryOffsetInBytes(PartEntry);
                    if (!SetMountedDeviceValue(PartEntry->Volume.DriveLetter,
                                               DiskEntry->LayoutBuffer->Signature,
                                               StartingOffset))
                    {
                        return FALSE;
                    }
                }
            }
        }
#else
        PartEntry = NULL;
        while ((PartEntry = GetAdjDiskRegion(DiskEntry, PartEntry,
                                             ENUM_REGION_NEXT | ENUM_REGION_PARTITIONED)))
        {
            // ASSERT(PartEntry->IsPartitioned);
            // ASSERT(PartEntry->PartitionType != PARTITION_ENTRY_UNUSED);
            /**/ASSERT(!IsContainerPartition(PartEntry->PartitionType));/**/

            /* Assign a "\DosDevices\#:" mount point to this partition */
            if (PartEntry->Volume.DriveLetter)
            {
                StartingOffset.QuadPart = GetPartEntryOffsetInBytes(PartEntry);
                if (!SetMountedDeviceValue(PartEntry->Volume.DriveLetter,
                                           DiskEntry->LayoutBuffer->Signature,
                                           StartingOffset))
                {
                    return FALSE;
                }
            }
        }
#endif
    }

    return TRUE;
}

VOID
SetMBRPartitionType(
    IN PPARTENTRY PartEntry,
    IN UCHAR PartitionType)
{
    PDISKENTRY DiskEntry = PartEntry->DiskEntry;

    ASSERT(DiskEntry->DiskStyle == PARTITION_STYLE_MBR);

    PartEntry->PartitionType = PartitionType;

    DiskEntry->Dirty = TRUE;
    DiskEntry->LayoutBuffer->PartitionEntry[PartEntry->PartitionIndex].PartitionType = PartitionType;
    DiskEntry->LayoutBuffer->PartitionEntry[PartEntry->PartitionIndex].RecognizedPartition = IsRecognizedPartition(PartitionType);
    DiskEntry->LayoutBuffer->PartitionEntry[PartEntry->PartitionIndex].RewritePartition = TRUE;
}

/* EOF */
