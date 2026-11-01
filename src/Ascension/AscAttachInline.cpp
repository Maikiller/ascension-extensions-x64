// The client byte patches the original writes unconditionally at attach outside FUN_10a3d840's generated table and
// FUN_10a60d20's (AscAttachLate): the attach init's own inline blocks, FUN_1021c2b0, a block of FUN_10a3d840 and the
// bankEquipmentManager installer FUN_10227970. Found missing by the live exactness audit (step 4, 2026-09-27); the bytes
// are the original's, see AscAttachInline.generated.inc.
#include <Ascension/AscX64.hpp>
#include <Ascension/AscLog.hpp>
#include <Ascension/AscBindings.hpp>
#include <Windows.h>
#include <cstdint>
#include <cstring>

namespace
{
    struct BytePatch
    {
        uint32_t address;
        uint32_t size;
        uint8_t bytes[64];
    };
    const BytePatch kPatches[] = {
#include <Ascension/AscAttachInline.generated.inc>
    };

    void Init()
    {
        for (const BytePatch& p : kPatches)
        {
#if ASC_X64
            // x64: o resto da tabela ja esta no AscAttachPatches (kPatchesX64 / CoveredX64); estes 3 viraram
            // variaveis no fonte do cliente (out/patches-cliente/18-attach-inline.diff).
            if (p.address == 0x6DB8F9 || p.address == 0x67A18E)   // g_coaGuildBankSwapIgnoreRank / g_coaKeepItemCacheOnIdChange
            {
                const int32_t one = 1;
                AscX64::WriteMem(p.address, &one, sizeof(one), "AscAttachInline");
            }
            else if (p.address == 0xAA2D0E)   // g_coaRibbonMinEdgeLife (0xAA2D0C): 0.25 -> 0.0
            {
                const float zero = 0.0f;
                AscX64::WriteMem(0xAA2D0C, &zero, sizeof(zero), "AscAttachInline");
            }
            continue;
#endif
            DWORD old;
            if (!VirtualProtect(reinterpret_cast<void*>(p.address), p.size, PAGE_EXECUTE_READWRITE, &old))
                continue;
            memcpy(reinterpret_cast<void*>(p.address), p.bytes, p.size);
            VirtualProtect(reinterpret_cast<void*>(p.address), p.size, old, &old);
            FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(p.address), p.size);
        }
    }
    AscBindings::Module s_module(nullptr, 0, &Init);
}
