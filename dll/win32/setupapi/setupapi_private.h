/*
 * Copyright 2001 Andreas Mohr
 * Copyright 2005-2006 Herv� Poussineau
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef __SETUPAPI_PRIVATE_H
#define __SETUPAPI_PRIVATE_H

#include <wchar.h>

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H

#define COBJMACROS

#include <windef.h>
#include <winbase.h>
#include <winuser.h>
#include <wingdi.h>
#include <winreg.h>
#include <wincon.h>

#include <commdlg.h>

#include <objbase.h>
#include <cfgmgr32.h>
#include <regstr.h>
#include <sddl.h>
#include <setupapi.h>
#include <softpub.h>
#include <mscat.h>
#include <lzexpand.h>
#include <shlobj.h>
#include <wine/unicode.h>
#define NTOS_MODE_USER
#include <ndk/rtlfuncs.h>

#include <wine/debug.h>
WINE_DEFAULT_DEBUG_CHANNEL(setupapi);

#ifdef __REACTOS__
#undef __WINESRC__
#endif

#ifdef __REACTOS__

#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))

#include <malloc.h>
// Redefine _recalloc since the header contains the function definition
// as exported from msvcrt.dll, which we do not currently support (NT 6+)
#define _recalloc _my_recalloc
static inline void* /*__cdecl*/ _recalloc(void *mem, size_t num, size_t size)
{
    size_t old_size;
    void *ret;

    if(!mem)
        return calloc(num, size);

    size = num*size;
    old_size = _msize(mem);

    ret = realloc(mem, size);
    if(!ret) {
        *_errno() = ENOMEM;
        return NULL;
    }

    if(size>old_size)
        memset((BYTE*)ret+old_size, 0, size-old_size);
    return ret;
}

#endif

#include <setupapi_undoc.h>

#include "resource.h"

#define SETUP_DEVICE_INFO_SET_MAGIC 0xd00ff057
#define SETUP_CLASS_IMAGE_LIST_MAGIC 0xd00ff058

#define CMP_MAGIC  0x01234567

struct DeviceInterface /* Element of DeviceInfo.InterfaceListHead */
{
    LIST_ENTRY ListEntry;

    /* Link to is parent device */
    struct DeviceInfo *DeviceInfo;
    GUID InterfaceClassGuid;


    /* SPINT_ACTIVE : the interface is active/enabled
     * SPINT_DEFAULT: the interface is the default interface for the device class
     * SPINT_REMOVED: the interface is removed
     */
    DWORD Flags;

    /* Contains the symbolic link of this interface, for example
     * \\?\ACPI#PNP0501#4&2658d0a0&0#{GUID} */
    WCHAR SymbolicLink[ANYSIZE_ARRAY];
};

/* We don't want to open the .inf file to read only one information in it, so keep a handle to it once it
 * has been already loaded once. Keep also a reference counter */
struct InfFileDetails
{
    /* Handle to the .inf file */
    HINF hInf;
    /* Reference count to this object. Once it raises 0, the .inf file is
     * automatically closed and this memory structure is deleted */
    LONG References;

    /* Contains the directory name of the .inf file.
     * Points into szData at then end of the structure */
    PCWSTR DirectoryName;
    /* Contains the .inf file name (without directory name).
     * Points into szData at then end of the structure */
    PCWSTR FileName;

    /* Variable size array (contains data for DirectoryName and FileName) */
    WCHAR szData[ANYSIZE_ARRAY];
};

struct DriverInfoElement /* Element of DeviceInfoSet.DriverListHead and DeviceInfo.DriverListHead */
{
    LIST_ENTRY ListEntry;

    SP_DRVINSTALL_PARAMS Params;
    ULARGE_INTEGER DriverDate;
    SP_DRVINFO_DATA_V2_W Info;
    SP_DRVINFO_DETAIL_DATA_W Details;
    GUID ClassGuid;
    LPWSTR MatchingId;
    struct InfFileDetails *InfFileDetails;
};

struct ClassInstallParams
{
    PSP_PROPCHANGE_PARAMS PropChangeParams;
    PSP_ADDPROPERTYPAGE_DATA AddPropertyPageData;
};

struct DeviceInfo /* Element of DeviceInfoSet.ListHead */
{
    LIST_ENTRY ListEntry;
    /* Used when dealing with CM_* functions */
    DEVINST dnDevInst;

    /* Link to parent DeviceInfoSet */
    struct DeviceInfoSet *set;

    /* Reserved Field of SP_DEVINSTALL_PARAMS_W structure
     * points to a struct DriverInfoElement */
    SP_DEVINSTALL_PARAMS_W InstallParams;

    /* Information about devnode:
     * - instanceId:
     *       "Root\*PNP0501" for example.
     *       It doesn't contain the unique ID for the device
     *       (points into the Data field at the end of the structure)
     *       WARNING: no NULL char exist between instanceId and UniqueId
     *       in Data field!
     * - UniqueId
     *       "5&1be2108e&0" or "0000"
     *       If DICD_GENERATE_ID is specified in creation flags,
     *       this unique ID is autogenerated using 4 digits, base 10
     *       (points into the Data field at the end of the structure)
     * - DeviceDescription
     *       String which identifies the device. Can be NULL. If not NULL,
     *       points into the Data field at the end of the structure
     * - ClassGuid
     *       Identifies the class of this device. It is GUID_NULL if the
     *       device has not been installed
     * - CreationFlags
     *       Is a combination of:
     *       - DICD_GENERATE_ID
     *              the unique ID needs to be generated
     *       - DICD_INHERIT_CLASSDRVS
     *              inherit driver of the device info set (== same pointer)
     */
    PCWSTR instanceId;
    PCWSTR UniqueId;
    PCWSTR DeviceDescription;
    GUID ClassGuid;
    DWORD CreationFlags;

    /* If CreationFlags contains DICD_INHERIT_CLASSDRVS, this list is invalid */
    /* If the driver is not searched/detected, this list is empty */
    LIST_ENTRY DriverListHead; /* List of struct DriverInfoElement */

    /* List of interfaces implemented by this device */
    LIST_ENTRY InterfaceListHead; /* List of struct DeviceInterface */

    /* Used by SetupDiGetClassInstallParamsW/SetupDiSetClassInstallParamsW */
    struct ClassInstallParams ClassInstallParams;

    /* Device property page provider data */
    HMODULE hmodDevicePropPageProvider;
    PVOID pDevicePropPageProvider;

    /* Variable size array (contains data for instanceId, UniqueId, DeviceDescription) */
    WCHAR Data[ANYSIZE_ARRAY];
};

struct DeviceInfoSet /* HDEVINFO */
{
    DWORD magic; /* SETUP_DEVICE_INFO_SET_MAGIC */
    /* If != GUID_NULL, only devices of this class can be in the device info set */
    GUID ClassGuid;
    /* Local or distant HKEY_LOCAL_MACHINE registry key */
    HKEY HKLM;
    /* Used when dealing with CM_* functions */
    HMACHINE hMachine;

    /* Reserved Field points to a struct DriverInfoElement */
    SP_DEVINSTALL_PARAMS_W InstallParams;

    /* List of struct DriverInfoElement (if no driver has been
     * searched/detected, this list is empty) */
    LIST_ENTRY DriverListHead;

    /* List of struct DeviceInfo */
    LIST_ENTRY ListHead;
    struct DeviceInfo *SelectedDevice;

    /* Used by SetupDiGetClassInstallParamsW/SetupDiSetClassInstallParamsW */
    struct ClassInstallParams ClassInstallParams;

    /* Class property page provider data */
    HMODULE hmodClassPropPageProvider;
    PVOID pClassPropPageProvider;

    /* Contains the name of the remote computer ('\\COMPUTERNAME' for example),
     * or NULL if related to local machine. Points into szData field at the
     * end of the structure */
    PCWSTR MachineName;

    /* Variable size array (contains data for MachineName) */
    WCHAR szData[ANYSIZE_ARRAY];
};

struct ClassImageList
{
    DWORD magic; /* SETUP_CLASS_IMAGE_LIST_MAGIC */

    /* Number of GUIDs contained in Guids and IconIndexes arrays */
    DWORD NumberOfGuids;
    /* Array of GUIDs associated to icons of the image list. Its size
     * is NumberOfGuids and is pointing after the end this structure */
    GUID* Guids;
    /* Array of corresponding icons index in the image list. Its size
     * is NumberOfGuids and is pointing after the end this structure */
    INT* IconIndexes;
};

struct FileLog /* HSPFILELOG */
{
    DWORD ReadOnly;
    DWORD SystemLog;
    LPWSTR LogName;
};

extern HINSTANCE SETUPAPI_hInstance;
extern OSVERSIONINFOEXW OsVersionInfo;

/*
 * See: https://msdn.microsoft.com/en-us/library/bb432397(v=vs.85).aspx
 * for more information.
 */
extern DWORD GlobalSetupFlags;
#define PSPGF_NO_BACKUP         0x0002
#define PSPGF_NONINTERACTIVE    0x0004

#define RC_STRING_MAX_SIZE 256

#define REG_INSTALLEDFILES "System\\CurrentControlSet\\Control\\InstalledFiles"
#define REGPART_RENAME "\\Rename"
#define REG_VERSIONCONFLICT "Software\\Microsoft\\VersionConflictManager"

static inline WCHAR *strdupW( const WCHAR *str )
{
    WCHAR *ret = NULL;
    if (str)
    {
        int len = (strlenW(str) + 1) * sizeof(WCHAR);
        if ((ret = HeapAlloc( GetProcessHeap(), 0, len ))) memcpy( ret, str, len );
    }
    return ret;
}

static inline char *strdupWtoA( const WCHAR *str )
{
    char *ret = NULL;
    if (str)
    {
        DWORD len = WideCharToMultiByte( CP_ACP, 0, str, -1, NULL, 0, NULL, NULL );
        if ((ret = HeapAlloc( GetProcessHeap(), 0, len )))
            WideCharToMultiByte( CP_ACP, 0, str, -1, ret, len, NULL, NULL );
    }
    return ret;
}

static inline WCHAR *strdupAtoW( const char *str )
{
    WCHAR *ret = NULL;
    if (str)
    {
        DWORD len = MultiByteToWideChar( CP_ACP, 0, str, -1, NULL, 0 );
        if ((ret = HeapAlloc( GetProcessHeap(), 0, len * sizeof(WCHAR) )))
            MultiByteToWideChar( CP_ACP, 0, str, -1, ret, len );
    }
    return ret;
}

/* string substitutions */

struct inf_file;
extern const WCHAR *DIRID_get_string( int dirid );
extern const WCHAR *PARSER_get_inf_filename( HINF hinf ) DECLSPEC_HIDDEN;
extern WCHAR *PARSER_get_src_root( HINF hinf ) DECLSPEC_HIDDEN;
extern WCHAR *PARSER_get_dest_dir( INFCONTEXT *context ) DECLSPEC_HIDDEN;

/* support for Ascii queue callback functions */

struct callback_WtoA_context
{
    void               *orig_context;
    PSP_FILE_CALLBACK_A orig_handler;
};

UINT CALLBACK QUEUE_callback_WtoA( void *context, UINT notification, UINT_PTR, UINT_PTR );

/* devinst.c */

DWORD
GetErrorCodeFromCrCode(const IN CONFIGRET cr);

BOOL
CreateDeviceInfo(
    IN struct DeviceInfoSet *list,
    IN LPCWSTR InstancePath,
    IN LPCGUID pClassGuid,
    OUT struct DeviceInfo **pDeviceInfo);

LONG
SETUP_CreateDevicesList(
    IN OUT struct DeviceInfoSet *list,
    IN PCWSTR MachineName OPTIONAL,
    IN CONST GUID *Class OPTIONAL,
    IN PCWSTR Enumerator OPTIONAL);

HKEY SETUPDI_CreateDevKey(HKEY RootKey, struct DeviceInfo *devInfo, REGSAM samDesired);
HKEY SETUPDI_CreateDrvKey(HKEY RootKey, struct DeviceInfo *devInfo, UUID *ClassGuid, REGSAM samDesired);
HKEY SETUPDI_OpenDevKey(HKEY RootKey, struct DeviceInfo *devInfo, REGSAM samDesired);
HKEY SETUPDI_OpenDrvKey(HKEY RootKey, struct DeviceInfo *devInfo, REGSAM samDesired);

/* driver.c */

struct InfFileDetails *
CreateInfFileDetails(
    IN LPCWSTR FullInfFileName);

VOID
DereferenceInfFile(struct InfFileDetails* infFile);

BOOL
DestroyDriverInfoElement(struct DriverInfoElement* driverInfo);

/* install.c */

BOOL
GetStringField( PINFCONTEXT context, DWORD index, PWSTR *value);

/* interface.c */

BOOL
DestroyDeviceInterface(
    struct DeviceInterface* deviceInterface);

LONG
SETUP_CreateInterfaceList(
    struct DeviceInfoSet *list,
    PCWSTR MachineName,
    CONST GUID *InterfaceGuid,
    PCWSTR DeviceInstanceW /* OPTIONAL */,
    BOOL OnlyPresentInterfaces);

/* misc.c */

DWORD
GetFunctionPointer(
    IN PWSTR InstallerName,
    OUT HMODULE* ModulePointer,
    OUT PVOID* FunctionPointer);

DWORD
FreeFunctionPointer(
    IN HMODULE ModulePointer,
    IN PVOID FunctionPointer);

DWORD
WINAPI
pSetupStringFromGuid(LPGUID lpGUID, PWSTR pString, DWORD dwStringLen);

DWORD WINAPI CaptureAndConvertAnsiArg(LPCSTR pSrc, LPWSTR *pDst);

VOID WINAPI MyFree(LPVOID lpMem);
LPVOID WINAPI MyMalloc(DWORD dwSize);
LPVOID WINAPI MyRealloc(LPVOID lpSrc, DWORD dwSize);
LPWSTR WINAPI DuplicateString(LPCWSTR lpSrc);
BOOL WINAPI IsUserAdmin(VOID);
LPWSTR WINAPI MultiByteToUnicode(LPCSTR lpMultiByteStr, UINT uCodePage);
LPSTR WINAPI UnicodeToMultiByte(LPCWSTR lpUnicodeStr, UINT uCodePage);

/* parser.c */

typedef BOOL (*FIND_CALLBACK)(LPCWSTR SectionName, PVOID Context);
BOOL EnumerateSectionsStartingWith(HINF hInf, LPCWSTR pStr, FIND_CALLBACK Callback, PVOID Context);

#endif /* __SETUPAPI_PRIVATE_H */
