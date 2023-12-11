/*
 *  ReactOS applications
 *  Copyright (C) 2004-2008 ReactOS Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS GUI first stage setup application
 * FILE:        base/setup/reactos/drivepage.c
 * PROGRAMMERS: Matthias Kupfer
 *              Dmitry Chapyshev (dmitry@reactos.org)
 */

#include "reactos.h"
#include <shlwapi.h>

#include <math.h> // For pow()

// #include <ntdddisk.h>
#include <ntddstor.h>
#include <ntddscsi.h>

#include "resource.h"

#define NDEBUG
#include <debug.h>

/* GLOBALS ******************************************************************/

#define IDS_LIST_COLUMN_FIRST IDS_PARTITION_NAME
#define IDS_LIST_COLUMN_LAST  IDS_PARTITION_STATUS

#define MAX_LIST_COLUMNS (IDS_LIST_COLUMN_LAST - IDS_LIST_COLUMN_FIRST + 1)
static const UINT column_ids[MAX_LIST_COLUMNS] = {IDS_LIST_COLUMN_FIRST, IDS_LIST_COLUMN_FIRST + 1, IDS_LIST_COLUMN_FIRST + 2, IDS_LIST_COLUMN_FIRST + 3};
static const INT  column_widths[MAX_LIST_COLUMNS] = {200, 90, 60, 60};
static const INT  column_alignment[MAX_LIST_COLUMNS] = {LVCFMT_LEFT, LVCFMT_LEFT, LVCFMT_RIGHT, LVCFMT_RIGHT};

/* FUNCTIONS ****************************************************************/

static INT_PTR
CALLBACK
MoreOptDlgProc(
    _In_ HWND hDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    PSETUPDATA pSetupData;

    /* Retrieve pointer to the global setup data */
    pSetupData = (PSETUPDATA)GetWindowLongPtrW(hDlg, GWLP_USERDATA);

    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            /***/ LONG MachineType; /***/ // FIXME: Must be moved in USetupData
            BOOL bIsBIOS;
            UINT uID;
            INT nSel;
            WCHAR szText[50];

            /* Save pointer to the global setup data */
            pSetupData = (PSETUPDATA)lParam;
            SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)pSetupData);

            SetDlgItemTextW(hDlg, IDC_PATH,
                            pSetupData->USetupData.InstallationDirectory);


            // FIXME: The following is a temporary HACK until we get
            // a uniformized "MachineType" inside USetupData.
            // This check should actually be done based on the platform type.
            // For the time being we just do it based on the selected disk type.
            {
            PPARTINFO GetSelectedPartition(HWND, HTLITEM*);
            PPARTINFO PartInfo;
            PartInfo = GetSelectedPartition(GetDlgItem(GetParent(hDlg), IDC_PARTITION), NULL);
            if (!PartInfo)
                MachineType = PARTITION_STYLE_MBR;
            else
                MachineType = PartInfo->PartEntry->DiskEntry->DiskStyle;
            }

            /* Initialize the list of possible bootloader locations */
            bIsBIOS = (MachineType == PARTITION_STYLE_MBR);
            for (uID = IDS_BOOTLOADER_NOINST; uID <= IDS_BOOTLOADER_VBRONLY; ++uID)
            {
                if ( ( bIsBIOS && (uID == IDS_BOOTLOADER_SYSTEM)) ||
                     (!bIsBIOS && (uID == IDS_BOOTLOADER_MBRVBR || uID == IDS_BOOTLOADER_VBRONLY)) )
                {
                    continue; // Skip this choice
                }

                LoadStringW(pSetupData->hInstance, uID, szText, ARRAYSIZE(szText));
                nSel = SendDlgItemMessageW(hDlg, IDC_INSTFREELDR, CB_ADDSTRING, 0, (LPARAM)szText);
                if (nSel != CB_ERR && nSel != CB_ERRSPACE)
                {
                    UINT uBldrLoc = uID - IDS_BOOTLOADER_NOINST - (bIsBIOS && (uID >= IDS_BOOTLOADER_SYSTEM) ? 1 : 0);
                    SendDlgItemMessageW(hDlg, IDC_INSTFREELDR, CB_SETITEMDATA, nSel, uBldrLoc);
                }
            }
            /* Select the default location entry */
            nSel = min(max(pSetupData->USetupData.BootLoaderLocation, 0), 3);
            SendDlgItemMessageW(hDlg, IDC_INSTFREELDR, CB_SETCURSEL, nSel, 0);

            break;
        }

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
                case IDOK:
                {
                    INT nSel;
                    UINT uBldrLoc;

                    /* Retrieve the installation path */
                    GetDlgItemTextW(hDlg, IDC_PATH,
                                    pSetupData->USetupData.InstallationDirectory,
                                    ARRAYSIZE(pSetupData->USetupData.InstallationDirectory));

                    /* Retrieve the bootloader location */
                    nSel = SendDlgItemMessageW(hDlg, IDC_INSTFREELDR, CB_GETCURSEL, 0, 0);
                    if (nSel == CB_ERR)
                        nSel = IDS_BOOTLOADER_SYSTEM - IDS_BOOTLOADER_NOINST; // Default location entry
                    uBldrLoc = SendDlgItemMessageW(hDlg, IDC_INSTFREELDR, CB_GETITEMDATA, nSel, 0);
                    if (uBldrLoc == CB_ERR)
                        uBldrLoc = 2; // Default location
                    uBldrLoc = min(max(uBldrLoc, 0), 3); // Technically, 3 if MBR, 2 if not.
                    pSetupData->USetupData.BootLoaderLocation = uBldrLoc;

                    EndDialog(hDlg, IDOK);
                    return TRUE;
                }

                case IDCANCEL:
                    EndDialog(hDlg, IDCANCEL);
                    return TRUE;
            }
            break;
    }

    return FALSE;
}


#define PARTITION_SIZE_INPUT_FIELD_LENGTH 9
/* Restriction for MaxSize */
#define PARTITION_MAXSIZE (pow(10, (PARTITION_SIZE_INPUT_FIELD_LENGTH - 1)) - 1)

typedef struct _PARTCREATE_CTX
{
    PARTINFO PartInfo; // A copy of the info stored in the TreeList
    ULONG MaxSizeMB;
    ULONG PartSizeMB;
    BOOLEAN MBRExtPart;
} PARTCREATE_CTX, *PPARTCREATE_CTX;

static INT_PTR
CALLBACK
PartitionDlgProc(
    _In_ HWND hDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    PPARTCREATE_CTX PartCreateCtx;

    /* Retrieve dialog context pointer */
    PartCreateCtx = (PPARTCREATE_CTX)GetWindowLongPtrW(hDlg, GWLP_USERDATA);

    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            PPARTENTRY PartEntry;
            PDISKENTRY DiskEntry;
            ULONG Index = 0;
            PCWSTR FileSystemName;
            INT nSel;
            PCWSTR DefaultFs;
            ULONG MaxSizeMB;

            /* Save dialog context pointer */
            PartCreateCtx = (PPARTCREATE_CTX)lParam;
            SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)PartCreateCtx);

            /* Retrieve the selected partition */
            PartEntry = PartCreateCtx->PartInfo.PartEntry;
            DiskEntry = PartEntry->DiskEntry;

            /* Set the maximum size the partition can have */
            MaxSizeMB = GetPartEntrySizeInBytes(PartEntry) / MB;  /* in MBytes (rounded) */
            MaxSizeMB = min(MaxSizeMB, PARTITION_MAXSIZE);
            PartCreateCtx->MaxSizeMB = MaxSizeMB;

            SendDlgItemMessageW(hDlg, IDC_UPDOWN_PARTSIZE, UDM_SETRANGE32, (WPARAM)1, (LPARAM)MaxSizeMB);
            SendDlgItemMessageW(hDlg, IDC_UPDOWN_PARTSIZE, UDM_SETPOS32, 0, (LPARAM)MaxSizeMB);
            // SetDlgItemInt(hDlg, IDC_EDIT_PARTSIZE, MaxSizeMB, FALSE);

            /* Default to regular partition (non-extended on MBR disks) */
            CheckDlgButton(hDlg, IDC_CHECK_MBREXTPART, BST_UNCHECKED);

            /* Also, disable and hide IDC_CHECK_MBREXTPART
             * if space is logical or the disk is not MBR */
            if ((DiskEntry->DiskStyle == PARTITION_STYLE_MBR) &&
                !PartEntry->LogicalPartition)
            {
                ShowWindow(GetDlgItem(hDlg, IDC_CHECK_MBREXTPART), SW_SHOW);
                EnableDlgItem(hDlg, IDC_CHECK_MBREXTPART, TRUE);
            }
            else
            {
                ShowWindow(GetDlgItem(hDlg, IDC_CHECK_MBREXTPART), SW_HIDE);
                EnableDlgItem(hDlg, IDC_CHECK_MBREXTPART, FALSE);
            }


            /* List the well-known filesystems */
            while (GetRegisteredFileSystems(Index++, &FileSystemName))
            {
                // ComboBox_InsertString()
                SendDlgItemMessageW(hDlg, IDC_FSTYPE, CB_INSERTSTRING, -1, (LPARAM)FileSystemName);
            }

#if 0
            // FIXME: Use "DefaultFs"?
            nSel = SendDlgItemMessageW(hDlg, IDC_FSTYPE, CB_FINDSTRINGEXACT, 0, (LPARAM)PartEntry->Volume.FileSystem);
#else
            /* By default select the "FAT" file system */
            DefaultFs = L"FAT";
            nSel = SendDlgItemMessageW(hDlg, IDC_FSTYPE, CB_FINDSTRINGEXACT, 0, (LPARAM)DefaultFs);
#endif
            if (nSel == CB_ERR)
                nSel = 0;
            SendDlgItemMessageW(hDlg, IDC_FSTYPE, CB_SETCURSEL, (WPARAM)nSel, 0);

            /* If the partition is originally formatted,
             * add the 'Keep existing filesystem' entry. */
            if (!(PartEntry->New || PartEntry->Volume.FormatState == Unformatted))
            {
                // ComboBox_InsertString()
                SendDlgItemMessageW(hDlg, IDC_FSTYPE, CB_INSERTSTRING, -1, (LPARAM)L"Existing filesystem");
            }

            /* Check the quick-format option by default as it speeds up formatting */
            CheckDlgButton(hDlg, IDC_CHECK_QUICKFMT, BST_CHECKED);

            break;
        }

        case WM_NOTIFY:
        {
#if 0
            LPPSHNOTIFY lppsn = (LPPSHNOTIFY)lParam;

            if ((lppsn->hdr.code == UDN_DELTAPOS) &&
                (lppsn->hdr.idFrom == IDC_UPDOWN_PARTSIZE))
            {
                LPNMUPDOWN lpnmud = (LPNMUPDOWN)lParam;
                // DWORD wwidth;

                /****/lpnmud = lpnmud;/****/
                // wwidth = lpnmud->iPos + lpnmud->iDelta;

                // /* Be sure that the (new) screen buffer sizes are in the correct range */
                // wwidth = min(max(wwidth , 1), 0xFFFF);

                // ConInfo->ScreenBufferSize.X = (SHORT)swidth;

                // PropSheet_Changed(GetParent(hDlg), hDlg);
            }
#endif
            break;
        }

        case WM_COMMAND:
        {
            if (HIWORD(wParam) != BN_CLICKED)
                break;

            switch (LOWORD(wParam))
            {
            case IDC_CHECK_MBREXTPART:
            {
                /* Check for MBR-extended (container) partition */
                // BST_UNCHECKED or BST_INDETERMINATE => FALSE
                if (IsDlgButtonChecked(hDlg, IDC_CHECK_MBREXTPART) == BST_CHECKED)
                {
                    /* It is, disable formatting options */
                    EnableDlgItem(hDlg, IDC_FS_STATIC, FALSE);
                    EnableDlgItem(hDlg, IDC_FSTYPE, FALSE);
                    EnableDlgItem(hDlg, IDC_CHECK_QUICKFMT, FALSE);
                }
                else
                {
                    /* It is not, re-enable formatting options */
                    EnableDlgItem(hDlg, IDC_FS_STATIC, TRUE);
                    EnableDlgItem(hDlg, IDC_FSTYPE, TRUE);
                    EnableDlgItem(hDlg, IDC_CHECK_QUICKFMT, TRUE);
                }
                break;
            }

            case IDOK:
            {
                PPARTINFO PartInfo = &PartCreateCtx->PartInfo;
                // PPARTENTRY PartEntry = PartInfo->PartEntry;
                INT nSel;

                /* Collect all the information and return it
                 * to the caller for creating the partition */

                // PartCreateCtx->PartSizeMB = GetDlgItemInt(hDlg, IDC_EDIT_PARTSIZE, NULL, FALSE);
                PartCreateCtx->PartSizeMB = (ULONG)SendDlgItemMessageW(hDlg, IDC_UPDOWN_PARTSIZE, UDM_SETPOS32, 0, (LPARAM)NULL);
                PartCreateCtx->PartSizeMB = min(max(PartCreateCtx->PartSizeMB, 1), PartCreateCtx->MaxSizeMB);

                PartCreateCtx->MBRExtPart = (IsDlgButtonChecked(hDlg, IDC_CHECK_MBREXTPART) == BST_CHECKED);


                /* Retrieve the formatting options */

// https://git.reactos.org/?p=reactos.git;a=blob;f=dll/win32/shell32/dialogs/drive.cpp;hb=455f33077599729c27f1f1347ad2f6329d50d1f3#l236
// https://git.reactos.org/?p=reactos.git;a=blob;f=dll/cpl/console/font.c;hb=455f33077599729c27f1f1347ad2f6329d50d1f3#l223

                /* Retrieve the selected filesystem */
                nSel = SendDlgItemMessageW(hDlg, IDC_FSTYPE, CB_GETCURSEL, 0, 0);
                // if (nSel == CB_ERR)
                //     nSel = ???; // Default entry
                // data = SendDlgItemMessageW(hDlg, IDC_FSTYPE, CB_GETITEMDATA, nSel, 0);
                // if (data == CB_ERR)
                //     data = ???; // Default entry

                if (SendDlgItemMessageW(hDlg, IDC_FSTYPE,
                                        CB_GETLBTEXT,
                                        nSel,
                                        (LPARAM)PartInfo->FileSystemName) == CB_ERR)
                {
                    // return;
                    // FIXME
                }

                /* Cached input information that will be set to the
                 * FORMAT_PARTITION_INFO structure given to the
                 * 'FSVOLNOTIFY_STARTFORMAT' step */
                // TODO: Think about which values could be defaulted...
                // PartInfo->FileSystemName = SelectedFileSystem->FileSystem;
                PartInfo->MediaFlag = FMIFS_HARDDISK;
                PartInfo->Label = NULL;
                PartInfo->QuickFormat =
                    (IsDlgButtonChecked(hDlg, IDC_CHECK_QUICKFMT) == BST_CHECKED);
                PartInfo->ClusterSize = 0;

                EndDialog(hDlg, IDOK);
                return TRUE;
            }

            case IDCANCEL:
            {
                EndDialog(hDlg, IDCANCEL);
                return TRUE;
            }
            }
        }
    }
    return FALSE;
}


BOOL
CreateTreeListColumns(
    IN HINSTANCE hInstance,
    IN HWND hWndTreeList,
    IN const UINT* pIDs,
    IN const INT* pColsWidth,
    IN const INT* pColsAlign,
    IN UINT nNumOfColumns)
{
    UINT i;
    TLCOLUMN tlC;
    WCHAR szText[50];

    /* Create the columns */
    tlC.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
    tlC.pszText = szText;

    /* Load the column labels from the resource file */
    for (i = 0; i < nNumOfColumns; i++)
    {
        tlC.iSubItem = i;
        tlC.cx = pColsWidth[i];
        tlC.fmt = pColsAlign[i];

        LoadStringW(hInstance, pIDs[i], szText, ARRAYSIZE(szText));

        if (TreeList_InsertColumn(hWndTreeList, i, &tlC) == -1)
            return FALSE;
    }

    return TRUE;
}

// unused
VOID
DisplayStuffUsingWin32Setup(HWND hwndDlg)
{
    HDEVINFO h;
    HWND hList;
    SP_DEVINFO_DATA DevInfoData;
    DWORD i;

    h = SetupDiGetClassDevs(&GUID_DEVCLASS_DISKDRIVE, NULL, NULL, DIGCF_PRESENT);
    if (h == INVALID_HANDLE_VALUE)
        return;

    hList = GetDlgItem(hwndDlg, IDC_PARTITION);
    DevInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    for (i=0; SetupDiEnumDeviceInfo(h, i, &DevInfoData); i++)
    {
        DWORD DataT;
        LPTSTR buffer = NULL;
        DWORD buffersize = 0;

        while (!SetupDiGetDeviceRegistryProperty(h,
                                                 &DevInfoData,
                                                 SPDRP_DEVICEDESC,
                                                 &DataT,
                                                 (PBYTE)buffer,
                                                 buffersize,
                                                 &buffersize))
        {
            if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
            {
                if (buffer) LocalFree(buffer);
                buffer = LocalAlloc(LPTR, buffersize * 2);
            }
            else
            {
                return;
            }
        }
        if (buffer)
        {
            SendMessageW(hList, LB_ADDSTRING, (WPARAM)0, (LPARAM)buffer);
            LocalFree(buffer);
        }
    }
    SetupDiDestroyDeviceInfoList(h);
}


HTLITEM
TreeListAddItem(
    _In_ HWND hTreeList,
    _In_opt_ HTLITEM hParent,
    _In_opt_ HTLITEM hInsertAfter,
    _In_ LPCWSTR lpText,
    _In_ INT iImage,
    _In_ INT iSelectedImage,
    _In_ LPARAM lParam)
{
    TLINSERTSTRUCTW Insert;

    ZeroMemory(&Insert, sizeof(Insert));

    Insert.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
    Insert.hParent = hParent;
    Insert.hInsertAfter = (hInsertAfter ? hInsertAfter : TVI_LAST);
    Insert.item.pszText = (LPWSTR)lpText;
    Insert.item.iImage = iImage;
    Insert.item.iSelectedImage = iSelectedImage;
    Insert.item.lParam = lParam;

    // Insert.item.mask |= TVIF_STATE;
    // Insert.item.stateMask = TVIS_OVERLAYMASK;
    // Insert.item.state = INDEXTOOVERLAYMASK(1);

    return TreeList_InsertItem(hTreeList, &Insert);
}

LPARAM
TreeListGetItemData(
    _In_ HWND hTreeList,
    _In_ HTLITEM hItem)
{
    TLITEMW tlItem;

    tlItem.mask = TVIF_PARAM;
    tlItem.hItem = hItem;

    TreeList_GetItem(hTreeList, &tlItem);

    return tlItem.lParam;
}

PPARTINFO
GetItemPartition(
    _In_ HWND hTreeList,
    _In_ HTLITEM hItem)
{
    HTLITEM hParentItem;
    PPARTINFO PartInfo;

    hParentItem = TreeList_GetParent(hTreeList, hItem);
    /* May or may not be a PPARTINFO: this is a PPARTINFO only when hParentItem != NULL */
    PartInfo = (PPARTINFO)TreeListGetItemData(hTreeList, hItem);
    if (!hParentItem || !PartInfo)
        return NULL;

    return PartInfo;
}

PPARTINFO
GetSelectedPartition(
    _In_ HWND hTreeList,
    _Out_opt_ HTLITEM* phItem)
{
    HTLITEM hItem;
    PPARTINFO PartInfo;

    hItem = TreeList_GetSelection(hTreeList);
    if (!hItem)
        return NULL;

    PartInfo = GetItemPartition(hTreeList, hItem);
    if (PartInfo && phItem)
        *phItem = hItem;

    return PartInfo;
}

PPARTINFO
FindPartInfoInTreeByPartEntry(
    _In_ HWND hTreeList,
    _In_ PPARTENTRY PartEntry)
{
    HTLITEM hItem;

    /* Enumerate every cached data in the TreeList, and for each, check
     * whether its corresponding PPARTENTRY is the one we are looking for */
    // for (hItem = TVI_ROOT; hItem; hItem = TreeList_GetNextItem(...)) { }
    hItem = TVI_ROOT;
    while ((hItem = TreeList_GetNextItem(hTreeList, hItem, TVGN_NEXTITEM)))
    {
        PPARTINFO PartInfo = GetItemPartition(hTreeList, hItem);
        if (!PartInfo)
            continue;

        if (PartInfo->PartEntry == PartEntry)
        {
            /* Found it, return the PartInfo */
            return PartInfo;
        }
    }

    /* Nothing was found */
    return NULL;
}


VOID
GetPartitionTypeString(
    IN PPARTENTRY PartEntry,
    OUT PSTR strBuffer,
    IN ULONG cchBuffer)
{
    if (PartEntry->PartitionType == PARTITION_ENTRY_UNUSED)
    {
        StringCchCopyA(strBuffer, cchBuffer,
                       "Unused" /* MUIGetString(STRING_FORMATUNUSED) */);
    }
    // else if (PartEntry == PartEntry->DiskEntry->ExtendedPartition)
    else if (IsContainerPartition(PartEntry->PartitionType))
    {
        StringCchCopyA(strBuffer, cchBuffer,
                       "Extended Partition" /* MUIGetString(STRING_EXTENDED_PARTITION) */);
    }
    else
    {
        UINT i;

        /* Do the table lookup */
        if (PartEntry->DiskEntry->DiskStyle == PARTITION_STYLE_MBR)
        {
            for (i = 0; i < ARRAYSIZE(MbrPartitionTypes); ++i)
            {
                if (PartEntry->PartitionType == MbrPartitionTypes[i].Type)
                {
                    StringCchCopyA(strBuffer, cchBuffer,
                                   MbrPartitionTypes[i].Description);
                    return;
                }
            }
        }
#if 0 // TODO: GPT support!
        else if (PartEntry->DiskEntry->DiskStyle == PARTITION_STYLE_GPT)
        {
            for (i = 0; i < ARRAYSIZE(GptPartitionTypes); ++i)
            {
                if (IsEqualPartitionType(PartEntry->PartitionType,
                                         GptPartitionTypes[i].Guid))
                {
                    StringCchCopyA(strBuffer, cchBuffer,
                                   GptPartitionTypes[i].Description);
                    return;
                }
            }
        }
#endif

        /* We are here because the partition type is unknown */
        if (cchBuffer > 0) *strBuffer = '\0';
    }

    if ((cchBuffer > 0) && (*strBuffer == '\0'))
    {
        StringCchPrintfA(strBuffer, cchBuffer,
                         // MUIGetString(STRING_PARTTYPE),
                         "Type 0x%02x",
                         PartEntry->PartitionType);
    }
}

static
HTLITEM
PrintPartitionData(
    _In_ HWND hWndList,
    _In_ HTLITEM htiParent,
    _In_opt_ HTLITEM hInsertAfter,
    _In_ PPARTENTRY PartEntry)
{
    PPARTINFO PartInfo;
    PDISKENTRY DiskEntry = PartEntry->DiskEntry;
    LARGE_INTEGER PartSize;
    HTLITEM htiPart;
    CHAR PartTypeString[32];
    PCHAR PartType = PartTypeString;
    WCHAR LineBuffer[128];

    /* Volume name */
    if (PartEntry->IsPartitioned == FALSE)
    {
        /* Unpartitioned space: Just display the description */
        StringCchPrintfW(LineBuffer, ARRAYSIZE(LineBuffer),
                         // MUIGetString(STRING_UNPSPACE)
                         L"Unpartitioned space");
    }
    else
//
// NOTE: This could be done with the next case.
//
    if ((DiskEntry->DiskStyle == PARTITION_STYLE_MBR) &&
        IsContainerPartition(PartEntry->PartitionType))
    {
        /* Extended partition container: Just display the partition's type */
        StringCchPrintfW(LineBuffer, ARRAYSIZE(LineBuffer),
                         // MUIGetString(STRING_EXTENDED_PARTITION)
                         L"Extended Partition");
    }
    else
    {
        /* Drive letter and partition number */
        StringCchPrintfW(LineBuffer, ARRAYSIZE(LineBuffer),
                         // MUIGetString(STRING_HDDINFOUNK5),
                         L"%s (%c%c)",
                         *PartEntry->Volume.VolumeLabel ? PartEntry->Volume.VolumeLabel : L"Partition",
                         (PartEntry->Volume.DriveLetter == 0) ? L'-' : PartEntry->Volume.DriveLetter,
                         (PartEntry->Volume.DriveLetter == 0) ? L'-' : L':');
    }

    /* Allocate and initialize a partition-info structure */
    PartInfo = LocalAlloc(LPTR, sizeof(*PartInfo));
    if (!PartInfo)
    {
        DPRINT1("Failed to allocate partition-info structure\n");
        // return NULL;
        // We'll store a NULL pointer?!
    }

    PartInfo->PartEntry = PartEntry;
    // TODO: the default volume info?

    htiPart = TreeListAddItem(hWndList, htiParent, hInsertAfter,
                              LineBuffer, 1, 1,
                              (LPARAM)PartInfo);

    *LineBuffer = 0;
    if (PartEntry->IsPartitioned)
    {
        PartTypeString[0] = '\0';

        /*
         * If the volume's file system is recognized, display the volume label
         * (if any) and the file system name. Otherwise, display the partition
         * type if it's not a new partition.
         */
        if (*PartEntry->Volume.FileSystem &&
            _wcsicmp(PartEntry->Volume.FileSystem, L"RAW") != 0)
        {
            // TODO: Group this part together with the similar one
            // from below once the strings are in the same encoding...
            if (PartEntry->New)
            {
                StringCchPrintfA(PartTypeString,
                                 ARRAYSIZE(PartTypeString),
                                 "New (%S)",
                                 PartEntry->Volume.FileSystem);
            }
            else
            {
                StringCchPrintfA(PartTypeString,
                                 ARRAYSIZE(PartTypeString),
                                 "%S",
                                 PartEntry->Volume.FileSystem);
            }
            PartType = PartTypeString;
        }
        else
        /* Determine partition type */
        if (PartEntry->New)
        {
            /* Use this description if the partition is new and not planned for formatting */
            PartType = "New (Unformatted)"; // MUIGetString(STRING_UNFORMATTED);
        }
        else
        {
            /* If the partition is not new but its file system is not recognized
             * (or is not formatted), use the partition type description. */
            GetPartitionTypeString(PartEntry,
                                   PartTypeString,
                                   ARRAYSIZE(PartTypeString));
            PartType = PartTypeString;
        }
#if 0
        if (!PartType || !*PartType)
        {
            PartType = MUIGetString(STRING_FORMATUNKNOWN);
        }
#endif

        StringCchPrintfW(LineBuffer, ARRAYSIZE(LineBuffer),
                         L"%S",
                         PartType);
    }
    TreeList_SetItemText(hWndList, htiPart, 1, LineBuffer);

    /* Format the disk size in KBs, MBs, etc... */
    PartSize.QuadPart = GetPartEntrySizeInBytes(PartEntry);
    if (StrFormatByteSizeW(PartSize.QuadPart, LineBuffer, ARRAYSIZE(LineBuffer)) == NULL)
    {
        /* We failed for whatever reason, do the hardcoded way */
        PWCHAR Unit;

#if 0
        if (PartSize.QuadPart >= 10 * GB) /* 10 GB */
        {
            PartSize.QuadPart = RoundingDivide(PartSize.QuadPart, GB);
            // Unit = MUIGetString(STRING_GB);
            Unit = L"GB";
        }
        else
#endif
        if (PartSize.QuadPart >= 10 * MB) /* 10 MB */
        {
            PartSize.QuadPart = RoundingDivide(PartSize.QuadPart, MB);
            // Unit = MUIGetString(STRING_MB);
            Unit = L"MB";
        }
        else
        {
            PartSize.QuadPart = RoundingDivide(PartSize.QuadPart, KB);
            // Unit = MUIGetString(STRING_KB);
            Unit = L"KB";
        }

        StringCchPrintfW(LineBuffer, ARRAYSIZE(LineBuffer),
                         L"%6lu %s",
                         PartSize.u.LowPart,
                         Unit);
    }
    TreeList_SetItemText(hWndList, htiPart, 2, LineBuffer);

    /* Volume status */
    *LineBuffer = 0;
    if (PartEntry->IsPartitioned)
    {
        StringCchPrintfW(LineBuffer, ARRAYSIZE(LineBuffer),
                         // MUIGetString(STRING_HDDINFOUNK5),
                         PartEntry->BootIndicator ? L"Active" : L"");
    }
    TreeList_SetItemText(hWndList, htiPart, 3, LineBuffer);


    /* If this is the single extended partition of an MBR disk,
     * recursively display its logical partitions */
    if (PartEntry == DiskEntry->ExtendedPartition)
    {
        PLIST_ENTRY LogicalEntry;
        PPARTENTRY LogicalPartEntry;

        for (LogicalEntry = DiskEntry->LogicalPartListHead.Flink;
             LogicalEntry != &DiskEntry->LogicalPartListHead;
             LogicalEntry = LogicalEntry->Flink)
        {
            LogicalPartEntry = CONTAINING_RECORD(LogicalEntry, PARTENTRY, ListEntry);
            PrintPartitionData(hWndList, htiPart, NULL, LogicalPartEntry);
        }

        /* Expand the extended partition node */
        TreeList_Expand(hWndList, htiPart, TVE_EXPAND);
    }


    return htiPart;
}

/* Called on response to the TVN_DELETEITEM notification sent by the TreeList */
static
VOID
DeleteTreeItem(
    _In_ HWND hWndList,
    _In_ TLITEMW* ptlItem)
{
    PPARTINFO PartInfo;

#if 1 // TEMP DEBUG ONLY?
    HTLITEM hParentItem = TreeList_GetParent(hWndList, ptlItem->hItem);
    /* May or may not be a PPARTINFO: this is a PPARTINFO only when hParentItem != NULL */
    // PartInfo = (PPARTINFO)TreeListGetItemData(hWndList, ptlItem->hItem);
    PartInfo = (PPARTINFO)ptlItem->lParam;
    if (!hParentItem || !PartInfo)
        return;
#else
    PartInfo = GetItemPartition(hWndList, ptlItem->hItem);
    if (!PartInfo)
        return;
#endif

    LocalFree(PartInfo);
}

static
VOID
PrintDiskData(
    _In_ HWND hWndList,
    _In_opt_ HTLITEM hInsertAfter,
    _In_ PDISKENTRY DiskEntry)
{
    BOOL Success;
    HANDLE hDevice;
    PCHAR DiskName = NULL;
    ULONG Length = 0;
    PPARTENTRY PrimaryPartEntry;
    PLIST_ENTRY PrimaryEntry;
    ULARGE_INTEGER DiskSize;
    HTLITEM htiDisk;
    WCHAR LineBuffer[128];
    UCHAR outBuf[512];

    StringCchPrintfW(LineBuffer, ARRAYSIZE(LineBuffer),
                     // L"\\Device\\Harddisk%lu\\Partition%lu",
                     L"\\\\.\\PhysicalDrive%lu",
                     DiskEntry->DiskNumber);

    hDevice = CreateFileW(
                LineBuffer,                         // device interface name
                GENERIC_READ /*| GENERIC_WRITE*/,   // dwDesiredAccess
                FILE_SHARE_READ | FILE_SHARE_WRITE, // dwShareMode
                NULL,                               // lpSecurityAttributes
                OPEN_EXISTING,                      // dwCreationDistribution
                0,                                  // dwFlagsAndAttributes
                NULL                                // hTemplateFile
                );
    if (hDevice != INVALID_HANDLE_VALUE)
    {
        STORAGE_PROPERTY_QUERY Query;

        Query.PropertyId = StorageDeviceProperty;
        Query.QueryType  = PropertyStandardQuery;

        Success = DeviceIoControl(hDevice,
                                  IOCTL_STORAGE_QUERY_PROPERTY,
                                  &Query,
                                  sizeof(Query),
                                  &outBuf,
                                  sizeof(outBuf),
                                  &Length,
                                  NULL);
        if (Success)
        {
            PSTORAGE_DEVICE_DESCRIPTOR devDesc = (PSTORAGE_DEVICE_DESCRIPTOR)outBuf;
            if (devDesc->ProductIdOffset)
            {
                DiskName = (PCHAR)&outBuf[devDesc->ProductIdOffset];
                Length -= devDesc->ProductIdOffset;
                DiskName[min(Length, strlen(DiskName))] = 0;
                // ( i = devDesc->ProductIdOffset; p[i] != 0 && i < Length; i++ )
            }
        }

        CloseHandle(hDevice);
    }

    if (DiskName && *DiskName)
    {
        if (DiskEntry->DriverName.Length > 0)
        {
            StringCchPrintfW(LineBuffer, ARRAYSIZE(LineBuffer),
                             // MUIGetString(STRING_HDDINFO_1),
                             L"Harddisk %lu (%S) (Port=%hu, Bus=%hu, Id=%hu) on %wZ",
                             DiskEntry->DiskNumber,
                             DiskName,
                             DiskEntry->Port,
                             DiskEntry->Bus,
                             DiskEntry->Id,
                             &DiskEntry->DriverName);
        }
        else
        {
            StringCchPrintfW(LineBuffer, ARRAYSIZE(LineBuffer),
                             // MUIGetString(STRING_HDDINFO_2),
                             L"Harddisk %lu (%S) (Port=%hu, Bus=%hu, Id=%hu)",
                             DiskEntry->DiskNumber,
                             DiskName,
                             DiskEntry->Port,
                             DiskEntry->Bus,
                             DiskEntry->Id);
        }
    }
    else
    {
        if (DiskEntry->DriverName.Length > 0)
        {
            StringCchPrintfW(LineBuffer, ARRAYSIZE(LineBuffer),
                             // MUIGetString(STRING_HDDINFO_1),
                             L"Harddisk %lu (Port=%hu, Bus=%hu, Id=%hu) on %wZ",
                             DiskEntry->DiskNumber,
                             DiskEntry->Port,
                             DiskEntry->Bus,
                             DiskEntry->Id,
                             &DiskEntry->DriverName);
        }
        else
        {
            StringCchPrintfW(LineBuffer, ARRAYSIZE(LineBuffer),
                             // MUIGetString(STRING_HDDINFO_2),
                             L"Harddisk %lu (Port=%hu, Bus=%hu, Id=%hu)",
                             DiskEntry->DiskNumber,
                             DiskEntry->Port,
                             DiskEntry->Bus,
                             DiskEntry->Id);
        }
    }

    htiDisk = TreeListAddItem(hWndList, NULL, hInsertAfter,
                              LineBuffer, 0, 0,
                              (LPARAM)DiskEntry);

    /* Disk type: MBR, GPT or RAW (Uninitialized) */
    TreeList_SetItemText(hWndList, htiDisk, 1,
                         DiskEntry->DiskStyle == PARTITION_STYLE_MBR ? L"MBR" :
                         DiskEntry->DiskStyle == PARTITION_STYLE_GPT ? L"GPT" :
                                                                       L"RAW");

    /* Format the disk size in KBs, MBs, etc... */
    DiskSize.QuadPart = GetDiskSizeInBytes(DiskEntry);
    if (StrFormatByteSizeW(DiskSize.QuadPart, LineBuffer, ARRAYSIZE(LineBuffer)) == NULL)
    {
        /* We failed for whatever reason, do the hardcoded way */
        PWCHAR Unit;

        if (DiskSize.QuadPart >= 10 * GB) /* 10 GB */
        {
            DiskSize.QuadPart = RoundingDivide(DiskSize.QuadPart, GB);
            // Unit = MUIGetString(STRING_GB);
            Unit = L"GB";
        }
        else
        {
            DiskSize.QuadPart = RoundingDivide(DiskSize.QuadPart, MB);
            if (DiskSize.QuadPart == 0)
                DiskSize.QuadPart = 1;
            // Unit = MUIGetString(STRING_MB);
            Unit = L"MB";
        }

        StringCchPrintfW(LineBuffer, ARRAYSIZE(LineBuffer),
                         L"%6lu %s",
                         DiskSize.u.LowPart,
                         Unit);
    }
    TreeList_SetItemText(hWndList, htiDisk, 2, LineBuffer);

    /* Print partition lines */
    for (PrimaryEntry = DiskEntry->PrimaryPartListHead.Flink;
         PrimaryEntry != &DiskEntry->PrimaryPartListHead;
         PrimaryEntry = PrimaryEntry->Flink)
    {
        PrimaryPartEntry = CONTAINING_RECORD(PrimaryEntry, PARTENTRY, ListEntry);

        /* If this is an extended partition, recursively print the logical partitions */
        PrintPartitionData(hWndList, htiDisk, NULL, PrimaryPartEntry);
    }

    /* Expand the disk node */
    TreeList_Expand(hWndList, htiDisk, TVE_EXPAND);
}

static VOID
InitPartitionList(
    _In_ HINSTANCE hInstance,
    _In_ HWND hWndList)
{
    HIMAGELIST hSmall;

    TreeList_SetExtendedStyleEx(hWndList, TVS_EX_FULLROWMARK, TVS_EX_FULLROWMARK);
    // TreeList_SetExtendedStyleEx(hWndList, TVS_EX_FULLROWITEMS, TVS_EX_FULLROWITEMS);

    CreateTreeListColumns(hInstance,
                          hWndList,
                          column_ids,
                          column_widths,
                          column_alignment,
                          MAX_LIST_COLUMNS);

    /* Create the ImageList */
    hSmall = ImageList_Create(GetSystemMetrics(SM_CXSMICON),
                              GetSystemMetrics(SM_CYSMICON),
                              ILC_COLOR32 | ILC_MASK, // ILC_COLOR24
                              1, 1);

    /* Add event type icons to the ImageList */
    ImageList_AddIcon(hSmall, LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_DISKDRIVE)));
    ImageList_AddIcon(hSmall, LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_PARTITION)));

    /* Assign the ImageList to the List View */
    TreeList_SetImageList(hWndList, hSmall, TVSIL_NORMAL);
}

static VOID
DrawPartitionList(
    _In_ HWND hWndList,
    _In_ PPARTLIST List)
{
    PLIST_ENTRY Entry;
    PDISKENTRY DiskEntry;

    /* Clear the list first */
    TreeList_DeleteAllItems(hWndList);

    /* Insert all the detected disks and partitions */
    for (Entry = List->DiskListHead.Flink;
         Entry != &List->DiskListHead;
         Entry = Entry->Flink)
    {
        DiskEntry = CONTAINING_RECORD(Entry, DISKENTRY, ListEntry);

        /* Print disk entry */
        PrintDiskData(hWndList, NULL, DiskEntry);
    }

    /* Select the first item */
    // TreeList_SetFocusItem(hWndList, 1, 1);
    TreeList_SelectItem(hWndList, 1);
}

static VOID
CleanupPartitionList(
    _In_ HWND hWndList)
{
    HIMAGELIST hSmall;

    /* Cleanup all the items. Their cached data will be automatically deleted
     * on response to the TVN_DELETEITEM notification sent by the TreeList
     * by DeleteTreeItem() */
    TreeList_DeleteAllItems(hWndList);

    /* And cleanup the imagelist */
    hSmall = TreeList_GetImageList(hWndList, TVSIL_NORMAL);
    TreeList_SetImageList(hWndList, NULL, TVSIL_NORMAL);
    ImageList_Destroy(hSmall);
}

INT_PTR
CALLBACK
DriveDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    PSETUPDATA pSetupData;
    HWND hList;

    /* Retrieve pointer to the global setup data */
    pSetupData = (PSETUPDATA)GetWindowLongPtrW(hwndDlg, GWLP_USERDATA);

    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            /* Save pointer to the global setup data */
            pSetupData = (PSETUPDATA)((LPPROPSHEETPAGE)lParam)->lParam;
            SetWindowLongPtrW(hwndDlg, GWLP_USERDATA, (LONG_PTR)pSetupData);

            /*
             * Keep the "Next" button disabled. It will be enabled only
             * when the user selects a valid partition.
             */
            PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK);

            /* Initially disable and hide all partitioning buttons */
            ShowWindow(GetDlgItem(hwndDlg, IDC_INITDISK), SW_HIDE);
            ShowWindow(GetDlgItem(hwndDlg, IDC_PARTCREATE), SW_HIDE);
            ShowWindow(GetDlgItem(hwndDlg, IDC_PARTDELETE), SW_HIDE);
            EnableDlgItem(hwndDlg, IDC_INITDISK, FALSE);
            EnableDlgItem(hwndDlg, IDC_PARTCREATE, FALSE);
            EnableDlgItem(hwndDlg, IDC_PARTDELETE, FALSE);

            hList = GetDlgItem(hwndDlg, IDC_PARTITION);
            UiContext.hPartList = hList;
            InitPartitionList(pSetupData->hInstance, hList);
            DrawPartitionList(hList, pSetupData->PartitionList);
            // DisplayStuffUsingWin32Setup(hwndDlg);

            // HACK: Wine "kwality" code doesn't still implement
            // PSN_QUERYINITIALFOCUS so we "emulate" its call there...
            {
            PSHNOTIFY pshn = {{hwndDlg, GetWindowLong(hwndDlg, GWL_ID), PSN_QUERYINITIALFOCUS}, (LPARAM)hList};
            SendMessageW(hwndDlg, WM_NOTIFY, (WPARAM)pshn.hdr.idFrom, (LPARAM)&pshn);
            }
            break;
        }

        case WM_DESTROY:
        {
            hList = GetDlgItem(hwndDlg, IDC_PARTITION);
            ASSERT(UiContext.hPartList == hList);
            UiContext.hPartList = NULL;
            CleanupPartitionList(hList);
            return TRUE;
        }

        case WM_COMMAND:
        {
            switch (LOWORD(wParam))
            {
                case IDC_PARTMOREOPTS:
                {
                    DialogBoxParamW(pSetupData->hInstance,
                                    MAKEINTRESOURCEW(IDD_ADVINSTOPTS),
                                    hwndDlg,
                                    MoreOptDlgProc,
                                    (LPARAM)pSetupData);
                    break;
                }

                case IDC_INITDISK:
                {
                    // TODO: Implement disk partitioning initialization
                    break;
                }

                case IDC_PARTCREATE:
                {
                    INT_PTR ret;
                    HTLITEM hItem;
                    PPARTINFO PartInfo;
                    PPARTENTRY PartEntry;
                    PARTCREATE_CTX PartCreateCtx;

                    hList = GetDlgItem(hwndDlg, IDC_PARTITION);

                    PartInfo = GetSelectedPartition(hList, &hItem);
                    if (!PartInfo)
                    {
                        // If the button was clicked, an empty disk
                        // region should have been selected first...
                        ASSERT(FALSE);
                        break;
                    }
                    PartEntry = PartInfo->PartEntry;

                    /* Make a copy of the info stored in the TreeList */
                    PartCreateCtx.PartInfo = *PartInfo;
                    PartCreateCtx.MaxSizeMB = 0;

                    ret = DialogBoxParamW(pSetupData->hInstance,
                                          MAKEINTRESOURCEW(IDD_PARTITION),
                                          hwndDlg,
                                          PartitionDlgProc,
                                          (LPARAM)&PartCreateCtx);

                    /* If we want to create a partition... */
                    if (ret == IDOK)
                    {
                        BOOLEAN Success;
                        HTLITEM hParentItem;
                        HTLITEM hInsertAfter;
                        ULONGLONG PartSize;
                        PPARTENTRY NextPart;

                        /* Convert to bytes */
                        PartSize = PartCreateCtx.PartSizeMB;
                        PartSize *= MB;

                        if (!PartCreateCtx.MBRExtPart)
                        {
                            Success = CreatePartition(pSetupData->PartitionList,
                                                      PartEntry,
                                                      PartSize);
                        }
                        else
                        {
                            Success = CreateExtendedPartition(pSetupData->PartitionList,
                                                              PartEntry,
                                                              PartSize);
                        }

                        if (!Success)
                        {
                            // FIXME: Just show an error? Respawn the creation dialog?
                            MessageBoxW(GetParent(hwndDlg),
                                        L"Failed to create a new partition.",
                                        L"Create partition",
                                        MB_OK | MB_ICONERROR);
                            break;
                        }

                        /* Retrieve the parent item (disk or MBR extended partition) */
                        hParentItem = TreeList_GetParent(hList, hItem);
                        hInsertAfter = TreeList_GetPrevSibling(hList, hItem);
                        if (!hInsertAfter)
                            hInsertAfter = TVI_FIRST;

                        /*
                         * Current entry has been recreated and perhaps split
                         * in two: new partition and remaining unused space.
                         * Thus, recreate the current entry.
                         *
                         * NOTE: Since we create a partition we don't care
                         * about its previous PartInfo, so it can be deleted.
                         */

                        /* Remove the current item */
                        PartInfo = NULL;
                        TreeList_DeleteItem(hList, hItem);

                        /* Recreate the entry */
                        hItem = PrintPartitionData(hList, hParentItem, hInsertAfter, PartEntry);

                        /* Restore the previous PartInfo */
                        PartInfo = GetItemPartition(hList, hItem);
                        ASSERT(PartInfo);
                        *PartInfo = PartCreateCtx.PartInfo;

                        /* Add also the following unused space, if any */
                        NextPart = GetAdjDiskRegion(NULL, PartEntry, ENUM_REGION_NEXT);
                        if (NextPart && !NextPart->IsPartitioned)
                            PrintPartitionData(hList, hParentItem, hItem, NextPart);

                        /* Give the focus on and select the created partition */
                        // TreeList_SetFocusItem(hList, 1, 1);
                        TreeList_SelectItem(hList, hItem);
                    }

                    break;
                }

                case IDC_PARTDELETE:
                {
                    PPARTINFO PartInfo;
                    PPARTENTRY PartEntry;
                    HTLITEM hItem;
                    LPCWSTR pszWarnMsg;

                    hList = GetDlgItem(hwndDlg, IDC_PARTITION);

                    PartInfo = GetSelectedPartition(hList, &hItem);
                    if (!PartInfo)
                    {
                        // If the button was clicked, a partition
                        // should have been selected first...
                        ASSERT(FALSE);
                        break;
                    }
                    PartEntry = PartInfo->PartEntry;

                    // FIXME: Localize strings

                    if (PartEntry == PartEntry->DiskEntry->ExtendedPartition)
                    {
                        /* MBR-extended (container) partition: show different message */
                        pszWarnMsg = L"Are you sure you want to delete the selected extended partition and ALL the logical partitions it contains?";
                    }
                    else
                    {
                        pszWarnMsg = L"Are you sure you want to delete the selected partition?";
                    }

                    /* If the user really wants to delete the partition... */
                    if (MessageBoxW(GetParent(hwndDlg),
                                    pszWarnMsg,
                                    L"Delete partition?",
                                    MB_YESNO | MB_DEFBUTTON2 | MB_ICONWARNING) == IDYES)
                    {
                        PPARTENTRY PrevPart, NextPart;
                        BOOLEAN PrevIsPartitioned, NextIsPartitioned;

                        /*
                         * Determine the nature of the previous and next disk regions,
                         * in order to know what to do on the corresponding tree items
                         * after the selected partition has been deleted.
                         *
                         * NOTE: Don't reference these pointers after DeletePartition(),
                         * since these disk regions might have been coalesced/reallocated.
                         * However we can check whether they were NULL or not.
                         */
                        PrevPart = GetAdjDiskRegion(NULL, PartEntry, ENUM_REGION_PREV);
                        NextPart = GetAdjDiskRegion(NULL, PartEntry, ENUM_REGION_NEXT);

                        PrevIsPartitioned = (PrevPart && PrevPart->IsPartitioned);
                        NextIsPartitioned = (NextPart && NextPart->IsPartitioned);

                        /* ... and make it so! */
                        ASSERT(PartEntry->IsPartitioned); // The current partition must be partitioned.
                        if (DeletePartition(pSetupData->PartitionList,
                                            PartEntry,
                                            &PartEntry))
                        {
                            HTLITEM hParentItem;
                            HTLITEM hInsertAfter;
                            HTLITEM hAdjItem;
                            PPARTINFO AdjPartInfo;

                            /* Retrieve the parent item (disk or MBR extended partition) */
                            hParentItem = TreeList_GetParent(hList, hItem);

                            /* If previous sibling isn't partitioned, remove it */
                            if (PrevPart && !PrevIsPartitioned)
                            {
                                hAdjItem = TreeList_GetPrevSibling(hList, hItem);
                                ASSERT(hAdjItem); // TODO: Investigate
                                if (hAdjItem)
                                {
                                    AdjPartInfo = GetItemPartition(hList, hAdjItem);
                                    ASSERT(AdjPartInfo);
                                    ASSERT(AdjPartInfo->PartEntry == PrevPart);
                                    AdjPartInfo = NULL;
                                    TreeList_DeleteItem(hList, hAdjItem);
                                }
                            }
                            /* If next sibling isn't partitioned, remove it */
                            if (NextPart && !NextIsPartitioned)
                            {
                                hAdjItem = TreeList_GetNextSibling(hList, hItem);
                                ASSERT(hAdjItem); // TODO: Investigate
                                if (hAdjItem)
                                {
                                    AdjPartInfo = GetItemPartition(hList, hAdjItem);
                                    ASSERT(AdjPartInfo);
                                    ASSERT(AdjPartInfo->PartEntry == NextPart);
                                    AdjPartInfo = NULL;
                                    TreeList_DeleteItem(hList, hAdjItem);
                                }
                            }

                            /* We are going to insert the updated entry after
                             * either, the original previous sibling (if it is
                             * partitioned), or the second-previous sibling
                             * (if the first one was already removed, see above) */
                            hInsertAfter = TreeList_GetPrevSibling(hList, hItem);
                            if (!hInsertAfter)
                                hInsertAfter = TVI_FIRST;

                            /* Remove the current item */
                            PartInfo = NULL;
                            TreeList_DeleteItem(hList, hItem);

                            /* Add back the "new" unpartitioned space */
                            hItem = PrintPartitionData(hList, hParentItem, hInsertAfter, PartEntry);

                            /* Give the focus on and select the unpartitioned space */
                            // TreeList_SetFocusItem(hList, 1, 1);
                            TreeList_SelectItem(hList, hItem);
                        }
                    }

                    break;
                }
            }
            break;
        }

        case WM_NOTIFY:
        {
            LPNMHDR lpnm = (LPNMHDR)lParam;

            // On Vista+ we can use TVN_ITEMCHANGED instead, with NMTVITEMCHANGE* pointer
            if (lpnm->idFrom == IDC_PARTITION && lpnm->code == TVN_SELCHANGED)
            {
                LPNMTREEVIEW pnmv = (LPNMTREEVIEW)lParam;

                // if (pnmv->uChanged & TVIF_STATE) /* The state has changed */
                if (pnmv->itemNew.mask & TVIF_STATE)
                {
                    /* The item has been (de)selected */
                    // if (pnmv->uNewState & TVIS_SELECTED)
                    if (pnmv->itemNew.state & TVIS_SELECTED)
                    {
                        HTLITEM hParentItem = TreeList_GetParent(lpnm->hwndFrom, pnmv->itemNew.hItem);
                        /* May or may not be a PPARTENTRY: this is a PPARTENTRY only when hParentItem != NULL */

                        if (!hParentItem)
                        {
                            /* Hard disk */
                            PDISKENTRY DiskEntry = (PDISKENTRY)pnmv->itemNew.lParam;
                            ASSERT(DiskEntry);

                            ShowWindow(GetDlgItem(hwndDlg, IDC_INITDISK), SW_SHOW);
                            ShowWindow(GetDlgItem(hwndDlg, IDC_PARTCREATE), SW_HIDE);
                            ShowWindow(GetDlgItem(hwndDlg, IDC_PARTDELETE), SW_HIDE);
                        #if 0 // FIXME: Init disk not implemented yet!
                            EnableDlgItem(hwndDlg, IDC_INITDISK,
                                          DiskEntry->DiskStyle == PARTITION_STYLE_RAW);
                        #else
                            EnableDlgItem(hwndDlg, IDC_INITDISK, FALSE);
                        #endif
                            EnableDlgItem(hwndDlg, IDC_PARTCREATE, FALSE);
                            EnableDlgItem(hwndDlg, IDC_PARTDELETE, FALSE);
                            goto DisableWizNext;
                        }
                        else
                        {
                            /* Partition or unpartitioned space */
                            PPARTINFO PartInfo = (PPARTINFO)pnmv->itemNew.lParam;
                            PPARTENTRY PartEntry;
                            ASSERT(PartInfo);
                            PartEntry = PartInfo->PartEntry;
                            ASSERT(PartEntry);

                            ShowWindow(GetDlgItem(hwndDlg, IDC_INITDISK), SW_HIDE);
                            ShowWindow(GetDlgItem(hwndDlg, IDC_PARTCREATE), SW_SHOW);
                            ShowWindow(GetDlgItem(hwndDlg, IDC_PARTDELETE), SW_SHOW);
                            EnableDlgItem(hwndDlg, IDC_INITDISK, FALSE);
                            EnableDlgItem(hwndDlg, IDC_PARTCREATE, !PartEntry->IsPartitioned);
                            EnableDlgItem(hwndDlg, IDC_PARTDELETE,  PartEntry->IsPartitioned);

                            if (PartEntry->IsPartitioned &&
                                // (PartEntry != PartEntry->DiskEntry->ExtendedPartition)
                                !IsContainerPartition(PartEntry->PartitionType) /* alternatively: PartEntry->PartitionNumber != 0 */ &&
                                ((PartEntry->Volume.FormatState == Formatted) || *PartInfo->FileSystemName))
                            {
                                PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
                            }
                            else
                            {
                                goto DisableWizNext;
                            }
                        }
                    }
                    else
                    {
DisableWizNext:
                        /*
                         * Keep the "Next" button disabled. It will be enabled only
                         * when the user selects a valid partition.
                         */
                        PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK);
                    }
                }

                break;
            }

            if (lpnm->idFrom == IDC_PARTITION && lpnm->code == TVN_DELETEITEM)
            {
                /* Deleting an item from the partition list */
                LPNMTREEVIEW pnmv = (LPNMTREEVIEW)lParam;
                DeleteTreeItem(lpnm->hwndFrom, (TLITEMW*)&pnmv->itemOld);
                break;
            }

            switch (lpnm->code)
            {
#if 0
                case PSN_SETACTIVE:
                {
                    /*
                     * Keep the "Next" button disabled. It will be enabled only
                     * when the user selects a valid partition.
                     */
                    PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK);
                    break;
                }
#endif

                case PSN_QUERYINITIALFOCUS:
                {
                    /* Give the focus on and select the first item */
                    hList = GetDlgItem(hwndDlg, IDC_PARTITION);
                    // TreeList_SetFocusItem(hList, 1, 1);
                    TreeList_SelectItem(hList, 1);
                    SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, (LONG_PTR)hList);
                    return TRUE;
                }

                case PSN_QUERYCANCEL:
                {
                    if (MessageBoxW(GetParent(hwndDlg),
                                    pSetupData->szAbortMessage,
                                    pSetupData->szAbortTitle,
                                    MB_YESNO | MB_ICONQUESTION) == IDYES)
                    {
                        /* Go to the Terminate page */
                        PropSheet_SetCurSelByID(GetParent(hwndDlg), IDD_RESTARTPAGE);
                    }

                    /* Do not close the wizard too soon */
                    SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, TRUE);
                    return TRUE;
                }

                case PSN_WIZNEXT: /* Set the selected data */
                {
                    PPARTINFO PartInfo = GetSelectedPartition(GetDlgItem(hwndDlg, IDC_PARTITION), NULL);
                    if (!PartInfo)
                    {
                        /* Fail and don't continue the installation */
                        SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, -1);
                        return TRUE;
                    }

                    InstallPartition = PartInfo->PartEntry;
                    ASSERT(InstallPartition);
                    break;
                }

                default:
                    break;
            }
        }
        break;

        default:
            break;
    }

    return FALSE;
}

/* EOF */
