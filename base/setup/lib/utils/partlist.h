/*
 * PROJECT:     ReactOS Setup Library
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Partition list functions
 * COPYRIGHT:   Copyright 2003-2019 Casper S. Hornstrup (chorns@users.sourceforge.net)
 *              Copyright 2018-2019 Hermes Belusca-Maito
 */

#pragma once

/* EXTRA HANDFUL MACROS *****************************************************/

// NOTE: They should be moved into some global header.

// /* We have to define it there, because it is not in the MS DDK */
// #define PARTITION_LINUX 0x83

/* OEM MBR partition types recognized by NT (see [MS-DMRP] Appendix B) */
#define PARTITION_EISA          0x12    // EISA partition
#define PARTITION_HIBERNATION   0x84    // Hibernation partition for laptops
#define PARTITION_DIAGNOSTIC    0xA0    // Diagnostic partition on some Hewlett-Packard (HP) notebooks
#define PARTITION_DELL          0xDE    // Dell partition
#define PARTITION_IBM           0xFE    // IBM Initial Microprogram Load (IML) partition

#define IsOEMPartition(PartitionType) \
    ( ((PartitionType) == PARTITION_EISA)        || \
      ((PartitionType) == PARTITION_HIBERNATION) || \
      ((PartitionType) == PARTITION_DIAGNOSTIC)  || \
      ((PartitionType) == PARTITION_DELL)        || \
      ((PartitionType) == PARTITION_IBM) )


/* PARTITION UTILITY FUNCTIONS **********************************************/

typedef enum _FORMATSTATE
{
    Unformatted,
    UnformattedOrDamaged,
    UnknownFormat,
    Formatted
} FORMATSTATE, *PFORMATSTATE;

typedef struct _VOLINFO
{
    // LIST_ENTRY ListEntry;

    // WCHAR VolumeName[MAX_PATH]; // Name in the DOS/Win32 namespace
    WCHAR DeviceName[MAX_PATH]; // NT device name

    WCHAR DriveLetter;
    WCHAR VolumeLabel[20];
    WCHAR FileSystem[MAX_PATH+1];
    FORMATSTATE FormatState;

/** The following properties may be replaced by flags **/

    /* Volume is new and has not yet been actually formatted and mounted */
    BOOLEAN New;

    /* Volume must be checked */
    BOOLEAN NeedsCheck;

} VOLINFO, *PVOLINFO;

typedef struct _PARTENTRY
{
    LIST_ENTRY ListEntry;

    /* The disk this partition belongs to */
    struct _DISKENTRY *DiskEntry;

    /* Partition geometry */
    ULARGE_INTEGER StartSector;
    ULARGE_INTEGER SectorCount;

    BOOLEAN BootIndicator;  // NOTE: See comment for the PARTLIST::SystemPartition member.
    UCHAR PartitionType;
    ULONG OnDiskPartitionNumber; /* Enumerated partition number (primary partitions first, excluding the extended partition container, then the logical partitions) */
    ULONG PartitionNumber;       /* Current partition number, only valid for the currently running NTOS instance */
    ULONG PartitionIndex;        /* Index in the LayoutBuffer->PartitionEntry[] cached array of the corresponding DiskEntry */

    BOOLEAN LogicalPartition;

    /* Partition is partitioned disk space */
    BOOLEAN IsPartitioned;

/** The following three properties may be replaced by flags **/

    /* Partition is new, table does not exist on disk yet */
    BOOLEAN New;

    /* Partition was created automatically */
    BOOLEAN AutoCreate; // FIXME: This is a HACK only for Setup!

    /* Volume-related properties */
    VOLINFO Volume; // FIXME: Do it differently later

} PARTENTRY, *PPARTENTRY;

typedef struct _DISKENTRY
{
    LIST_ENTRY ListEntry;

    /* The list of disks/partitions this disk belongs to */
    struct _PARTLIST *PartList;

    MEDIA_TYPE MediaType;   /* FixedMedia or RemovableMedia */

    /* Disk geometry */

    ULONGLONG Cylinders;
    ULONG TracksPerCylinder;
    ULONG SectorsPerTrack;
    ULONG BytesPerSector;

    ULARGE_INTEGER SectorCount;
    ULONG SectorAlignment;
    ULONG CylinderAlignment;

    /* BIOS Firmware parameters */
    BOOLEAN BiosFound;
    ULONG HwAdapterNumber;
    ULONG HwControllerNumber;
    ULONG HwDiskNumber;         /* Disk number currently assigned on the system */
    ULONG HwFixedDiskNumber;    /* Disk number on the system when *ALL* removable disks are not connected */
//    ULONG Signature;  // Obtained from LayoutBuffer->Signature
//    ULONG Checksum;

    /* SCSI parameters */
    ULONG DiskNumber;
//  SCSI_ADDRESS;
    USHORT Port;
    USHORT Bus;  // PathId;
    USHORT Id;   // TargetId;
    // USHORT Lun;

    /* Has the partition list been modified? */
    BOOLEAN Dirty;

    BOOLEAN NewDisk; /* If TRUE, the disk is uninitialized */
    PARTITION_STYLE DiskStyle;  /* MBR/GPT-partitioned disk, or uninitialized disk (RAW) */

    UNICODE_STRING DriverName;

    PDRIVE_LAYOUT_INFORMATION LayoutBuffer;
    // TODO: When adding support for GPT disks:
    // Use PDRIVE_LAYOUT_INFORMATION_EX which indicates whether
    // the disk is MBR, GPT, or unknown (uninitialized).
    // Depending on the style, either use the MBR or GPT partition info.

    LIST_ENTRY PrimaryPartListHead; /* List of primary partitions */
    LIST_ENTRY LogicalPartListHead; /* List of logical partitions (Valid only for MBR-partitioned disks) */

    /* Pointer to the unique extended partition on this disk (Valid only for MBR-partitioned disks) */
    PPARTENTRY ExtendedPartition;

} DISKENTRY, *PDISKENTRY;

typedef struct _BIOSDISKENTRY
{
    LIST_ENTRY ListEntry;
    ULONG AdapterNumber;
    ULONG ControllerNumber;
    ULONG DiskNumber;
    ULONG Signature;
    ULONG Checksum;
    PDISKENTRY DiskEntry;   /* Corresponding recognized disk; is NULL if the disk is not recognized */ // RecognizedDiskEntry;
    CM_DISK_GEOMETRY_DEVICE_DATA DiskGeometry;
    CM_INT13_DRIVE_PARAMETER Int13DiskData;
} BIOSDISKENTRY, *PBIOSDISKENTRY;

typedef struct _PARTLIST
{
    /*
     * The system partition where the boot manager resides.
     * The corresponding system disk is obtained via:
     *    SystemPartition->DiskEntry.
     */
    // NOTE: It seems to appear that the specifications of ARC and (u)EFI
    // actually allow for multiple system partitions to exist on the system.
    // If so we should instead rely on the BootIndicator bit of the PARTENTRY
    // structure in order to find these.
    PPARTENTRY SystemPartition;

    LIST_ENTRY DiskListHead;
    LIST_ENTRY BiosDiskListHead;

} PARTLIST, *PPARTLIST;

#define  PARTITION_TBL_SIZE 4

#define PARTITION_MAGIC     0xAA55

/* Defines system type for MBR showing that a GPT is following */
#define EFI_PMBR_OSTYPE_EFI 0xEE

#include <pshpack1.h>

typedef struct _PARTITION
{
    unsigned char   BootFlags;        /* bootable?  0=no, 128=yes  */
    unsigned char   StartingHead;     /* beginning head number */
    unsigned char   StartingSector;   /* beginning sector number */
    unsigned char   StartingCylinder; /* 10 bit nmbr, with high 2 bits put in begsect */
    unsigned char   PartitionType;    /* Operating System type indicator code */
    unsigned char   EndingHead;       /* ending head number */
    unsigned char   EndingSector;     /* ending sector number */
    unsigned char   EndingCylinder;   /* also a 10 bit nmbr, with same high 2 bit trick */
    unsigned int  StartingBlock;      /* first sector relative to start of disk */
    unsigned int  SectorCount;        /* number of sectors in partition */
} PARTITION, *PPARTITION;

typedef struct _PARTITION_SECTOR
{
    UCHAR BootCode[440];                     /* 0x000 */
    ULONG Signature;                         /* 0x1B8 */
    UCHAR Reserved[2];                       /* 0x1BC */
    PARTITION Partition[PARTITION_TBL_SIZE]; /* 0x1BE */
    USHORT Magic;                            /* 0x1FE */
} PARTITION_SECTOR, *PPARTITION_SECTOR;

#include <poppack.h>

typedef struct
{
    LIST_ENTRY ListEntry;
    ULONG DiskNumber;
    ULONG Identifier;
    ULONG Signature;
} BIOS_DISK, *PBIOS_DISK;



ULONGLONG
AlignDown(
    IN ULONGLONG Value,
    IN ULONG Alignment);

ULONGLONG
AlignUp(
    IN ULONGLONG Value,
    IN ULONG Alignment);

ULONGLONG
RoundingDivide(
   IN ULONGLONG Dividend,
   IN ULONGLONG Divisor);


#define GetPartEntryOffsetInBytes(PartEntry) \
    ((PartEntry)->StartSector.QuadPart * (PartEntry)->DiskEntry->BytesPerSector)

#define GetPartEntrySizeInBytes(PartEntry) \
    ((PartEntry)->SectorCount.QuadPart * (PartEntry)->DiskEntry->BytesPerSector)

#define GetDiskSizeInBytes(DiskEntry) \
    ((DiskEntry)->SectorCount.QuadPart * (DiskEntry)->BytesPerSector)


#define ENUM_REGION_NEXT                0x00 //< Enumerate the next region (default)
#define ENUM_REGION_PREV                0x01 //< Enumerate the previous region
#define ENUM_REGION_PARTITIONED         0x02 //< Enumerate only partitioned regions (otherwise, enumerate all regions, including free space)
// 0x04, 0x08 reserved
#define ENUM_REGION_MBR_PRIMARY_ONLY    0x10 //< MBR disks only: Enumerate only primary regions
#define ENUM_REGION_MBR_LOGICAL_ONLY    0x20 //< MBR disks only: Enumerate only logical regions
#define ENUM_REGION_MBR_BY_ORDER        0x40 //< MBR disks only: Enumerate by order on disk (may traverse extended partitions to enumerate the logical ones in-between), instead of by type (first all primary, then all logical)
/*
they are listed in actual
order of appearance on a given disk. For example for MBR disks, all
_primary_ partitions are enumerated first, before _logical_ partitions.
*/
// 0x80 reserved

PPARTENTRY
GetAdjDiskRegion(
    _In_opt_ PDISKENTRY CurrentDisk,
    _In_opt_ PPARTENTRY CurrentPart,
    _In_ ULONG EnumFlags);

PPARTENTRY
GetAdjPartition(
    _In_ PPARTLIST List,
    _In_opt_ PPARTENTRY CurrentPart,
    _In_ ULONG EnumFlags);



BOOLEAN
IsSuperFloppy(
    IN PDISKENTRY DiskEntry);

BOOLEAN
IsPartitionActive(
    IN PPARTENTRY PartEntry);

PPARTLIST
CreatePartitionList(VOID);

VOID
DestroyPartitionList(
    IN PPARTLIST List);

PDISKENTRY
GetDiskByBiosNumber(
    _In_ PPARTLIST List,
    _In_ ULONG HwDiskNumber);

PDISKENTRY
GetDiskByNumber(
    _In_ PPARTLIST List,
    _In_ ULONG DiskNumber);

PDISKENTRY
GetDiskBySCSI(
    _In_ PPARTLIST List,
    _In_ USHORT Port,
    _In_ USHORT Bus,
    _In_ USHORT Id);

PDISKENTRY
GetDiskBySignature(
    _In_ PPARTLIST List,
    _In_ ULONG Signature);

PPARTENTRY
GetPartition(
    // _In_ PPARTLIST List,
    _In_ PDISKENTRY DiskEntry,
    _In_ ULONG PartitionNumber);

BOOLEAN
GetDiskOrPartition(
    IN PPARTLIST List,
    IN ULONG DiskNumber,
    IN ULONG PartitionNumber OPTIONAL,
    OUT PDISKENTRY* pDiskEntry,
    OUT PPARTENTRY* pPartEntry OPTIONAL);

PPARTENTRY
SelectPartition(
    IN PPARTLIST List,
    IN ULONG DiskNumber,
    IN ULONG PartitionNumber);

ERROR_NUMBER
PartitionCreationChecks(
    _In_ PPARTENTRY PartEntry);

ERROR_NUMBER
ExtendedPartitionCreationChecks(
    _In_ PPARTENTRY PartEntry);

BOOLEAN
CreatePartition(
    _In_ PPARTLIST List,
    _Inout_ PPARTENTRY PartEntry,
    _In_opt_ ULONGLONG SizeBytes);

BOOLEAN
CreateExtendedPartition(
    _In_ PPARTLIST List,
    _Inout_ PPARTENTRY PartEntry,
    _In_opt_ ULONGLONG SizeBytes);

NTSTATUS
MountVolume(
    _Inout_ PVOLINFO VolumeEntry,
    _In_opt_ UCHAR MbrPartitionType);

NTSTATUS
DismountVolume(
    _In_ PVOLINFO VolumeEntry);

BOOLEAN
DeletePartition(
    IN PPARTLIST List,
    IN PPARTENTRY PartEntry,
    OUT PPARTENTRY* FreeRegion OPTIONAL);

PPARTENTRY
FindSupportedSystemPartition(
    IN PPARTLIST List,
    IN BOOLEAN ForceSelect,
    IN PDISKENTRY AlternativeDisk OPTIONAL,
    IN PPARTENTRY AlternativePart OPTIONAL);

BOOLEAN
SetActivePartition(
    IN PPARTLIST List,
    IN PPARTENTRY PartEntry,
    IN PPARTENTRY OldActivePart OPTIONAL);

NTSTATUS
WritePartitions(
    IN PDISKENTRY DiskEntry);

BOOLEAN
WritePartitionsToDisk(
    IN PPARTLIST List);

BOOLEAN
SetMountedDeviceValue(
    IN WCHAR Letter,
    IN ULONG Signature,
    IN LARGE_INTEGER StartingOffset);

BOOLEAN
SetMountedDeviceValues(
    IN PPARTLIST List);

VOID
SetMBRPartitionType(
    IN PPARTENTRY PartEntry,
    IN UCHAR PartitionType);

/* EOF */
