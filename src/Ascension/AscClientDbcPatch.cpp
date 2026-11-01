// The client-DBC SMSG_PATCH_* handlers of the original's installer FUN_10228d30 / FUN_10229156 /
// FUN_1022958a (registered through the map-backed registrar 0x102CFEB0). Each patches one of the CLIENT's
// WowClientDB tables: the wire record is read item by item, the row with that id (the getter,
// AscClientDbc::Row) is overwritten in place, or a new row is allocated and inserted (AscClientDbc::Insert).
//
// The table (AscClientDbcPatch.generated.inc, tools/gen_client_patch.py) was recovered by running every
// handler in Unicorn (tools/emulate_patch_handler.py): the wire program and which wire value lands in which
// row dword are measured, not read by eye. Strings are copied into their own allocation (FUN_10ae66d0 +
// strcpy_s) and the row keeps the pointer; an overwritten row's old strings are leaked, as in the original.
// Handlers with client refresh calls or DLL side effects are written out by hand below the table.
#include <Ascension/AscScript.hpp>
#include <Ascension/AscBindings.hpp>
#include <Ascension/AscX64.hpp>
#include <Ascension/AscClientDbc.hpp>
#include <Ascension/AscLog.hpp>
#include <Ascension/RealmInfo.hpp>
#include <Ascension/AscRuntime.hpp>
#include <Client/CDataStore.hpp>
#include <Client/CNetClient.hpp>
#include <Misc/DataContainer.hpp>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>
#include <windows.h>

void AscAppearance_RebuildOutfits();
void AscSpellApi_SkillLineAbilityUpdate(uint32_t spell);
void AscSpellApi_SkillLineAbilityErase(uint32_t spell);

namespace
{
    struct Spec
    {
        uint32_t opcode;
        uint32_t container;
        uint32_t rowSize;
        const char* program;   // b h d q x s z, see the generator
        const char* map;       // per row dword: 1 + value index, or 0
    };
    const Spec kSpecs[] = {
#include <Ascension/AscClientDbcPatch.generated.inc>
        // By hand (the emulator cannot run their std::string handling), from the disassembly:
        // 0x967 ITEM_RANDOM_SUFFIX (FUN_1022ded0): id, 8 skipped bytes, 40 bytes of fields, two sized strings;
        // row = id, name, internal name, the fields.
        // 0x932 (FUN_1022d960): 8 dwords, container of getter FUN_100b1870 -- see On932.
        {0x932, 0xAD3D4C, 0x20, "dddddddd", "\001\002\003\004\005\006\007\010"},
        // 0x935 SPELL_ITEM_ENCHANTMENT (FUN_10232360): 14 dwords, a 16-locale string (slot 0 kept), 4 skipped, 7 dwords.
        {0x935, 0xAD48B0, 0x58, "ddddddddddddddL____ddddddd", "\001\002\003\004\005\006\007\010\011\012\013\014\015\016\017\020\021\022\023\024\025\026"},
        // 0x937 ACHIEVEMENT_CRITERIA (FUN_10229b40): 9 dwords, a 16-locale string (slot 0), 5 dwords.
        {0x937, 0xAD3080, 0x3C, "dddddddddLddddd", "\001\002\003\004\005\006\007\010\011\012\013\014\015\016\017"},
        // 0x938 ACHIEVEMENT_CATEGORY (FUN_10229850): 2 dwords, a 16-locale string (slot 0), 4 skipped, 1 dword.
        {0x938, 0xAD30A4, 0x10, "ddL____d", "\001\002\003\004"},
        {0x967, 0xAD3ED8, 0x34, "d________xxqzz", "\001\014\015\002\003\004\005\006\007\010\011\012\013"},
    };

    std::map<uint32_t, const Spec*>& ByOpcode() { static std::map<uint32_t, const Spec*> m; return m; }

    char* CopyString(const char* s, size_t n)
    {
        char* p = static_cast<char*>(malloc(n + 1));
        memcpy(p, s, n);
        p[n] = 0;
        return p;
    }

    // The wire record as the handler reads it: numeric items split into dwords, strings as pointers
    // (uintptr_t: on x64 the client's string columns are 8-byte char*).
    std::vector<uintptr_t> ReadValues(const char* program, CDataStore* p)
    {
        std::vector<uintptr_t> v;
        uint32_t packed = 0, bytePos = 0;
        auto take = [&](uint32_t n) { uint32_t x = 0; memcpy(&x, p->m_buffer + p->m_read, n); p->m_read += static_cast<int32_t>(n); return x; };
        for (const char* c = program; *c; ++c)
            switch (*c)
            {
            case 'b': v.push_back(take(1)); break;
            case 'h': v.push_back(take(2)); break;
            case 'd': v.push_back(take(4)); break;
            case 'q': v.push_back(take(4)); v.push_back(take(4)); break;
            case 'x': for (int i = 0; i < 4; ++i) v.push_back(take(4)); break;
            case '_': p->m_read += 1; break;   // a byte the handler skips (e.g. an in-row string slot)
            case 'L':                            // a 16-locale string, each u32 length + bytes; slot 0 is kept
            {
                uintptr_t first = 0;
                for (int i = 0; i < 16; ++i)
                {
                    const uint32_t n = take(4);
                    const char* str = reinterpret_cast<const char*>(p->m_buffer + p->m_read);
                    if (i == 0)
                        first = reinterpret_cast<uintptr_t>(CopyString(str, n));
                    p->m_read += static_cast<int32_t>(n);
                }
                v.push_back(first);
                break;
            }
            case 'p':                            // a u8 packed into the current value ('|' closes it)
            {
                if (bytePos == 0)
                    v.push_back(0);
                v.back() |= static_cast<uintptr_t>(take(1)) << (8 * bytePos);
                ++bytePos;
                break;
            }
            case '|': bytePos = 0; break;
            case 'B':                            // a u32 of which only the low byte is kept; four pack into one value
            {
                const uint32_t b = take(4) & 0xFF;
                if (packed == 0)
                    v.push_back(0);
                v.back() |= static_cast<uintptr_t>(b) << (8 * packed);
                packed = (packed + 1) & 3;
                break;
            }
            case 's':
            {
                const char* s = reinterpret_cast<const char*>(p->m_buffer + p->m_read);
                const size_t n = strlen(s);
                p->m_read += static_cast<int32_t>(n + 1);
                v.push_back(reinterpret_cast<uintptr_t>(CopyString(s, n)));
                break;
            }
            case 'z':
            {
                const uint32_t n = take(4);
                const char* s = reinterpret_cast<const char*>(p->m_buffer + p->m_read);
                p->m_read += static_cast<int32_t>(n);
                v.push_back(reinterpret_cast<uintptr_t>(CopyString(s, n)));
                break;
            }
            }
        return v;
    }

    // Returns the row written, in the 12340 layout (x64: AscClientDbc's x86 copy of it, for reading);
    // `inserted` tells a new row from an overwritten one (the hand-written handlers' follow-ups depend on it).
    // x64: each x86 dword goes to its x64 offset in the client's own record (AscClientDbc::PutField); before,
    // the write landed in the read-only shadow and was lost.
    uint8_t* Apply(const Spec& s, CDataStore* p, bool* inserted = nullptr)
    {
        const std::vector<uintptr_t> v = ReadValues(s.program, p);
        const uint32_t dwords = s.rowSize / 4;
        auto value = [&](uint32_t d) -> uintptr_t { const uint8_t k = static_cast<uint8_t>(s.map[d]); return k ? v[k - 1] : 0; };
        const uint32_t id = static_cast<uint32_t>(value(0));
        if (inserted)
            *inserted = false;
        if (uint8_t* rec = AscClientDbc::Writable(s.container, id))
        {
            for (uint32_t d = 0; d < dwords; ++d)
                if (s.map[d])
                    AscClientDbc::PutField(s.container, rec, d * 4, value(d));
            return AscClientDbc::Row(s.container, id);
        }
        if (AscClientDbc::Row(s.container, id))   // x64: row exists but is not writable (no map / RLE)
        {
            AscLog::Printf("x64: SMSG_PATCH 0x%X: container 0x%06X id %u nao gravavel -- ignorado", s.opcode, s.container, id);
            return AscClientDbc::Row(s.container, id);
        }
        uint8_t* rec = AscClientDbc::NewRecord(s.container, s.rowSize);
        if (!rec)
        {
            AscLog::Printf("x64: SMSG_PATCH 0x%X: container 0x%06X sem mapa -- id %u nao inserido", s.opcode, s.container, id);
            return nullptr;
        }
        for (uint32_t d = 0; d < dwords; ++d)
            AscClientDbc::PutField(s.container, rec, d * 4, value(d));
        AscClientDbc::InsertRecord(s.container, rec);
        if (inserted)
            *inserted = true;
        return AscClientDbc::Row(s.container, id);
    }

    // Follow-ups the original runs after some patches (installed over the generic handler below).
    typedef void (*After)(uint8_t* row, bool inserted);
    std::map<uint32_t, After>& Afters() { static std::map<uint32_t, After> m; return m; }

    // Reinicio do som (SI2::ShutDown(1) 0x4C6230, Sleep(200), SI2::Init(1) 0x4C82E0) depois de 0x92D/0x98A/0x98B.
    // DESVIO INTENCIONAL do original (FUN_102333b0 / FUN_10230900 / FUN_10230ac0 reiniciam a cada pacote): no
    // login o servidor manda uma rajada de 0x98B (SoundEntries) e cada um custava ShutDown+200 ms+Init na thread
    // principal -- 35 s sem resposta ao entrar no mundo (bateria 2, 31/10 21:48) e microtravadas no jogo. Aqui
    // o pacote so marca "som sujo"; um unico reinicio roda quando passam kSoundQuietMs sem novo pacote, num
    // timer do cliente (ClientSetTimer 0x403370, thread principal). Nada le o som novo entre os pacotes.
    const uint32_t kSoundQuietMs = 400;
    const uint32_t kSoundPollMs = 100;
    uint32_t g_soundDirtyAt = 0;
    bool g_soundDirty = false;
    bool g_soundTimer = false;

#if ASC_X64
    // ---- 0x98A / 0x98B no x64: atualiza so o SoundKit do id patchado ---------------------------------------------
    // DESVIO do x86 (FUN_10230900 / FUN_10230ac0 so reiniciam o som): o SI2::Init(1) do restart NAO chama o
    // InitSoundKitLookup (0x4CD4A0, so com restart=0), entao o s_soundKitDefs -- o que o PlaySoundKit le -- nunca era
    // refeito e o patch de SoundEntries nao tinha efeito; e o InitSoundKitLookup varre m_records, que nao ve linha
    // nova. Aqui o VSOUNDKITDEF do id e montado como o InitSoundKitLookup faz e trocado em s_soundKitDefs[id]
    // (o antigo vaza: um som tocando pode apontar para ele), sem recriar o FMOD (~0,4 s + device, SESound.log
    // 31/10 22:07:44). Limite: o s_soundKitLookup (nome -> id, PlaySound("nome")) nao recebe nomes novos.
    // So x64: as strings de arquivo vao pelo calloc da ucrtbase.dll (o mesmo CRT do free() do ~VSOUNDKITDEF no
    // cliente); no x86 o CRT estatico do Ascension.exe tem heap proprio, entao la fica o reinicio.
    namespace SoundKits
    {
        const uint32_t kSoundEntries = 0xAD4670;       // SoundEntriesRec (12340: 0x78)
        const uint32_t kSoundEntriesAdv = 0xAD5024;    // SoundEntriesAdvancedRec
        const uint32_t kDefs = 0xB4AD3C;               // TSGrowableArray<VSOUNDKITDEF*> s_soundKitDefs (x64 +0 alloc, +4 count, +8 data)
        // VSOUNDKITDEF x64 (PDB, sizeof 0xF0)
        const uint32_t kDefSize = 0xF0, kName = 0x0, kFiles = 0x8, kMinDist = 0x58, kCutoff = 0x5C, kVolume = 0x60,
                       kId = 0x64, kFlags = 0x68, kNumFiles = 0x6C, kLastIndex = 0x70, kFreqs = 0x74, kPending = 0xD8,
                       kAdvId = 0xE0, kAdv = 0xE8;

        uint8_t* Arr() { return static_cast<uint8_t*>(ASC_PTR(kDefs)); }
        uint32_t& Alloc() { return *reinterpret_cast<uint32_t*>(Arr()); }
        uint32_t& Count() { return *reinterpret_cast<uint32_t*>(Arr() + 4); }
        uint8_t**& Data() { return *reinterpret_cast<uint8_t***>(Arr() + 8); }
        void* SMemAlloc(size_t n) { return reinterpret_cast<void*(__cdecl*)(size_t, const char*, int, uint32_t)>(ASC_PTR(0x76E540))(n, "AscClientDbcPatch", __LINE__, 0); }
        void SMemFree(void* p) { reinterpret_cast<int(__cdecl*)(void*, const char*, int, uint32_t)>(ASC_PTR(0x76E5A0))(p, "AscClientDbcPatch", __LINE__, 0); }
        template <class T> T& At(uint8_t* p, uint32_t off) { return *reinterpret_cast<T*>(p + off); }

        // calloc do mesmo CRT do cliente (o ~VSOUNDKITDEF faz free() nos m_files): o cliente x64 importa a UCRT
        // dinamica (api-ms-win-crt-heap -> ucrtbase.dll). Fallback: o heap do processo, que e o heap da UCRT.
        void* ClientCalloc(size_t n)
        {
            typedef void*(__cdecl* calloc_t)(size_t, size_t);
            static calloc_t fn = []() -> calloc_t {
                HMODULE ucrt = GetModuleHandleW(L"ucrtbase.dll");
                return ucrt ? reinterpret_cast<calloc_t>(GetProcAddress(ucrt, "calloc")) : nullptr;
            }();
            return fn ? fn(1, n) : HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n);
        }

        bool Grow(uint32_t count)   // garante Count() >= count (slots novos nulos)
        {
            if (count <= Count())
                return true;
            if (count > Alloc())
            {
                uint8_t** fresh = static_cast<uint8_t**>(SMemAlloc(sizeof(uint8_t*) * count));
                if (!fresh)
                    return false;
                memset(fresh, 0, sizeof(uint8_t*) * count);
                uint8_t** old = Data();
                if (old)
                    memcpy(fresh, old, sizeof(uint8_t*) * Count());
                Data() = fresh;
                Alloc() = count;
                if (old)
                    SMemFree(old);   // inicio de bloco do ReallocData (SMemReAlloc), nao ponteiro interno
            }
            else
                memset(Data() + Count(), 0, sizeof(uint8_t*) * (count - Count()));
            Count() = count;
            return true;
        }

        void RefreshEntry(uint32_t id)
        {
            if (!Count())
                return;   // SI2 ainda nao montou a tabela (InitSoundKitLookup le o DB depois)
            const uint8_t* rec = AscClientDbc::Row(kSoundEntries, id);   // linha 12340 (strings pelo Str)
            if (!rec || !Grow(id + 1))
                return;
            uint8_t* def = static_cast<uint8_t*>(SMemAlloc(kDefSize));
            if (!def)
                return;
            memset(def, 0, kDefSize);   // VSOUNDKITDEF(): arquivos/pesos nulos, numFiles/advancedId 0
            At<int32_t>(def, kLastIndex) = -1;
            At<int32_t>(def, kPending) = -1;
            const char* dir = AscClientDbc::Str(rec, 0x5C);
            const size_t dirLen = dir ? strlen(dir) : 0;
            for (uint32_t j = 0; j < 10; ++j)
            {
                const char* file = AscClientDbc::Str(rec, 0xC + j * 4);
                if (!file || !*file)
                    break;
                const size_t len = strlen(file) + 1 + (dirLen ? dirLen + 1 : 0);
                char* path = static_cast<char*>(ClientCalloc(len));
                if (!path)
                    break;
                if (dirLen)
                {
                    strcpy_s(path, len, dir);
                    if (dir[dirLen - 1] != '\\')
                        strcat_s(path, len, "\\");
                }
                strcat_s(path, len, file);
                At<char*>(def, kFiles + j * 8) = path;
                At<int32_t>(def, kNumFiles)++;
                At<int32_t>(def, kFreqs + j * 4) = *reinterpret_cast<const int32_t*>(rec + 0x34 + j * 4);
            }
            At<uint32_t>(def, kFlags) = *reinterpret_cast<const uint32_t*>(rec + 0x64);
            At<const char*>(def, kName) = AscClientDbc::Str(rec, 0x8);
            At<float>(def, kMinDist) = *reinterpret_cast<const float*>(rec + 0x68);
            At<float>(def, kCutoff) = *reinterpret_cast<const float*>(rec + 0x6C);
            At<float>(def, kVolume) = *reinterpret_cast<const float*>(rec + 0x60);
            At<int32_t>(def, kId) = static_cast<int32_t>(id);
            const uint32_t adv = *reinterpret_cast<const uint32_t*>(rec + 0x74);
            At<int32_t>(def, kAdvId) = static_cast<int32_t>(adv);
            At<const void*>(def, kAdv) = adv ? AscClientDbc::Native(kSoundEntriesAdv, adv) : nullptr;
            Data()[id] = def;   // o def antigo fica (vazamento pequeno, por id patchado)
        }

        void RefreshAdvanced(uint32_t advId)   // SoundEntriesAdvanced: repoe o ponteiro m_advanced de quem usa o id
        {
            const void* rec = AscClientDbc::Native(kSoundEntriesAdv, advId);
            for (uint32_t i = 0; i < Count(); ++i)
                if (uint8_t* def = Data()[i])
                    if (At<int32_t>(def, kAdvId) == static_cast<int32_t>(advId))
                        At<const void*>(def, kAdv) = rec;
        }
    }
#endif

    int __cdecl SoundRestartTick(void*)
    {
        if (g_soundDirty && GetTickCount() - g_soundDirtyAt < kSoundQuietMs)
        {
            AscRuntime::Schedule(kSoundPollMs, &SoundRestartTick, nullptr);   // ainda chegando pacote
            return 0;
        }
        g_soundTimer = false;
        if (!g_soundDirty)
            return 0;
        g_soundDirty = false;
        reinterpret_cast<void(__cdecl*)(int)>(ASC_PTR(0x4C6230))(1);
        Sleep(200);
        reinterpret_cast<void(__cdecl*)(int)>(ASC_PTR(0x4C82E0))(1);
        AscLog::Printf("ClientDbcPatch: som reiniciado (uma vez, apos a rajada de patches de som)");
        return 0;
    }

    void MarkSoundDirty()
    {
        g_soundDirtyAt = GetTickCount();
        g_soundDirty = true;
        if (!g_soundTimer)
        {
            g_soundTimer = true;
            AscRuntime::Schedule(kSoundPollMs, &SoundRestartTick, nullptr);
        }
    }

    int __cdecl OnPatch(void*, uint32_t opcode, uint32_t, CDataStore* p)
    {
        auto it = ByOpcode().find(opcode);
        if (it == ByOpcode().end())
            return 1;
        bool inserted = false;
        uint8_t* row = Apply(*it->second, p, &inserted);
        auto after = Afters().find(opcode);
        if (row && after != Afters().end())
            after->second(row, inserted);
        return 1;
    }

    // FUN_1008e3d0(player, unused): every inventory slot's item (0..0x26) is looked up (0x4D4DB0, type 2; the
    // results are not used), then for each equipped bag (GUIDs at 0xC23540) that exists, the bag's container
    // (vtable +0x24) gets 0x754390(i) for each of its slots (descriptor +0x100).
    void RefreshInventory(uint8_t* player)
    {
        typedef void*(__cdecl* ByGuid_t)(uint32_t, uint32_t, uint32_t);
        const ByGuid_t ByGuid = [](uint32_t lo, uint32_t hi, uint32_t m) -> void* { return static_cast<void*>(AscScript::ObjectPtr(AscX64::Guid(lo, hi), m)); };   // 0x4D4DB0 (x64: WOWGUID + arquivo/linha)
        for (uint32_t i = 0; i < 0x27; ++i)
        {
            const uint8_t* desc = *reinterpret_cast<uint8_t* const*>(player + ASC_OFF(8, 0x10));
            ByGuid(*reinterpret_cast<const uint32_t*>(desc + 0x510 + i * 8), *reinterpret_cast<const uint32_t*>(desc + 0x514 + i * 8), 2);
        }
        for (uint8_t slot = 0x13; slot < 0x17; ++slot)
        {
            const uint32_t* guid = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(ASC_PTR(0xC23540)) + (slot - 0x13) * 8);   // WOWGUID m_containers[]   // DAT_10BC91E0 holds 0xC23540
            uint8_t* bag = static_cast<uint8_t*>(ByGuid(guid[0], guid[1], 4));
            if (!bag)
                continue;
            void* container = (*reinterpret_cast<void*(__thiscall**)(void*)>(*reinterpret_cast<uint8_t**>(bag) + ASC_OFF(0x24, 0x48)))(bag);   // vtable slot 9
            if (!container)
                continue;
            for (uint32_t i = 0; i < *reinterpret_cast<const uint32_t*>(*reinterpret_cast<uint8_t**>(bag + ASC_OFF(8, 0x10)) + 0x100); ++i)
                reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(ASC_PTR(0x754390))(container, i);
        }
    }

    // 0x932 (FUN_1022d960): 8 dwords; id 0 is refused (returns 0, nothing stored); an OVERWRITTEN row is
    // followed by RefreshInventory(player).
    int __cdecl On932(void*, uint32_t opcode, uint32_t, CDataStore* p)
    {
        uint32_t id;
        memcpy(&id, p->m_buffer + p->m_read, 4);
        if (id == 0)
        {
            p->m_read += 0x20;
            return 0;
        }
        bool inserted = false;
        const uint8_t* row = Apply(*ByOpcode()[opcode], p, &inserted);
        if (row && !inserted)
            if (uint8_t* player = reinterpret_cast<uint8_t*(__cdecl*)()>(ASC_PTR(0x4038F0))())
                RefreshInventory(player);
        return 1;
    }

    uint8_t g_spellbookDirty = 0;   // DAT_10BE24E4: only ever written (0x934 / 0x92A)

    // 0x934 SKILL_LINE_ABILITY (FUN_10230100). Overwrite: the OLD spell leaves the spell -> row map
    // (FUN_103266c0) before the row is overwritten. Both paths then map the new spell (FUN_10328a30),
    // set DAT_10BE24E4 while in world (0xBD0792) and the client byte 0xD3F60C; an overwrite also runs
    // 0x53B4E0(spell, 0) and, unless that returned -1, 0x53FAD0(spell, 0) and 0x542030(spell, 1, 0).
    int __cdecl On934(void*, uint32_t opcode, uint32_t, CDataStore* p)
    {
        const Spec& s = *ByOpcode()[opcode];
        uint32_t id;
        memcpy(&id, p->m_buffer + p->m_read, 4);
        const uint8_t* old = AscClientDbc::Row(s.container, id);
        if (old)
            AscSpellApi_SkillLineAbilityErase(*reinterpret_cast<const uint32_t*>(old + 8));
        bool inserted = false;
        const uint8_t* row = Apply(s, p, &inserted);
        if (!row)
            return 1;
        const uint32_t spell = *reinterpret_cast<const uint32_t*>(row + 8);
        AscSpellApi_SkillLineAbilityUpdate(spell);
        if (*reinterpret_cast<const uint8_t*>(ASC_PTR(0xBD0792)))
            g_spellbookDirty = 1;
        *reinterpret_cast<uint8_t*>(ASC_PTR(0xD3F60C)) = 1;
        if (inserted)
            return 1;
        if (reinterpret_cast<int(__cdecl*)(uint32_t, int)>(ASC_PTR(0x53B4E0))(spell, 0) != -1)
        {
            reinterpret_cast<void(__cdecl*)(uint32_t, int)>(ASC_PTR(0x53FAD0))(spell, 0);
            reinterpret_cast<void(__cdecl*)(uint32_t, int, int)>(ASC_PTR(0x542030))(spell, 1, 0);
        }
        return 1;
    }

    uint8_t g_spellDirty2 = 0;   // DAT_10BE24E5: only ever written (0x92A)

    // 0x92A SPELL (FUN_10231de0): the 0x2A8-byte Spell.dbc image, then four sized strings stored at +0x220,
    // +0x228, +0x224, +0x22C (in that order). Spell modifiers the client cannot hold -- an effect applying
    // aura 0x6B / 0x6C (add flat / pct modifier) with misc value > 30 -- become aura 4 (dummy). The row is
    // copied through the client's 0x95D980 (src, size, dst) while its spell-cache flag 0xC5DEA0 is set, else
    // plainly. An overwrite in world (0xBD0792) sets DAT_10BE24E4 and DAT_10BE24E5; a new row is inserted
    // with the generic inserter (FUN_100ad860).
    int __cdecl On92A(void*, uint32_t, uint32_t, CDataStore* p)
    {
        uint8_t tmp[0x2A8];
        memcpy(tmp, p->m_buffer + p->m_read, sizeof tmp);
        p->m_read += sizeof tmp;
        static const uint32_t kStringField[4] = {0x220, 0x228, 0x224, 0x22C};
        const std::vector<uintptr_t> strings = ReadValues("zzzz", p);
#if !ASC_X64
        for (int i = 0; i < 4; ++i)
            memcpy(tmp + kStringField[i], &strings[i], 4);
#endif
        for (int i = 0; i < 3; ++i)
        {
            int32_t& aura = *reinterpret_cast<int32_t*>(tmp + 0x17C + i * 4);
            const int32_t misc = *reinterpret_cast<const int32_t*>(tmp + 0x1B8 + i * 4);
            if ((aura == 0x6B || aura == 0x6C) && misc > 0x1E)
                aura = 4;
        }
        const bool compressed = *reinterpret_cast<const uint8_t*>(ASC_PTR(0xC5DEA0)) != 0;
        typedef void(__cdecl* Pack_t)(const void*, uint32_t, void*);   // CompressRow(src, size, dst)
        const uint32_t id = *reinterpret_cast<const uint32_t*>(tmp);
#if ASC_X64
        // x64: the image becomes a SpellRec in the client's layout (strings as char*), then the same two paths.
        const uint32_t size = AscClientDbc::RecordSize(0xAD49D0, sizeof tmp);
        std::vector<uint8_t> rec64(size, 0);
        {
            uint8_t* fresh = AscClientDbc::NewRecord(0xAD49D0, sizeof tmp);
            if (!fresh)
            {
                AscLog::Printf("x64: SMSG_PATCH 0x92A: Spell sem mapa -- spell %u ignorado", id);
                return 1;
            }
            for (uint32_t off = 0; off < sizeof tmp; off += 4)
            {
                uint32_t v32;
                memcpy(&v32, tmp + off, 4);
                uintptr_t v = v32;
                for (int i = 0; i < 4; ++i)
                    if (off == kStringField[i])
                        v = strings[i];
                AscClientDbc::PutField(0xAD49D0, fresh, off, v);
            }
            memcpy(rec64.data(), fresh, size);
            operator delete(fresh);
        }
        if (uint8_t* raw = AscClientDbc::Raw(0xAD49D0, id))
        {
            if (compressed)
                reinterpret_cast<Pack_t>(ASC_PTR(0x95D980))(rec64.data(), size, raw);
            else
                memcpy(raw, rec64.data(), size);
            if (*reinterpret_cast<const uint8_t*>(ASC_PTR(0xBD0792)))
            {
                g_spellbookDirty = 1;
                g_spellDirty2 = 1;
            }
            return 1;
        }
        uint8_t* row = static_cast<uint8_t*>(operator new(size));
        memcpy(row, rec64.data(), size);
        if (compressed)
            reinterpret_cast<Pack_t>(ASC_PTR(0x95D980))(rec64.data(), size, row);
        AscClientDbc::InsertRecord(0xAD49D0, row);
        return 1;
#else
        if (uint8_t* row = AscClientDbc::Row(0xAD49D0, id))
        {
            if (compressed)
                reinterpret_cast<Pack_t>(ASC_PTR(0x95D980))(tmp, sizeof tmp, row);
            else
                memcpy(row, tmp, sizeof tmp);
            if (*reinterpret_cast<const uint8_t*>(ASC_PTR(0xBD0792)))
            {
                g_spellbookDirty = 1;
                g_spellDirty2 = 1;
            }
            return 1;
        }
        uint8_t* row = static_cast<uint8_t*>(operator new(sizeof tmp));
        memcpy(row, tmp, sizeof tmp);
        if (compressed)
            reinterpret_cast<Pack_t>(ASC_PTR(0x95D980))(tmp, sizeof tmp, row);
        AscClientDbc::Insert(0xAD49D0, row);
        return 1;
#endif
    }

    // 0x95A (FUN_10233540): the record (id, 8 skipped bytes, two sized strings) is read and allocated, but
    // its getter (FUN_100b2710) always misses and its inserter (FUN_1008d890) is an empty stub -- a disabled
    // table. Nothing is stored (the original leaks the row).
    int __cdecl OnDisabled95A(void*, uint32_t, uint32_t, CDataStore* p)
    {
        const std::vector<uintptr_t> v = ReadValues("d________zz", p);
        (void)v;   // the strings leak as in the original
        return 1;
    }

    void Init()
    {
        sDC.AddPacketHandler(0x95A, CNetClientCustomPacket((void*)&OnDisabled95A, nullptr));
        sDC.AddPacketHandler(0x92A, CNetClientCustomPacket((void*)&On92A, nullptr));
        // 0x966 ITEM_SET (FUN_1022e0b0): a NEW row also runs the appearance manager's FUN_100c6750.
        Afters()[0x966] = [](uint8_t*, bool inserted) { if (inserted) AscAppearance_RebuildOutfits(); };
        // 0x930 / 0x931 (FUN_10232830 / FUN_10232920), either path: the client's 0x758CD0() and 0x758C50(0).
        for (uint32_t op : {0x930u, 0x931u})
            Afters()[op] = [](uint8_t*, bool)
            {
                reinterpret_cast<void(__cdecl*)()>(ASC_PTR(0x758CD0))();
                reinterpret_cast<void(__cdecl*)()>(ASC_PTR(0x758C50))();   // CSimpleScriptManager::Create()
            };
        // 0x92D (FUN_102333b0): an OVERWRITTEN row, unless flag 0x10BE3740 (FUN_102cfea0; nothing in plain code
        // sets it) -> 0x4C6230(1), Sleep(200), 0x4C82E0(1).
        Afters()[0x92D] = [](uint8_t*, bool inserted)
        {
            if (inserted)
                return;
            MarkSoundDirty();   // reinicio adiado e agrupado (ver MarkSoundDirty)
        };
        // 0x92E (FUN_10233480), either path: on a dev realm (RealmInfo +0x44, FUN_102fc640) the client's 0x7FBE90().
        Afters()[0x92E] = [](uint8_t*, bool)
        {
            if (RealmInfoSvc::Get().gates[4])
                reinterpret_cast<void(__cdecl*)()>(ASC_PTR(0x7FBE90))();
        };
        // 0x936 ACHIEVEMENT (FUN_10229fb0): an OVERWRITTEN row -> the client's 0x5B4F00() and 0x5B6DF0(), then
        // FUN_1020ae50 rebuilds the DLL map 0x10BE05F8 (row +0x18 -> rows). Nothing in the plain code reads
        // that map, so its rebuild is not reproduced.
        Afters()[0x936] = [](uint8_t*, bool inserted)
        {
            if (inserted)
                return;
            reinterpret_cast<void(__cdecl*)()>(ASC_PTR(0x5B4F00))();
            reinterpret_cast<void(__cdecl*)()>(ASC_PTR(0x5B6DF0))();
        };
        // 0x937 / 0x938: an OVERWRITTEN row -> the client's 0x5B4F00() and 0x5B6DF0(), as 0x936.
        for (uint32_t op : {0x937u, 0x938u})
            Afters()[op] = Afters()[0x936];
        // 0x98A / 0x98B (FUN_10230900 / FUN_10230ac0), either path: 0x4C6230(1), Sleep(200), 0x4C82E0(1).
#if ASC_X64
        // x64: atualiza so o SoundKit do id (SoundKits acima), sem reiniciar o som (desvio intencional).
        Afters()[0x98B] = [](uint8_t* row, bool) { SoundKits::RefreshEntry(*reinterpret_cast<const uint32_t*>(row)); };
        Afters()[0x98A] = [](uint8_t* row, bool) { SoundKits::RefreshAdvanced(*reinterpret_cast<const uint32_t*>(row)); };
#else
        // x86: reinicio agrupado pelo MarkSoundDirty (desvio intencional).
        for (uint32_t op : {0x98Au, 0x98Bu})
            Afters()[op] = [](uint8_t*, bool) { MarkSoundDirty(); };   // reinicio adiado e agrupado
#endif
        for (const Spec& s : kSpecs)
        {
            ByOpcode()[s.opcode] = &s;
            sDC.AddPacketHandler(s.opcode, CNetClientCustomPacket(s.opcode == 0x932 ? (void*)&On932 : s.opcode == 0x934 ? (void*)&On934 : (void*)&OnPatch, nullptr));
        }
    }
    AscBindings::Module s_module(nullptr, 0, &Init);
}
