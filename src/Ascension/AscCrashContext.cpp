// "Ascension Debug": crash-report context (module init FUN_1019dc80, 23 hooks kept in the list 0x10BDED44).
//
// Recorders -- each keeps the last value its client function saw, then calls the original:
//   0x4D7750 -> FUN_1019daf0  load map: "Last Loaded Map ID"; a map Map.dbc does not have is a fatal assert
//   0x57ABC0 -> FUN_101a0500  spell tooltip: "Last Spell Tooltip ID"
//   0x812410 / 0x810ED0 -> FUN_101a05e0 / FUN_101a0660  spell table lookups: "Last Spell Table Lookup ID"
//   0x65C290 -> sub_1019dad0  ClientDB::GetRow: the table and the id
//   0x6AFFD0 -> sub_1019d310  "Last BLP"
//   0x717A20 -> sub_1019d520  unit model data: the unit's entry
//   0x7B31E0 -> FUN_1019d360  detail doodads: "Last DetailDoodad IDX"; an index past GroundEffectDoodad.dbc
//                             is a fatal assert naming the ADT chunk, and the original is NOT called
//   0x7BD480 -> sub_1019d8f0  "Last Loaded Map Object Name"
//   0x7F6A10 -> FUN_101a1290  "Last LoadGameTable"
//   0x819210 -> sub_1019dc40  "Last FrameScript_Execute"
//   M2 model calls, recording the model's file name (model +0x2C -> +0x3C): 0x81F700 scene render
//     (FUN_1019d870, via +0x60), 0x824FC0 is drawable (FUN_1019d730; from 0x821C77 also "Last IsDrawable"),
//     0x82CED0 sequence info, 0x82EC30 collision facets, 0x82F0F0 animate MT, 0x8309C0 animate particle,
//     0x830DC0 animate, 0x832260 process callbacks, 0x838490 skin (+0x3C of the skin itself),
//     0x823ED0 wait for load (FUN_1019d830)
//   0x823F10 -> 0x1019D810    skipped when the model has no shared data (+0x2C)
// Writer:
//   0x682200 -> FUN_1019e5f0  after the client's crash text, the "Ascension Debug" block and the Lua stack
#include <Ascension/AscClientDbc.hpp>
#include <Ascension/AscCrashContext.hpp>
#include <Ascension/AscX64.hpp>
#include <Ascension/AscBindings.hpp>
#include <Ascension/AscLua.hpp>
#include <Ascension/AscProfiler.hpp>
#include <Ascension/AscRealmData.hpp>
#include <Ascension/AscRuntime.hpp>
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <sstream>
#include <string>
#include <windows.h>
#include <psapi.h>

namespace AscCrashContext
{
    std::string g_signalEvent, g_m2Cache, g_m2Shared, g_m2SetVertices, g_m2ShadowMap, g_asyncTexture, g_textureCreate;
    uint32_t g_gameObjectEntry = 0;
    std::string g_gameObjectModel;
}

namespace
{
    using namespace AscCrashContext;

    std::string g_assertInfo;         // 0x10BCA210
    std::string g_mapId;              // 0x10BCA1C8
    std::string g_spellTooltip;       // 0x10BCA198
    std::string g_spellLookup;        // 0x10BCA384
    const void* g_clientDb = nullptr; // 0x10BDED2C
    int32_t g_clientDbId = 0;         // 0x10BDED30
    std::string g_blp;                // 0x10BCA3FC
    uint32_t g_modelDataEntry = 0;    // 0x10BDED50
    std::string g_detailDoodad;       // 0x10BCA3D8
    std::string g_mapObject;          // 0x10BCA1B0
    std::string g_gameTable;          // 0x10BCA450
    std::string g_frameScript;        // 0x10BCA324
    std::string g_m2SceneRender;      // 0x10BCA360
    std::string g_m2IsDrawable;       // 0x10BCA288
    std::string g_m2IsDrawableFrom;   // 0x10BCA2A0 (callers at 0x821C77)
    std::string g_m2SequenceInfo;     // 0x10BCA270
    std::string g_m2CollisionFacets;  // 0x10BCA258
    std::string g_m2AnimateMT;        // 0x10BCA150
    std::string g_m2AnimateParticle;  // 0x10BCA1F8
    std::string g_m2Animate;          // 0x10BCA138
    std::string g_m2ProcessCallbacks; // 0x10BCA168
    std::string g_m2Skin;             // 0x10BCA180
    std::string g_m2WaitForLoad;      // 0x10BCA228

    // CM2Model::m_shared x86 +0x2C / x64 +0x48 -> CM2Shared::m_fileName x86 +0x3C / x64 +0x74; "" without one
    const char* ModelName(const void* model)
    {
        const uint8_t* shared = *reinterpret_cast<uint8_t* const*>(static_cast<const uint8_t*>(model) + ASC_OFF(0x2C, 0x48));
        return shared ? reinterpret_cast<const char*>(shared + ASC_OFF(0x3C, 0x74)) : "";
    }

    // true se `ret` cai dentro da funcao do exe `fn12340`. x86: so o endereco exato de retorno (0x821C77).
    // x64: o RVA do retorno muda a cada build do cliente; a tabela de unwind (.pdata) da o inicio da funcao.
    bool ReturnsInto(void* ret, uint32_t ret12340, uint32_t fn12340)
    {
#if ASC_X64
        (void)ret12340;
        DWORD64 base = 0;
        const RUNTIME_FUNCTION* rf = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(ret), &base, nullptr);
        return rf && reinterpret_cast<void*>(base + rf->BeginAddress) == ASC_PTR(fn12340);
#else
        (void)fn12340;
        return reinterpret_cast<uintptr_t>(ret) == ret12340;
#endif
    }

    // ---- recorders -----------------------------------------------------------------------------------------
    typedef int(__cdecl* Fn4D7750_t)(uint32_t);
    Fn4D7750_t g_4D7750 = nullptr;
    int __cdecl LoadMap(uint32_t map)
    {
        g_mapId = std::to_string(map);
        const bool exists = AscClientDbc::Row(0xAD4160, map) != nullptr;   // FUN_100b1c60, Map.dbc
        if (!exists)
            Assert(("MapID = " + std::to_string(map) + " does not exist in Map.dbc").c_str(), nullptr, 0);
        return g_4D7750(map);
    }

    typedef int(__cdecl* Fn57ABC0_t)(const uint32_t*, uintptr_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    Fn57ABC0_t g_57ABC0 = nullptr;
    int __cdecl SpellTooltip(const uint32_t* spell, uintptr_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9)
    {
        g_spellTooltip = std::to_string(*spell);
        return g_57ABC0(spell, a2, a3, a4, a5, a6, a7, a8, a9);
    }

    // SpellTableLookupAbility / SpellTableLookupInfo devolvem ponteiro de registro (64 bits no x64)
    typedef void*(__cdecl* Fn3_t)(uint32_t, uint32_t, uint32_t);
    Fn3_t g_812410 = nullptr, g_810ED0 = nullptr;
    void* __cdecl SpellLookupA(uint32_t a1, uint32_t a2, uint32_t spell)   // FUN_101a05e0
    {
        g_spellLookup = std::to_string(spell);
        return g_812410(a1, a2, spell);
    }
    void* __cdecl SpellLookupB(uint32_t a1, uint32_t a2, uint32_t spell)   // FUN_101a0660
    {
        g_spellLookup = std::to_string(spell);
        return g_810ED0(a1, a2, spell);
    }

    typedef void*(__fastcall* Fn65C290_t)(void* ASC_EDX_P(void*), int32_t);
    Fn65C290_t g_65C290 = nullptr;
    void* __fastcall ClientDbGetRow(void* db ASC_EDX_P(void* edx), int32_t id)
    {
        g_clientDbId = id;
        g_clientDb = db;
        return g_65C290(db ASC_EDX_A(edx), id);
    }

    typedef int(__fastcall* Fn6AFFD0_t)(void* ASC_EDX_P(void*), const char*, uint32_t, uintptr_t, uint32_t, uint32_t);
    Fn6AFFD0_t g_6AFFD0 = nullptr;
    int __fastcall LoadBlp(void* self ASC_EDX_P(void* edx), const char* name, uint32_t a2, uintptr_t a3, uint32_t a4, uint32_t a5)
    {
        g_blp = name;
        return g_6AFFD0(self ASC_EDX_A(edx), name, a2, a3, a4, a5);
    }

    typedef int(__fastcall* Fn0_t)(void* ASC_EDX_P(void*));
    typedef void*(__fastcall* Fn0p_t)(void* ASC_EDX_P(void*));   // CGUnit_C::GetModelData -> CreatureModelDataRec*
    Fn0p_t g_717A20 = nullptr;
    void* __fastcall UnitModelData(void* unit ASC_EDX_P(void* edx))
    {
        g_modelDataEntry = *reinterpret_cast<const uint32_t*>(*reinterpret_cast<uint8_t* const*>(static_cast<uint8_t*>(unit) + ASC_OFF(8, 0x10)) + 0xC);
        return g_717A20(unit ASC_EDX_A(edx));
    }

    // AddDoodad(id, const C3Vector& pos, float rotation, float scale, const C3Vector& normal, uint16 tri, const CImVector& color)
    typedef int(__fastcall* Fn7B31E0_t)(void* ASC_EDX_P(void*), uint32_t, uintptr_t, float, float, uintptr_t, uint32_t, uintptr_t);
    Fn7B31E0_t g_7B31E0 = nullptr;
    int __fastcall DetailDoodad(void* self ASC_EDX_P(void* edx), uint32_t index, uintptr_t a2, float a3, float a4, uintptr_t a5, uint32_t a6, uintptr_t a7)
    {
        if (index < *reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(ASC_PTR(0xD1C4F4)) + 4))   // DAT_10BCA11C -> 0xD1C4F4 (+4 the count)
        {
            g_detailDoodad = std::to_string(index);
            return g_7B31E0(self ASC_EDX_A(edx), index, a2, a3, a4, a5, a6, a7);
        }
        const uint8_t* chunk = *reinterpret_cast<uint8_t* const*>(static_cast<uint8_t*>(self) + ASC_OFF(0x98, 0xF0));   // m_chunk
        // CMapBaseObj::m_parentLinkList.m_terminator.m_next (x86 +0x20 / x64 +0x38) -> CMapBaseObjLink::m_ref (+8 / +0x10)
        const uint8_t* adt = *reinterpret_cast<uint8_t* const*>(*reinterpret_cast<uint8_t* const*>(chunk + ASC_OFF(0x20, 0x38)) + ASC_OFF(8, 0x10));
        char text[256];
        snprintf(text, sizeof(text), "ADT: [%d,%d] Chunk: [%d,%d] has incorrect GroundEffectDoodad.dbc ID in MCLY.effectId: %u",
                 *reinterpret_cast<const int32_t*>(adt + ASC_OFF(0x48, 0x64)), *reinterpret_cast<const int32_t*>(adt + ASC_OFF(0x4C, 0x68)),   // CMapArea::m_index
                 *reinterpret_cast<const int32_t*>(chunk + ASC_OFF(0x24, 0x40)), *reinterpret_cast<const int32_t*>(chunk + ASC_OFF(0x28, 0x44)), index);   // CMapChunk::m_aIndex
        Assert(text, nullptr, 0);
        return 0;
    }

    typedef int(__cdecl* Fn2_t)(const char*, uintptr_t);
    Fn2_t g_7BD480 = nullptr;
    int __cdecl LoadMapObject(const char* name, uintptr_t a2)
    {
        g_mapObject = name;
        return g_7BD480(name, a2);
    }

    Fn0_t g_7F6A10 = nullptr;
    int __fastcall LoadGameTable(void* self ASC_EDX_P(void* edx))
    {
        g_gameTable = *reinterpret_cast<const char* const*>(static_cast<uint8_t*>(self) + ASC_OFF(4, 8));
        return g_7F6A10(self ASC_EDX_A(edx));
    }

    typedef int(__cdecl* Fn819210_t)(const char*, uintptr_t, uintptr_t);
    Fn819210_t g_819210 = nullptr;
    int __cdecl FrameScriptExecute(const char* script, uintptr_t a2, uintptr_t a3)
    {
        g_frameScript = script ? script : "";
        return g_819210(script, a2, a3);
    }

    typedef int(__fastcall* Fn1_t)(void* ASC_EDX_P(void*), uintptr_t);
    typedef int(__fastcall* Fn2m_t)(void* ASC_EDX_P(void*), uint32_t, uint32_t);
    typedef int(__fastcall* FnFI_t)(void* ASC_EDX_P(void*), float, uint32_t);   // AnimateParticleST(float, unsigned)
    typedef int(__fastcall* Fn3m_t)(void* ASC_EDX_P(void*), uintptr_t, uintptr_t, uintptr_t);
    typedef int(__fastcall* Fn5m_t)(void* ASC_EDX_P(void*), uintptr_t, uintptr_t, uintptr_t, float, float);   // AnimateMT(view*, diffuse&, emissive&, alpha, alpha2)
    Fn1_t g_81F700 = nullptr, g_838490 = nullptr, g_823ED0 = nullptr, g_823F10 = nullptr;
    Fn2m_t g_824FC0 = nullptr;
    FnFI_t g_8309C0 = nullptr;
    Fn3m_t g_82CED0 = nullptr, g_82EC30 = nullptr;
    Fn5m_t g_82F0F0 = nullptr;
    Fn0_t g_830DC0 = nullptr, g_832260 = nullptr;

    int __fastcall M2SceneRender(void* self ASC_EDX_P(void* edx), uint32_t a)
    {
        const uint8_t* model = *reinterpret_cast<uint8_t* const*>(static_cast<uint8_t*>(self) + ASC_OFF(0x60, 0x78));
        g_m2SceneRender = reinterpret_cast<const char*>(*reinterpret_cast<uint8_t* const*>(model + ASC_OFF(0x2C, 0x48)) + ASC_OFF(0x3C, 0x74));
        return g_81F700(self ASC_EDX_A(edx), a);
    }
    int __fastcall M2IsDrawable(void* self ASC_EDX_P(void* edx), uint32_t a, uint32_t b)
    {
        g_m2IsDrawable = ModelName(self);
        if (ReturnsInto(_ReturnAddress(), 0x821C77, 0x821A20))   // CM2Scene::Animate
            g_m2IsDrawableFrom = ModelName(self);
        return g_824FC0(self ASC_EDX_A(edx), a, b);
    }
    int __fastcall M2SequenceInfo(void* self ASC_EDX_P(void* edx), uint32_t a, uint32_t b, uintptr_t c)
    {
        g_m2SequenceInfo = ModelName(self);
        return g_82CED0(self ASC_EDX_A(edx), a, b, c);
    }
    int __fastcall M2CollisionFacets(void* self ASC_EDX_P(void* edx), uintptr_t a, uintptr_t b, uintptr_t c)
    {
        g_m2CollisionFacets = ModelName(self);
        return g_82EC30(self ASC_EDX_A(edx), a, b, c);
    }
    int __fastcall M2AnimateMT(void* self ASC_EDX_P(void* edx), uintptr_t a, uintptr_t b, uintptr_t c, float d, float e)
    {
        g_m2AnimateMT = ModelName(self);
        return g_82F0F0(self ASC_EDX_A(edx), a, b, c, d, e);
    }
    int __fastcall M2AnimateParticle(void* self ASC_EDX_P(void* edx), float a, uint32_t b)
    {
        g_m2AnimateParticle = ModelName(self);
        return g_8309C0(self ASC_EDX_A(edx), a, b);
    }
    int __fastcall M2Animate(void* self ASC_EDX_P(void* edx))
    {
        g_m2Animate = ModelName(self);
        return g_830DC0(self ASC_EDX_A(edx));
    }
    int __fastcall M2ProcessCallbacks(void* self ASC_EDX_P(void* edx))
    {
        g_m2ProcessCallbacks = ModelName(self);
        return g_832260(self ASC_EDX_A(edx));
    }
    int __fastcall M2Skin(void* self ASC_EDX_P(void* edx), uint32_t a)
    {
        g_m2Skin = reinterpret_cast<const char*>(static_cast<uint8_t*>(self) + ASC_OFF(0x3C, 0x74));
        return g_838490(self ASC_EDX_A(edx), a);
    }
    int __fastcall M2WaitForLoad(void* self ASC_EDX_P(void* edx), uintptr_t a)   // FUN_1019d830: no null check
    {
        g_m2WaitForLoad = reinterpret_cast<const char*>(*reinterpret_cast<uint8_t* const*>(static_cast<uint8_t*>(self) + ASC_OFF(0x2C, 0x48)) + ASC_OFF(0x3C, 0x74));
        return g_823ED0(self ASC_EDX_A(edx), a);
    }
    int __fastcall M2Guarded(void* self ASC_EDX_P(void* edx), uint32_t a)   // 0x1019D810
    {
        if (*reinterpret_cast<void* const*>(static_cast<uint8_t*>(self) + ASC_OFF(0x2C, 0x48)) == nullptr)
            return 0;
        return g_823F10(self ASC_EDX_A(edx), a);
    }

    // ---- the writer ----------------------------------------------------------------------------------------
    const auto SStrPack = reinterpret_cast<void(__stdcall*)(char*, const char*, size_t)>(ASC_PTR(0x76EF70));

    // FUN_101a0f00: "[level] name (source:line)\n" per Lua stack level.
    std::string LuaTrace()
    {
        lua_State* L = AscLua::GetState();
        if (!L)
            return std::string();
        const auto getstack = reinterpret_cast<int(__cdecl*)(lua_State*, int, void*)>(ASC_PTR(0x84FE40));
        const auto getinfo = reinterpret_cast<int(__cdecl*)(lua_State*, const char*, void*)>(ASC_PTR(0x850A90));
        std::stringstream out;
        alignas(8) uint8_t ar[0x100] = {};   // lua_Debug: x86 0x80 / x64 0x78
        for (int level = 0; getstack(L, level, ar); ++level)
            if (getinfo(L, "nSl", ar))
            {
                const char* name = *reinterpret_cast<const char* const*>(ar + ASC_OFF(4, 8));   // lua_Debug::name
                if (!name || !*name)
                    name = "<anonymous>";
                out << "[" << level << "] " << name << " (" << reinterpret_cast<const char*>(ar + ASC_OFF(0x24, 0x38)) << ":"   // short_src
                    << *reinterpret_cast<const int*>(ar + ASC_OFF(0x14, 0x28)) << ")\n";
            }
        return out.str();
    }

    typedef int(__cdecl* Fn682200_t)(char*, uint32_t);
    Fn682200_t g_682200 = nullptr;
    int __cdecl CrashText(char* text, uint32_t a2)
    {
        const int result = g_682200(text, a2);
        auto line = [text](const std::string& s) {
            SStrPack(text, s.c_str(), 0x7FFFFFFF);
            SStrPack(text, "\r\n", 0x7FFFFFFF);
        };
        SStrPack(text, "\r\n----------------------------------------\r\n", 0x7FFFFFFF);
        SStrPack(text, "     Ascension Debug\r\n", 0x7FFFFFFF);
        SStrPack(text, "----------------------------------------\r\n", 0x7FFFFFFF);
        SStrPack(text, "\r\n", 0x7FFFFFFF);
        line("DLL Version: 792084a2572c06bfd865feaf553c001f2bdd60fd");
        PROCESS_MEMORY_COUNTERS pmc = {};
        if (K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
            line("Process Memory Usage: " + std::to_string(static_cast<uint64_t>(pmc.WorkingSetSize >> 20)) + "MB (Peak: " +
                 std::to_string(static_cast<uint64_t>(pmc.PeakWorkingSetSize >> 20)) + "MB)");
        line("Assert Info: " + g_assertInfo);
        line("HD Patch: " + std::to_string(AscRealmData::HdPatchLoaded() ? 1u : 0u));
        line("Last DetailDoodad IDX: " + g_detailDoodad);
        line("Last FrameScript_Execute: " + g_frameScript);
        line("Last FrameScript_SignalEvent: " + g_signalEvent);
        line("Last Loaded Map Object Name: " + g_mapObject);
        line("Last Loaded M2 Name (CM2Cache): " + g_m2Cache);
        line("Last Loaded M2 Name (CM2Shared): " + g_m2Shared);
        line("Last Loaded M2 (Skin) Name: " + g_m2Skin);
        line("Last M2 Animate Name: " + g_m2Animate + (g_m2IsDrawableFrom.empty() ? std::string() : " (Last IsDrawable: " + g_m2IsDrawableFrom + ")"));
        line("Last M2 Animate MT Name: " + g_m2AnimateMT);
        line("Last M2 ProcessCallbacks Name: " + g_m2ProcessCallbacks);
        line("Last M2 Animate Particle Name: " + g_m2AnimateParticle);
        line("Last M2 Scene Render Name: " + g_m2SceneRender);
        line("Last M2 SetVerticies Name: " + g_m2SetVertices);
        line("Last M2 WaitForLoad Name: " + g_m2WaitForLoad);
        line("Last M2 Is Drawable: " + g_m2IsDrawable);
        line("Last M2 Shadow Map Name: " + g_m2ShadowMap);
        line("Last M2 GetCollisionFacets: " + g_m2CollisionFacets);
        line("Last M2 GetSequenceInfo: " + g_m2SequenceInfo);
        line("Last Received Opcode: " + std::to_string(static_cast<uint32_t>(AscProfiler::CurrentOpcode())));
        line("Last Spell Tooltip ID: " + g_spellTooltip);
        line("Last Spell Table Lookup ID: " + g_spellLookup);
        line("Last Loaded Map ID: " + g_mapId);
        line("Last Async Texture: " + g_asyncTexture);
        line("Last Unit::GetModelData ID: " + std::to_string(g_modelDataEntry));
        line("Last TextureCreate: " + g_textureCreate);
        line("Last BLP: " + g_blp);
        line("Last GameObject::CGGameObject_C__ModelLoaded Entry: " + std::to_string(g_gameObjectEntry));
        line("Last GameObject::CGGameObject_C__ModelLoaded Model Name: " + g_gameObjectModel);
        {
            std::stringstream db;
            db << g_clientDb;   // operator<<(const void*)
            line("Last ClientDB::GetRow: DB = " + db.str() + " ID = " + std::to_string(g_clientDbId));
        }
        line("Last LoadGameTable = " + g_gameTable);
        line(std::string("Connection: ") + reinterpret_cast<const char*>(ASC_PTR(0xC79C9E)));
        SStrPack(text, "\r\n", 0x7FFFFFFF);
        const std::string trace = LuaTrace();
        if (!trace.empty())
        {
            SStrPack(text, "\r\n----------------------------------------\r\n", 0x7FFFFFFF);
            SStrPack(text, "      Stack Trace (lua)\r\n", 0x7FFFFFFF);
            SStrPack(text, "----------------------------------------\r\n", 0x7FFFFFFF);
            SStrPack(text, "\r\n", 0x7FFFFFFF);
            line(trace);
            // FUN_101a06e0 is meant to list the Lua stack values, but it only appends once its stream is
            // non-empty, and it starts empty -- so the original always prints just the label.
            SStrPack(text, "[Stack] ", 0x7FFFFFFF);
        }
        return result;
    }

    void Init()
    {
        g_682200 = reinterpret_cast<Fn682200_t>(AscRuntime::Detour(0x682200, 9, reinterpret_cast<void*>(&CrashText)));
        g_819210 = reinterpret_cast<Fn819210_t>(AscRuntime::Detour(0x819210, 11, reinterpret_cast<void*>(&FrameScriptExecute)));
        g_57ABC0 = reinterpret_cast<Fn57ABC0_t>(AscRuntime::Detour(0x57ABC0, 6, reinterpret_cast<void*>(&SpellTooltip)));
        g_717A20 = reinterpret_cast<Fn0p_t>(AscRuntime::Detour(0x717A20, 6, reinterpret_cast<void*>(&UnitModelData)));
#if !ASC_X64
        // x86: 0x65C290 e o WowClientDB<T>::GetRecord dobrado por ICF (pega todas as tabelas). x64: 237 copias
        // separadas + inlinadas nos chamadores -> sem ponto unico; "Last ClientDB::GetRow" fica DB = 0 ID = 0.
        g_65C290 = reinterpret_cast<Fn65C290_t>(AscRuntime::Detour(0x65C290, 6, reinterpret_cast<void*>(&ClientDbGetRow)));
#endif
        g_838490 = reinterpret_cast<Fn1_t>(AscRuntime::Detour(0x838490, 6, reinterpret_cast<void*>(&M2Skin)));
        g_830DC0 = reinterpret_cast<Fn0_t>(AscRuntime::Detour(0x830DC0, 6, reinterpret_cast<void*>(&M2Animate)));
        g_82F0F0 = reinterpret_cast<Fn5m_t>(AscRuntime::Detour(0x82F0F0, 9, reinterpret_cast<void*>(&M2AnimateMT)));
        g_832260 = reinterpret_cast<Fn0_t>(AscRuntime::Detour(0x832260, 6, reinterpret_cast<void*>(&M2ProcessCallbacks)));
        g_8309C0 = reinterpret_cast<FnFI_t>(AscRuntime::Detour(0x8309C0, 9, reinterpret_cast<void*>(&M2AnimateParticle)));
        g_7BD480 = reinterpret_cast<Fn2_t>(AscRuntime::Detour(0x7BD480, 7, reinterpret_cast<void*>(&LoadMapObject)));
        g_812410 = reinterpret_cast<Fn3_t>(AscRuntime::Detour(0x812410, 6, reinterpret_cast<void*>(&SpellLookupA)));
        g_810ED0 = reinterpret_cast<Fn3_t>(AscRuntime::Detour(0x810ED0, 6, reinterpret_cast<void*>(&SpellLookupB)));
        g_81F700 = reinterpret_cast<Fn1_t>(AscRuntime::Detour(0x81F700, 10, reinterpret_cast<void*>(&M2SceneRender)));
        g_4D7750 = reinterpret_cast<Fn4D7750_t>(AscRuntime::Detour(0x4D7750, 8, reinterpret_cast<void*>(&LoadMap)));
        g_7B31E0 = reinterpret_cast<Fn7B31E0_t>(AscRuntime::Detour(0x7B31E0, 6, reinterpret_cast<void*>(&DetailDoodad)));
        g_6AFFD0 = reinterpret_cast<Fn6AFFD0_t>(AscRuntime::Detour(0x6AFFD0, 5, reinterpret_cast<void*>(&LoadBlp)));
        g_823ED0 = reinterpret_cast<Fn1_t>(AscRuntime::Detour(0x823ED0, 6, reinterpret_cast<void*>(&M2WaitForLoad)));
        g_823F10 = reinterpret_cast<Fn1_t>(AscRuntime::Detour(0x823F10, 6, reinterpret_cast<void*>(&M2Guarded)));
        g_82EC30 = reinterpret_cast<Fn3m_t>(AscRuntime::Detour(0x82EC30, 6, reinterpret_cast<void*>(&M2CollisionFacets)));
        g_82CED0 = reinterpret_cast<Fn3m_t>(AscRuntime::Detour(0x82CED0, 6, reinterpret_cast<void*>(&M2SequenceInfo)));
        g_7F6A10 = reinterpret_cast<Fn0_t>(AscRuntime::Detour(0x7F6A10, 9, reinterpret_cast<void*>(&LoadGameTable)));
        g_824FC0 = reinterpret_cast<Fn2m_t>(AscRuntime::Detour(0x824FC0, 6, reinterpret_cast<void*>(&M2IsDrawable)));
    }
    AscBindings::Module s_module(nullptr, 0, &Init);
}

void AscCrashContext::Assert(const char* message, const char* file, int line)
{
    g_assertInfo = message;
    reinterpret_cast<void(__cdecl*)(const char*, const char*, int)>(ASC_PTR(0x8C51D0))(message, file, line);
}
