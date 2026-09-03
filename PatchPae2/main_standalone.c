/*
 * PatchPae2 by wj32
 * Standalone build — Process Hacker 2 (phlib) dependency removed
 * for cross-compilation with MinGW-w64.
 *
 * Original: https://github.com/wj32/PatchPae2
 *
 * All patching logic is IDENTICAL to the original.
 * Only the command-line parsing and utility functions have been
 * reimplemented using standard Win32 APIs.
 */

#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <imagehlp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TYPE_KERNEL 1
#define TYPE_LOADER 2

typedef VOID (*PPATCH_FUNCTION)(
    PLOADED_IMAGE LoadedImage,
    PBOOLEAN Success
    );

/*
 * ========================================================================
 * Utility functions (replacing phlib equivalents)
 * ========================================================================
 */

static VOID Fail(
    LPCWSTR Message,
    DWORD Win32Result
    )
{
    if (Win32Result == 0)
    {
        wprintf(L"%s\n", Message);
    }
    else
    {
        LPWSTR errorMsg = NULL;
        FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL, Win32Result,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPWSTR)&errorMsg, 0, NULL);
        if (errorMsg)
        {
            wprintf(L"%s: %s", Message, errorMsg);
            LocalFree(errorMsg);
        }
        else
        {
            wprintf(L"%s: Error %lu\n", Message, Win32Result);
        }
    }

    ExitProcess(1);
}

static ULONG GetBuildNumber(
    LPCWSTR FileName
    )
{
    DWORD handle;
    DWORD size;
    PVOID versionInfo;
    VS_FIXEDFILEINFO *rootBlock;
    UINT rootBlockLength;
    ULONG buildNumber = 0;

    size = GetFileVersionInfoSizeW(FileName, &handle);
    if (size == 0)
        return 0;

    versionInfo = malloc(size);
    if (!versionInfo)
        return 0;

    if (!GetFileVersionInfoW(FileName, handle, size, versionInfo))
    {
        free(versionInfo);
        return 0;
    }

    if (VerQueryValueW(versionInfo, L"\\", (LPVOID *)&rootBlock, &rootBlockLength) &&
        rootBlockLength != 0)
        buildNumber = rootBlock->dwFileVersionLS >> 16;

    free(versionInfo);
    return buildNumber;
}

static ULONG GetRevisionNumber(
    LPCWSTR FileName
    )
{
    DWORD handle;
    DWORD size;
    PVOID versionInfo;
    VS_FIXEDFILEINFO *rootBlock;
    UINT rootBlockLength;
    ULONG revisionNumber = 0;

    size = GetFileVersionInfoSizeW(FileName, &handle);
    if (size == 0)
        return 0;

    versionInfo = malloc(size);
    if (!versionInfo)
        return 0;

    if (!GetFileVersionInfoW(FileName, handle, size, versionInfo))
    {
        free(versionInfo);
        return 0;
    }

    if (VerQueryValueW(versionInfo, L"\\", (LPVOID *)&rootBlock, &rootBlockLength) &&
        rootBlockLength != 0)
        revisionNumber = rootBlock->dwFileVersionLS & 0xffff;

    free(versionInfo);
    return revisionNumber;
}

static VOID Patch(
    LPCWSTR FileName,
    PPATCH_FUNCTION Action
    )
{
    BOOLEAN success;
    LOADED_IMAGE loadedImage;
    char mbFileName[MAX_PATH];

    WideCharToMultiByte(CP_ACP, 0, FileName, -1, mbFileName, MAX_PATH, NULL, NULL);

    if (!MapAndLoad(mbFileName, NULL, &loadedImage, FALSE, FALSE))
        Fail(L"Unable to map and load image", GetLastError());

    success = FALSE;
    Action(&loadedImage, &success);
    /* This will unload the image and fix the checksum. */
    UnMapAndLoad(&loadedImage);

    if (success)
        wprintf(L"Patched.\n");
    else
        Fail(L"Failed.", 0);
}

/*
 * ========================================================================
 * Kernel patch functions (IDENTICAL to original)
 * ========================================================================
 */

VOID PatchKernel(
    PLOADED_IMAGE LoadedImage,
    PBOOLEAN Success
    )
{
    /* MxMemoryLicense */

    /* Basically, the portion of code we are going to patch
     * queries the NT license value for the allowed memory.
     * If there is a limit, it sets MiTotalPagesAllowed to
     * that limit times 256. If there is no specified limit,
     * it sets MiTotalPagesAllowed to 0x80000 (2 GB).
     *
     * We will patch the limit to be 0x20000 << 8 pages (128 GB). */

    UCHAR target[] =
    {
        /* test eax, eax ; did ZwQueryLicenseValue succeed? */
        0x85, 0xc0,
        /* jl short loc_75644b ; if it didn't go to the default case */
        0x7c, 0x11,
        /* mov eax, [ebp+var_4] ; get the returned memory limit */
        0x8b, 0x45, 0xfc,
        /* test eax, eax ; is it non-zero? */
        0x85, 0xc0,
        /* jz short loc_75644b ; if it's zero, go to the default case */
        0x74, 0x0a,
        /* shl eax, 8 ; multiply by 256 */
        0xc1, 0xe0, 0x08
    };
    ULONG movOffset = 4;
    PUCHAR ptr = LoadedImage->MappedAddress;
    ULONG i, j, k;

    for (i = 0; i < LoadedImage->SizeOfImage - sizeof(target); i++)
    {
        for (j = 0; j < sizeof(target); j++)
        {
            if (ptr[j] != target[j])
                break;
        }

        if (j == sizeof(target))
        {
            /* Found it. Patch the code. */

            /* mov eax, [ebp+var_4] -> mov eax, 0x20000 */
            ptr[movOffset] = 0xb8;
            *(PULONG)&ptr[movOffset + 1] = 0x20000;
            /* nop out the jz */
            ptr[movOffset + 5] = 0x90;
            ptr[movOffset + 6] = 0x90;

            /* Do the same thing to the next mov eax, [ebp+var_4]
             * occurence. */
            for (k = 0; k < 100; k++)
            {
                if (
                    ptr[k] == 0x8b &&
                    ptr[k + 1] == 0x45 &&
                    ptr[k + 2] == 0xfc &&
                    ptr[k + 3] == 0x85 &&
                    ptr[k + 4] == 0xc0
                    )
                {
                    /* mov eax, [ebp+var_4] -> mov eax, 0x20000 */
                    ptr[k] = 0xb8;
                    *(PULONG)&ptr[k + 1] = 0x20000;
                    /* nop out the jz */
                    ptr[k + 5] = 0x90;
                    ptr[k + 6] = 0x90;

                    *Success = TRUE;

                    break;
                }
            }

            break;
        }

        ptr++;
    }
}

VOID PatchKernel9200(
    PLOADED_IMAGE LoadedImage,
    PBOOLEAN Success
    )
{
    UCHAR target[] =
    {
        0x85, 0xc0,
        0x78, 0x4c,
        0x8b, 0x45, 0xfc,
        0x85, 0xc0,
        0x74, 0x45,
        0xc1, 0xe0, 0x08
    };
    ULONG movOffset = 4;
    PUCHAR ptr = LoadedImage->MappedAddress;
    ULONG i, j, k;

    for (i = 0; i < LoadedImage->SizeOfImage - sizeof(target); i++)
    {
        for (j = 0; j < sizeof(target); j++)
        {
            if (ptr[j] != target[j] && j != 3 && j != 10)
                break;
        }

        if (j == sizeof(target))
        {
            ptr[movOffset] = 0xb8;
            *(PULONG)&ptr[movOffset + 1] = 0x20000;
            ptr[movOffset + 5] = 0x90;
            ptr[movOffset + 6] = 0x90;

            for (k = 0; k < 100; k++)
            {
                if (
                    ptr[k] == 0x8b &&
                    ptr[k + 1] == 0x45 &&
                    ptr[k + 2] == 0xfc &&
                    ptr[k + 3] == 0x85 &&
                    ptr[k + 4] == 0xc0
                    )
                {
                    ptr[k] = 0xb8;
                    *(PULONG)&ptr[k + 1] = 0x20000;
                    ptr[k + 5] = 0x90;
                    ptr[k + 6] = 0x90;

                    *Success = TRUE;

                    break;
                }
            }

            break;
        }

        ptr++;
    }
}

VOID PatchKernel9600(
    PLOADED_IMAGE LoadedImage,
    PBOOLEAN Success
    )
{
    UCHAR target[] =
    {
        0x85, 0xc0,
        0x78, 0x50,
        0x8b, 0x45, 0xfc,
        0x85, 0xc0,
        0x74, 0x49,
        0xc1, 0xe0, 0x08
    };
    ULONG movOffset = 4;
    PUCHAR ptr = LoadedImage->MappedAddress;
    ULONG i, j, k;

    for (i = 0; i < LoadedImage->SizeOfImage - sizeof(target); i++)
    {
        for (j = 0; j < sizeof(target); j++)
        {
            if (ptr[j] != target[j] && j != 3 && j != 10)
                break;
        }

        if (j == sizeof(target))
        {
            ptr[movOffset] = 0xb8;
            *(PULONG)&ptr[movOffset + 1] = 0x20000;
            ptr[movOffset + 5] = 0x90;
            ptr[movOffset + 6] = 0x90;

            for (k = 0; k < 100; k++)
            {
                if (
                    ptr[k] == 0x8b &&
                    ptr[k + 1] == 0x45 &&
                    ptr[k + 2] == 0xfc &&
                    ptr[k + 3] == 0x85 &&
                    ptr[k + 4] == 0xc0
                    )
                {
                    ptr[k] = 0xb8;
                    *(PULONG)&ptr[k + 1] = 0x20000;
                    ptr[k + 5] = 0x90;
                    ptr[k + 6] = 0x90;

                    *Success = TRUE;

                    break;
                }
            }

            break;
        }

        ptr++;
    }
}

VOID PatchKernel10586(
    PLOADED_IMAGE LoadedImage,
    PBOOLEAN Success
    )
{
    UCHAR target[] =
    {
        0x85, 0xc0,
        0x78, 0x46,
        0x8b, 0x75, 0xfc,
        0x85, 0xf6,
        0x74, 0x3f,
        0xc1, 0xe6, 0x08
    };
    ULONG movOffset = 4;
    PUCHAR ptr = LoadedImage->MappedAddress;
    ULONG i, j, k;

    for (i = 0; i < LoadedImage->SizeOfImage - sizeof(target); i++)
    {
        for (j = 0; j < sizeof(target); j++)
        {
            if (ptr[j] != target[j] && j != 3 && j != 10)
                break;
        }

        if (j == sizeof(target))
        {
            /* mov esi, [ebp+Address] -> mov esi, 0x20000 */
            ptr[movOffset] = 0xbe;
            *(PULONG)&ptr[movOffset + 1] = 0x20000;
            ptr[movOffset + 5] = 0x90;
            ptr[movOffset + 6] = 0x90;

            for (k = 0; k < 100; k++)
            {
                if (
                    ptr[k] == 0x8b &&
                    ptr[k + 1] == 0x4d &&
                    ptr[k + 2] == 0xfc &&
                    ptr[k + 3] == 0x85 &&
                    ptr[k + 4] == 0xc9
                    )
                {
                    /* mov ecx, [ebp+Address] -> mov ecx, 0x20000 */
                    ptr[k] = 0xb9;
                    *(PULONG)&ptr[k + 1] = 0x20000;
                    ptr[k + 5] = 0x90;
                    ptr[k + 6] = 0x90;

                    *Success = TRUE;

                    break;
                }
            }

            break;
        }

        ptr++;
    }
}

/*
 * ========================================================================
 * Loader patch functions (IDENTICAL to original)
 * ========================================================================
 */

VOID PatchLoader(
    PLOADED_IMAGE LoadedImage,
    PBOOLEAN Success
    )
{
    UCHAR target[] =
    {
        0x2b, 0x73, 0x04,
        0x50,
        0x03, 0x75, 0xe8,
        0x8d, 0x45, 0x8c,
        0x50,
        0x56,
        0x8b, 0xc3
    };
    ULONG movOffset = 19;
    PUCHAR ptr = LoadedImage->MappedAddress;
    ULONG i, j;

    for (i = 0; i < LoadedImage->SizeOfImage - sizeof(target); i++)
    {
        for (j = 0; j < sizeof(target); j++)
        {
            if (ptr[j] != target[j])
                break;
        }

        if (j == sizeof(target))
        {
            /* mov ecx, eax -> mov [ebp+arg_0], 0 */
            ptr[movOffset] = 0xc7;
            ptr[movOffset + 1] = 0x45;
            ptr[movOffset + 2] = 0x08;
            ptr[movOffset + 3] = 0x00;
            ptr[movOffset + 4] = 0x00;
            ptr[movOffset + 5] = 0x00;
            ptr[movOffset + 6] = 0x00;
            /* jge short -> jmp short */
            ptr[movOffset + 7] = 0xeb;

            *Success = TRUE;

            break;
        }

        ptr++;
    }
}

VOID PatchLoader7600(
    PLOADED_IMAGE LoadedImage,
    PBOOLEAN Success
    )
{
    UCHAR target[] =
    {
        0x50,
        0x8d, 0x85, 0x94, 0xfe, 0xff, 0xff,
        0x50,
        0xff, 0xb5, 0xd4, 0xfe, 0xff, 0xff,
        0x8b, 0x45, 0xdc,
        0xff, 0x75, 0xe8
    };
    ULONG jgeOffset = 30;
    PUCHAR ptr = LoadedImage->MappedAddress;
    ULONG i, j;

    for (i = 0; i < LoadedImage->SizeOfImage - sizeof(target); i++)
    {
        for (j = 0; j < sizeof(target); j++)
        {
            if (ptr[j] != target[j])
                break;
        }

        if (j == sizeof(target))
        {
            /* jge short -> jmp short */
            ptr[jgeOffset] = 0xeb;

            *Success = TRUE;

            break;
        }

        ptr++;
    }
}

VOID PatchLoader7601(
    PLOADED_IMAGE LoadedImage,
    PBOOLEAN Success
    )
{
    UCHAR target[] =
    {
        0x50,
        0x8d, 0x85, 0x94, 0xfe, 0xff, 0xff,
        0x50,
        0xff, 0xb5, 0xd4, 0xfe, 0xff, 0xff,
        0x8b, 0x45, 0xdc,
        0xff, 0x75, 0xe8
    };
    ULONG jgeOffset = 30;
    PUCHAR ptr = LoadedImage->MappedAddress;
    ULONG i, j;

    for (i = 0; i < LoadedImage->SizeOfImage - sizeof(target); i++)
    {
        for (j = 0; j < sizeof(target); j++)
        {
            if (ptr[j] != target[j])
                break;
        }

        if (j == sizeof(target))
        {
            /* jge short -> jmp short */
            ptr[jgeOffset] = 0xeb;

            *Success = TRUE;

            break;
        }

        ptr++;
    }
}

VOID PatchLoader7601_23569(
    PLOADED_IMAGE LoadedImage,
    PBOOLEAN Success
    )
{
    /* ImgpValidateImageHash - patch to always return 0 */
    UCHAR target[] =
    {
        /* mov [esp+70h+var_58], 0C0000428h ; critical service failed */
        0xC7, 0x44, 0x24, 0x18, 0x28, 0x04, 0x00, 0xC0,
        /* mov eax, [esp+70h+var_58] */
        0x8B, 0x44, 0x24, 0x18,
    };
    ULONG jgeOffset = 8;
    PUCHAR ptr = LoadedImage->MappedAddress;
    ULONG i, j;

    *Success = FALSE;

    for (i = 0; i < LoadedImage->SizeOfImage - sizeof(target); i++)
    {
        for (j = 0; j < sizeof(target); j++)
        {
            if (ptr[j] != target[j])
                break;
        }

        if (j == sizeof(target))
        {
            /* mov eax, [esp+70h+var_58] -> xor eax, eax; nop; nop */
            memcpy(ptr + jgeOffset, "\x31\xC0\x90\x90", 4);

            *Success = TRUE;

            break;
        }

        ptr++;
    }
}

/* PatchLoader7601_23569_0 - alternate version from Elbandi's fork */
VOID PatchLoader7601_23569_0(
    PLOADED_IMAGE LoadedImage,
    PBOOLEAN Success
    )
{
    UCHAR target[] =
    {
        0x8d, 0x85, 0x94, 0xfe, 0xff, 0xff,
        0x50,
        0xff, 0x75, 0xd8,
        0x8b, 0x45, 0x08,
        0xff, 0x70, 0x0c,
        0x8d, 0x45, 0x9c
    };
    ULONG jgeOffset = 29;
    PUCHAR ptr = LoadedImage->MappedAddress;
    ULONG i, j;

    for (i = 0; i < LoadedImage->SizeOfImage - sizeof(target); i++)
    {
        for (j = 0; j < sizeof(target); j++)
        {
            if (ptr[j] != target[j])
                break;
        }

        if (j == sizeof(target))
        {
            /* jge short -> jmp short */
            ptr[jgeOffset] = 0xeb;

            *Success = TRUE;

            break;
        }

        ptr++;
    }
}

/* Windows 8 (build 9200) loader patches */

static VOID PatchLoader9200Part1(
    PLOADED_IMAGE LoadedImage,
    PBOOLEAN Success
    )
{
    UCHAR target[] =
    {
        0x50,
        0xff, 0x75, 0xec,
        0x8d, 0x85, 0xc4, 0xfe, 0xff, 0xff,
        0x50,
        0x51,
        0xff, 0x76, 0x0c,
        0x8d, 0x45, 0x8c
    };
    ULONG jnsOffset = 27;
    PUCHAR ptr = LoadedImage->MappedAddress;
    ULONG i, j;

    for (i = 0; i < LoadedImage->SizeOfImage - sizeof(target); i++)
    {
        for (j = 0; j < sizeof(target); j++)
        {
            if (ptr[j] != target[j])
                break;
        }

        if (j == sizeof(target))
        {
            /* jns short -> jmp short */
            ptr[jnsOffset] = 0xeb;

            *Success = TRUE;

            break;
        }

        ptr++;
    }
}

static VOID PatchLoader9200Part2(
    PLOADED_IMAGE LoadedImage,
    PBOOLEAN Success
    )
{
    UCHAR target[] =
    {
        0x6a, 0x00,
        0xff, 0x75, 0xe8,
        0x8d, 0x45, 0x88,
        0x50,
        0xff, 0xb5, 0xb0, 0xfe, 0xff, 0xff,
        0x33, 0xc0,
        0xff, 0x75, 0x10
    };
    ULONG movOffset = 25;
    PUCHAR ptr = LoadedImage->MappedAddress;
    ULONG i, j;

    for (i = 0; i < LoadedImage->SizeOfImage - sizeof(target); i++)
    {
        for (j = 0; j < sizeof(target); j++)
        {
            if (ptr[j] != target[j])
                break;
        }

        if (j == sizeof(target))
        {
            /* mov ebx, eax -> xor ebx, ebx */
            ptr[movOffset] = 0x33;
            ptr[movOffset + 1] = 0xdb;

            *Success = TRUE;

            break;
        }

        ptr++;
    }
}

VOID PatchLoader9200(
    PLOADED_IMAGE LoadedImage,
    PBOOLEAN Success
    )
{
    BOOLEAN success1 = FALSE;
    BOOLEAN success2 = FALSE;

    PatchLoader9200Part1(LoadedImage, &success1);
    PatchLoader9200Part2(LoadedImage, &success2);
    *Success = success1 && success2;
}

/* Windows 8.1 (build 9600) loader patches */

static VOID PatchLoader9600Part1(
    PLOADED_IMAGE LoadedImage,
    PBOOLEAN Success
    )
{
    UCHAR target[] =
    {
        0x50,
        0xff, 0x75, 0x88,
        0x8d, 0x85, 0xb8, 0xfe, 0xff, 0xff,
        0xff, 0x75, 0xec,
        0x50,
        0x8b, 0x45, 0xd0,
        0x51,
        0x8b, 0x48, 0x0c
    };
    ULONG jnsOffset = 30;
    PUCHAR ptr = LoadedImage->MappedAddress;
    ULONG i, j;

    for (i = 0; i < LoadedImage->SizeOfImage - sizeof(target); i++)
    {
        for (j = 0; j < sizeof(target); j++)
        {
            if (ptr[j] != target[j])
                break;
        }

        if (j == sizeof(target))
        {
            /* jns short -> jmp short */
            ptr[jnsOffset] = 0xeb;

            *Success = TRUE;

            break;
        }

        ptr++;
    }
}

static VOID PatchLoader9600Part2(
    PLOADED_IMAGE LoadedImage,
    PBOOLEAN Success
    )
{
    UCHAR target[] =
    {
        0x6a, 0x00,
        0x6a, 0x00,
        0xff, 0x75, 0xd0,
        0x33, 0xd2,
        0xff, 0x75, 0xe0,
        0x50,
        0xff, 0xb5, 0x9c, 0xfe, 0xff, 0xff
    };
    ULONG movOffset = 24;
    PUCHAR ptr = LoadedImage->MappedAddress;
    ULONG i, j;

    for (i = 0; i < LoadedImage->SizeOfImage - sizeof(target); i++)
    {
        for (j = 0; j < sizeof(target); j++)
        {
            if (ptr[j] != target[j])
                break;
        }

        if (j == sizeof(target))
        {
            /* mov esi, eax -> xor esi, esi */
            ptr[movOffset] = 0x33;
            ptr[movOffset + 1] = 0xf6;

            *Success = TRUE;

            break;
        }

        ptr++;
    }
}

VOID PatchLoader9600(
    PLOADED_IMAGE LoadedImage,
    PBOOLEAN Success
    )
{
    BOOLEAN success1 = FALSE;
    BOOLEAN success2 = FALSE;

    PatchLoader9600Part1(LoadedImage, &success1);
    PatchLoader9600Part2(LoadedImage, &success2);
    *Success = success1 && success2;
}

/* Windows 10 (build 10586) loader patches */

static VOID PatchLoader10586Part1(
    PLOADED_IMAGE LoadedImage,
    PBOOLEAN Success
    )
{
    UCHAR target[] =
    {
        0x8d, 0x85, 0x80, 0xfe, 0xff, 0xff,
        0xff, 0x75, 0xf0,
        0x50,
        0x51,
        0x8d, 0x85, 0x44, 0xff, 0xff, 0xff,
        0x50,
        0x8b, 0x45, 0xd0,
        0x56,
        0x8b, 0x48, 0x0c
    };

    BOOL wildcardOffsets[sizeof(target)];
    memset(wildcardOffsets, 0, sizeof(wildcardOffsets));
    wildcardOffsets[2] = TRUE;
    wildcardOffsets[13] = TRUE;
    wildcardOffsets[20] = TRUE;

    ULONG jnsOffset = 34;
    PUCHAR ptr = LoadedImage->MappedAddress;
    ULONG i, j;

    for (i = 0; i < LoadedImage->SizeOfImage - sizeof(target); i++)
    {
        for (j = 0; j < sizeof(target); j++)
        {
            if ((ptr[j] != target[j]) && !wildcardOffsets[j])
                break;
        }

        if (j == sizeof(target))
        {
            /* js short -> nop x6 */
            ptr[jnsOffset] = 0x90;
            ptr[jnsOffset + 1] = 0x90;
            ptr[jnsOffset + 2] = 0x90;
            ptr[jnsOffset + 3] = 0x90;
            ptr[jnsOffset + 4] = 0x90;
            ptr[jnsOffset + 5] = 0x90;

            *Success = TRUE;

            break;
        }

        ptr++;
    }
}

static VOID PatchLoader10586Part2(
    PLOADED_IMAGE LoadedImage,
    PBOOLEAN Success
    )
{
    UCHAR target[] =
    {
        0x51,
        0x51,
        0x51,
        0xff, 0x75, 0xcc,
        0xff, 0x75, 0xd8,
        0x50,
        0xff, 0xb5, 0x94, 0xfe, 0xff, 0xff,
        0x51,
        0xff, 0x75, 0xf4,
        0x8b, 0x4d, 0x08
    };
    ULONG wildcardOffset = 8;
    ULONG movOffset = 28;
    PUCHAR ptr = LoadedImage->MappedAddress;
    ULONG i, j;

    for (i = 0; i < LoadedImage->SizeOfImage - sizeof(target); i++)
    {
        for (j = 0; j < sizeof(target); j++)
        {
            if ((ptr[j] != target[j]) && j != wildcardOffset)
                break;
        }

        if (j == sizeof(target))
        {
            /* mov esi, eax -> xor esi, esi */
            ptr[movOffset] = 0x33;
            ptr[movOffset + 1] = 0xf6;

            *Success = TRUE;

            break;
        }

        ptr++;
    }
}

VOID PatchLoader10586(
    PLOADED_IMAGE LoadedImage,
    PBOOLEAN Success
    )
{
    BOOLEAN success1 = FALSE;
    BOOLEAN success2 = FALSE;

    PatchLoader10586Part1(LoadedImage, &success1);
    PatchLoader10586Part2(LoadedImage, &success2);
    *Success = success1 && success2;
}

/*
 * ========================================================================
 * Main entry point
 * ========================================================================
 */

int wmain(int argc, wchar_t *argv[])
{
    LPCWSTR inputFile = NULL;
    LPCWSTR outputFile = NULL;
    LPCWSTR typeStr = NULL;
    int typeInteger = TYPE_KERNEL;
    ULONG buildNumber, revisionNumber;
    WCHAR failMsg[256];
    int i;

    /* Parse command line */
    for (i = 1; i < argc; i++)
    {
        if (_wcsicmp(argv[i], L"-o") == 0 && i + 1 < argc)
        {
            outputFile = argv[++i];
        }
        else if (_wcsicmp(argv[i], L"-type") == 0 && i + 1 < argc)
        {
            typeStr = argv[++i];
        }
        else
        {
            if (!inputFile)
                inputFile = argv[i];
        }
    }

    if (typeStr)
    {
        if (_wcsicmp(typeStr, L"kernel") == 0)
            typeInteger = TYPE_KERNEL;
        else if (_wcsicmp(typeStr, L"loader") == 0)
            typeInteger = TYPE_LOADER;
        else
            Fail(L"Wrong type. Must be \"kernel\" or \"loader\".", 0);
    }

    if (!inputFile || inputFile[0] == 0)
        Fail(L"Input file not specified!", 0);
    if (!outputFile || outputFile[0] == 0)
        Fail(L"Output file not specified!", 0);

    if (!CopyFileW(inputFile, outputFile, FALSE))
        Fail(L"Unable to copy file", GetLastError());

    if (!(buildNumber = GetBuildNumber(outputFile)))
        Fail(L"Unable to get the build number of the file.", 0);

    if (!(revisionNumber = GetRevisionNumber(outputFile)))
        Fail(L"Unable to get the revision number of the file.", 0);

    wprintf(L"Build %lu, Revision %lu\n", buildNumber, revisionNumber);

    if (typeInteger == TYPE_KERNEL)
    {
        if (buildNumber < 9200)
            Patch(outputFile, PatchKernel);
        else if (buildNumber == 9200)
            Patch(outputFile, PatchKernel9200);
        else if (buildNumber == 9600)
            Patch(outputFile, PatchKernel9600);
        else if (buildNumber == 10586)
            Patch(outputFile, PatchKernel10586);
        else
        {
            swprintf(failMsg, 256, L"Unsupported kernel version: %lu", buildNumber);
            Fail(failMsg, 0);
        }
    }
    else
    {
        if (buildNumber < 7600)
            Patch(outputFile, PatchLoader);
        else if (buildNumber == 7600)
            Patch(outputFile, PatchLoader7600);
        else if (buildNumber == 7601)
        {
            if (revisionNumber >= 23569)
                Patch(outputFile, PatchLoader7601_23569);
            else
                Patch(outputFile, PatchLoader7601);
        }
        else if (buildNumber == 9200)
            Patch(outputFile, PatchLoader9200);
        else if (buildNumber == 9600)
            Patch(outputFile, PatchLoader9600);
        else if (buildNumber >= 10240)
            Patch(outputFile, PatchLoader10586);
        else
        {
            swprintf(failMsg, 256, L"Unsupported loader version: %lu", buildNumber);
            Fail(failMsg, 0);
        }
    }

    return 0;
}
