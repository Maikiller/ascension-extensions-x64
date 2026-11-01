// The original's character-component module (installer sub_102cb370): item display collections -- extra
// component models attached to a character's equipment slots from ItemDisplayInfoCollections.dbc -- and
// the model bookkeeping, slot removal, quiver and small render hooks installed with it.
//
//   Bindings   SetModelApplyComponents (handler at 0x102CBFC0), ReloadItemDisplayInfoCollections
//              (0x1020A860). ApplyComponents (FUN_102cbb80) is registered by the same world registrar
//              (FUN_102ccee0) and lives in AscGlobalsJ.cpp.
//   Detours    0x5989E0 -> sub_102cab10, 0x597FC0 -> sub_102cab40   (flags around two client calls)
//              0x4F2640 -> sub_102caa20   (item added to a slot: collections attached, FUN_102cbfe0)
//              0x81F8F0 -> FUN_102cb000   (model created: id, pending scale, capture, last name)
//              0x4EE6D0 -> FUN_102caaa0   (CCharacterComponent::RemoveItemBySlot: attachments removed)
//              0x4EF840 -> sub_102caa60,  0x4F9F70 -> sub_102caea0
//              0x6E1E10 -> sub_102cac90,  0x6E09E0 -> FUN_102cac10  (unit refresh -> 0x72B7F0)
//              0x72B7F0 -> FUN_102cad20   (other players with a bow get a quiver)
//              0x832AB0 -> sub_102cafb0   (pass-through), 0x826E60 -> sub_102ccf50 (human male check)
//              0x824ED0 -> FUN_102caed0   (model released: id dropped)
//              0x6E08C0 -> FUN_102cab70   (player slot update: collections attached)
//   Packets    SMSG_PATCH_ITEM_DISPLAY_INFO_COLLECTIONS 0x56A (FUN_101e1b50).
//
// ItemDisplayInfoCollections.dbc rows (0x20): {id, displayId, attach, attachHD, model, texture, scale,
// scaleHD}; the HD columns are used when patch-Q is loaded (AscRealmData::HdPatchLoaded).
#include <Ascension/AscClientDbc.hpp>
#include <Ascension/AscBindings.hpp>
#include <Ascension/AscX64.hpp>
#include <Ascension/AscScript.hpp>
#include <Ascension/AscRuntime.hpp>
#include <Ascension/AscRealmData.hpp>
#include <Ascension/AscDbc.hpp>
#include <Ascension/AscLua.hpp>
#include <Client/CDataStore.hpp>
#include <Client/CNetClient.hpp>
#include <Misc/DataContainer.hpp>
#include <Windows.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

using namespace AscScript;

void AscGeosets_ModelCreated(void* model);   // AscGeosets.cpp

namespace
{
    // ---- DLL globals ----------------------------------------------------------------------------
    bool g_applyComponents = true;      // DAT_10bcbda4, SetModelApplyComponents
    bool g_inCall5989E0 = false;        // DAT_10be35e0
    bool g_inCall597FC0 = false;        // DAT_10be35e1
    bool g_slotReAdded = false;         // DAT_10be35e2
    bool g_humanMaleOverride = false;   // DAT_10be35e3 (never set by the original)
    uint16_t g_humanMaleValue = 0;      // DAT_10be35e4
    bool g_capture = false;             // DAT_10be357c
    void* g_captured = nullptr;         // DAT_10be3578
    float g_pendingScale = 0.0f;        // DAT_10bcbedc
    std::string g_lastModelName;        // DAT_10bcbcb0 ("Last Loaded M2 Name (CM2Shared)" in crash reports)
    uint32_t g_arg4F9F70[2] = {};       // DAT_10be3580 / 3584 (written only)

    // Model ids: every model 0x81F8F0 creates gets one (FUN_102ca9c0); the counter starts at 1 and
    // advances twice per id, as in the original.
    uint64_t g_nextModelId = 1;                                  // DAT_10bcbeb0
    std::unordered_map<void*, uint64_t> g_modelIds;              // 0x10BE35A0
    // Attached collection models: component -> slot index -> model ids (0x10BE35C0).
    std::unordered_map<void*, std::unordered_map<uint32_t, std::vector<uint64_t>>> g_attached;
    // displayId -> collection rows (0x10BE0638), built from the table like FUN_1020feb0.
    std::unordered_map<uint32_t, std::vector<const uint8_t*>> g_collections;
    bool g_collectionsBuilt = false;

    AscDbc::Table& Collections() { return AscDbc::Get("DBFilesClient\\ItemDisplayInfoCollections.dbc"); }

    void BuildCollections()
    {
        g_collections.clear();
        AscDbc::Table& t = Collections();
        if (t.Loaded())
            for (uint32_t id = t.MinId(); id <= t.MaxId(); ++id)
            {
                if (const uint8_t* row = t.Row(id))
                    g_collections[AscDbc::Table::U32(row, 4)].push_back(row);
                if (id == 0xFFFFFFFF)
                    break;
            }
        g_collectionsBuilt = true;
    }

    uint64_t ModelId(void* model)       // FUN_102cb2e0
    {
        auto it = g_modelIds.find(model);
        return it == g_modelIds.end() ? 0 : it->second;
    }
    void AssignModelId(void* model)     // FUN_102ca9c0
    {
        const uint64_t id = g_nextModelId++;
        g_modelIds[model] = id;
        ++g_nextModelId;
    }

    // FUN_102cb240: CCharacterComponent slot (ITEMDISPLAY) -> the DLL's slot index.
    int32_t SlotIndex(uint32_t slot)
    {
        switch (slot)
        {
        case 0: return 0;
        case 2: return 1;
        case 3: return 2;
        case 4: return 3;
        case 5: return 4;
        case 6: return 5;
        case 7: return 6;
        case 8: return 7;
        case 9: return 8;
        case 0xE: return 10;
        case 0x12: return 9;
        default: return -1;
        }
    }

    // The slot folders at 0x10BCBDA8 (constant std::string table, one per slot index).
    const char* const kSlotFolders[] = {"Head", "Shoulder", "Collections", "Collections", "Waist",
                                        "Collections", "Collections", "Collections", "Collections", "", "Cape"};
    const char* Folder(uint32_t index) { return index < 11 ? kSlotFolders[index] : ""; }

    std::string DropExtension(const std::string& s) { return s.substr(0, s.size() < 4 ? s.size() : s.size() - 4); }

    // FUN_102cc980: "Item\ObjectComponents\<folder>\<name>"; per-race models (not for slot index 4, with a
    // race, gender not 2) become "...\<name minus extension>_<ChrRaces prefix>_<M|F|>.mdx".
    std::string ModelPath(uint32_t index, std::string name, uint32_t race, uint32_t gender)
    {
        if (name.empty())
            return std::string();
        if (index == 4 || race == 0 || gender == 2)
            return std::string("Item\\ObjectComponents\\") + Folder(index) + "\\" + name;
        const uint8_t* chrRace = ClientDbcRow(0xAD3428, race);   // FUN_100b13f0
        if (!chrRace)
            return std::string();
        name = DropExtension(name);
        std::string sex;
        if (gender == 0)
            sex = "M";
        else if (gender == 1)
            sex = "F";
        return std::string("Item\\ObjectComponents\\") + Folder(index) + "\\" + name + "_" +
               AscClientDbc::Str(chrRace, 0x18) + "_" + sex + ".mdx";
    }
    // FUN_102cc540: "Item\ObjectComponents\<folder>\<name minus extension>.blp".
    std::string TexturePath(uint32_t index, const std::string& name)
    {
        if (name.empty())
            return std::string();
        return std::string("Item\\ObjectComponents") + "\\" + Folder(index) + "\\" + DropExtension(name) + ".blp";
    }

    // FUN_102cba60 (the std::function both removal paths pass): a model belongs to (component, index)
    // when its id is in that list; a match is taken out of the list.
    bool TakeAttached(void* component, uint32_t index, void* model)
    {
        auto outer = g_attached.find(component);
        if (outer == g_attached.end())
            return false;
        auto inner = outer->second.find(index);
        if (inner == outer->second.end())
            return false;
        const uint64_t id = ModelId(model);
        if (id == 0)
            return false;
        auto it = std::find(inner->second.begin(), inner->second.end(), id);
        if (it == inner->second.end())
            return false;
        inner->second.erase(it);
        return true;
    }
    // FUN_102cb100 on *(component +0x38): the scene's models (+0x58, linked through +0x60) that match
    // are released through 0x8274F0.
    void RemoveAttached(uint8_t* component, uint32_t index)
    {
        uint8_t* scene = *reinterpret_cast<uint8_t* const*>(component + ASC_OFF(0x38, 0x40));
        std::vector<uint8_t*> matched;
        for (uint8_t* m = *reinterpret_cast<uint8_t* const*>(scene + ASC_OFF(0x58, 0x98)); m; m = *reinterpret_cast<uint8_t* const*>(m + ASC_OFF(0x60, 0xA8)))
            if (TakeAttached(component, index, m))
                matched.push_back(m);
        for (uint8_t* m : matched)
            reinterpret_cast<void(__thiscall*)(void*)>(ASC_PTR(0x8274F0))(m);
    }

    // FUN_102cbfe0 (Update3DAttachments): the slot's previous collection models go; then, for a display
    // row with collections (component +0x18 race set, +0x1C gender not 2, not inside both flagged client
    // calls, SetModelApplyComponents on), each collection is attached through 0x4EAA70 with its paths in
    // the client's component buffers, its scale applied by the model hook, and the model recorded.
    void Attach(uint8_t* component, uint32_t index, const uint8_t* display)
    {
        RemoveAttached(component, index);
        if (!display || *reinterpret_cast<const uint32_t*>(component + ASC_OFF(0x18, 0x20)) == 0 ||
            *reinterpret_cast<const uint32_t*>(component + ASC_OFF(0x1C, 0x24)) == 2 || (g_inCall5989E0 && g_inCall597FC0) || !g_applyComponents)
            return;
        if (!g_collectionsBuilt)
            BuildCollections();
        auto it = g_collections.find(*reinterpret_cast<const uint32_t*>(display));
        if (it == g_collections.end())
            return;
        AscDbc::Table& t = Collections();
        const uint32_t race = *reinterpret_cast<const uint32_t*>(component + ASC_OFF(0x18, 0x20));
        const uint32_t gender = *reinterpret_cast<const uint32_t*>(component + ASC_OFF(0x1C, 0x24));
        for (const uint8_t* row : it->second)
        {
            const std::string model = ModelPath(index, t.Str(row, 0x10), race, gender);
            std::string texture = TexturePath(index, t.Str(row, 0x14));
            if (texture.empty())
                texture = TexturePath(index, t.Str(row, 0x10));
            strcpy(reinterpret_cast<char*>(ASC_PTR(0xB6B600)), model.c_str());
            strcpy(reinterpret_cast<char*>(ASC_PTR(0xB6B4F8)), texture.c_str());
            const bool hd = AscRealmData::HdPatchLoaded();
            g_pendingScale = AscDbc::Table::F32(row, hd ? 0x1C : 0x18);
            g_capture = true;
            reinterpret_cast<void(__cdecl*)(uintptr_t, uint32_t, char*, char*, int, const uint8_t*)>(ASC_PTR(0x4EAA70))(
                *reinterpret_cast<const uintptr_t*>(component + ASC_OFF(0x38, 0x40)), AscDbc::Table::U32(row, hd ? 0xC : 8),
                reinterpret_cast<char*>(ASC_PTR(0xB6B600)), reinterpret_cast<char*>(ASC_PTR(0xB6B4F8)), 0, display);
            if (void* m = g_captured)
            {
                if (ModelId(m) == 0)
                    AssignModelId(m);
                g_attached[component][index].push_back(ModelId(m));
            }
            g_capture = false;
            g_captured = nullptr;
        }
    }

    // ItemDisplayInfoRec que vai para o AddLink do cliente (e cujo id, +0, o Attach le). x86: a copia de
    // WowClientDB::GetRecord (0x4CFD90) num buffer estatico; x64: o registro nativo (AscClientDbc::Native devolve
    // a copia descomprimida quando a tabela esta em RLE).
#if ASC_X64
    const void* DisplayForClient(uint32_t id) { return AscClientDbc::Native(0xAD3DDC, id); }
#endif

    // ---- detours --------------------------------------------------------------------------------
    typedef int(__fastcall* Fn1_t)(void* ASC_EDX_P(void*), uintptr_t);
    typedef int(__fastcall* Fn2_t)(void* ASC_EDX_P(void*), uintptr_t, uintptr_t);
    typedef int(__fastcall* Fn3_t)(void* ASC_EDX_P(void*), uint32_t, uintptr_t, uint32_t);
    typedef int(__fastcall* Fn0_t)(void* ASC_EDX_P(void*));

    Fn1_t g_5989E0 = nullptr;           // sub_102cab10 (__thiscall(a), ret 4)
    int __fastcall Detour5989E0(void* ecx ASC_EDX_P(void* edx), uintptr_t a)
    {
        g_inCall5989E0 = true;
        const int r = g_5989E0(ecx ASC_EDX_A(edx), a);
        g_inCall5989E0 = false;
        return r;
    }
    Fn3_t g_597FC0 = nullptr;           // sub_102cab40 (__thiscall(a, b, c), ret 0xC)
    int __fastcall Detour597FC0(void* ecx ASC_EDX_P(void* edx), uint32_t a, uintptr_t b, uint32_t c)
    {
        g_inCall597FC0 = true;
        const int r = g_597FC0(ecx ASC_EDX_A(edx), a, b, c);
        g_inCall597FC0 = false;
        return r;
    }
    Fn3_t g_4F2640 = nullptr;           // sub_102caa20 (__thiscall(component, slot, display, c), ret 0xC)
    int __fastcall Detour4F2640(void* ecx ASC_EDX_P(void* edx), uint32_t slot, uintptr_t display, uint32_t c)
    {
        g_slotReAdded = true;
        const int r = g_4F2640(ecx ASC_EDX_A(edx), slot, display, c);
        Attach(static_cast<uint8_t*>(ecx), slot, reinterpret_cast<const uint8_t*>(display));
        return r;
    }
    // FUN_102cb000 over 0x81F8F0 (__thiscall(this, name, b), ret 8). Devolve o CM2Model* (x64: 8 bytes, nao int).
    typedef void*(__fastcall* CreateModel_t)(void* ASC_EDX_P(void*), uintptr_t, uint32_t);
    CreateModel_t g_81F8F0 = nullptr;
    void* __fastcall Detour81F8F0(void* ecx ASC_EDX_P(void* edx), uintptr_t name, uint32_t b)
    {
        g_lastModelName = reinterpret_cast<const char*>(name);   // FUN_1008aee0: assign, unchecked
        void* model = g_81F8F0(ecx ASC_EDX_A(edx), name, b);
        AscGeosets_ModelCreated(model);   // the geoset module's own hook on 0x81F8F0 (FUN_101a14f0)
        if (model)
        {
            AssignModelId(model);
            if (!(g_pendingScale == 0.0f))
            {
                float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
                for (int row = 0; row < 3; ++row)       // FUN_102bd4d0: the 3x3 part scaled
                    for (int col = 0; col < 3; ++col)
                        m[row * 4 + col] *= g_pendingScale;
                reinterpret_cast<void(__thiscall*)(void*, float*)>(ASC_PTR(0x4D8630))(model, m);
                g_pendingScale = 0.0f;
            }
        }
        if (g_capture)
            g_captured = model;
        return model;
    }
    // FUN_102caaa0 over 0x4EE6D0 (RemoveItemBySlot, __thiscall(component, slot), ret 4): unless the
    // original re-added the slot, its collection models go.
    Fn1_t g_4EE6D0 = nullptr;
    int __fastcall Detour4EE6D0(void* ecx ASC_EDX_P(void* edx), uint32_t slot)
    {
        g_slotReAdded = false;
        const int r = g_4EE6D0(ecx ASC_EDX_A(edx), slot);
        if (!g_slotReAdded)
        {
            const int32_t index = SlotIndex(slot);
            if (index != -1)
                RemoveAttached(static_cast<uint8_t*>(ecx), static_cast<uint32_t>(index));
        }
        return r;
    }
    // sub_102caa60 over 0x4EF840 (__thiscall(a), ret 4): in world, 0x4D3790 first (result unused).
    Fn1_t g_4EF840 = nullptr;
    int __fastcall Detour4EF840(void* ecx ASC_EDX_P(void* edx), uint32_t a)
    {
        if (*reinterpret_cast<const uint8_t*>(ASC_PTR(0xBD0792)) != 0)
            ActivePlayerGuid();
        return g_4EF840(ecx ASC_EDX_A(edx), a);
    }
    // sub_102caea0 over 0x4F9F70 (__cdecl, four arguments).
    typedef int(__cdecl* Fn4F9F70_t)(uintptr_t, uint32_t, uintptr_t, uintptr_t);
    Fn4F9F70_t g_4F9F70 = nullptr;
    int __cdecl Detour4F9F70(uintptr_t a, uint32_t b, uintptr_t c, uintptr_t d)
    {
        g_arg4F9F70[0] = c;
        g_arg4F9F70[1] = d;
        return g_4F9F70(a, b, c, d);
    }
    // sub_102cac90 / FUN_102cac10: after the original, a unit whose descriptor +0x598 names an item the
    // client knows (0x707330) re-runs 0x72B7F0 with (state byte +0x1E8 != 2) for units and players.
    void RefreshUnit(uint8_t* unit)
    {
        const uint8_t* d = *reinterpret_cast<uint8_t* const*>(unit + ASC_OFF(8, 0x10));
        const uint64_t guid = *reinterpret_cast<const uint64_t*>(d + 0x598);
        if (guid == 0)
            return;
        void* object = ObjectPtr(guid, 2);
        if (!object || !reinterpret_cast<int(__thiscall*)(void*)>(ASC_PTR(0x707330))(object))
            return;
        const uint32_t type = *reinterpret_cast<const uint32_t*>(unit + ASC_OFF(0x14, 0x24));
        const uint8_t state = (type == 3 || type == 4) ? d[0x1E8] : 0;
        reinterpret_cast<int(__thiscall*)(void*, int, void*)>(ASC_PTR(0x72B7F0))(unit, state != 2 ? 1 : 0, nullptr);   // ShowRangedWeapon(sheathed, bool* rightHand)
    }
    Fn2_t g_6E1E10 = nullptr;
    int __fastcall Detour6E1E10(void* ecx ASC_EDX_P(void* edx), uint32_t a, uintptr_t b)
    {
        const int r = g_6E1E10(ecx ASC_EDX_A(edx), a, b);
        RefreshUnit(static_cast<uint8_t*>(ecx));
        return r;
    }
    Fn0_t g_6E09E0 = nullptr;
    int __fastcall Detour6E09E0(void* ecx ASC_EDX_P(void* edx))
    {
        const int r = g_6E09E0(ecx ASC_EDX_A(edx));
        RefreshUnit(static_cast<uint8_t*>(ecx));
        return r;
    }
    // FUN_102cad20 over 0x72B7F0 (__thiscall(unit, a, b), ret 8): for another player (a == 0) whose
    // ranged slot item (descriptor +0x4EC, item cache 0xC5D828) is class 2 / subclass 2, a quiver is
    // attached to component slot 0x1A with ItemDisplayInfo 21328.
    Fn2_t g_72B7F0 = nullptr;
    int __fastcall Detour72B7F0(void* ecx ASC_EDX_P(void* edx), uint32_t a, uintptr_t b)
    {
        const int r = g_72B7F0(ecx ASC_EDX_A(edx), a, b);
        uint8_t* unit = static_cast<uint8_t*>(ecx);
        if (static_cast<uint8_t>(a) != 0 || *reinterpret_cast<const uint32_t*>(unit + ASC_OFF(0x14, 0x24)) != 4 || unit == ActivePlayer())
            return r;
        const uint8_t* d = *reinterpret_cast<uint8_t* const*>(unit + ASC_OFF(8, 0x10));
        const uint8_t* item = reinterpret_cast<const uint8_t*(__thiscall*)(void*, uint32_t, void*, void*, void*, int)>(ASC_PTR(0x67CA30))(
            reinterpret_cast<void*>(ASC_PTR(0xC5D828)), *reinterpret_cast<const uint32_t*>(d + 0x4EC), ASC_NOGUID, nullptr, nullptr, 0);
        if (!item || *reinterpret_cast<const uint32_t*>(item + ASC_OFF(4, 8)) != 2 || *reinterpret_cast<const uint32_t*>(item + ASC_OFF(8, 0xC)) != 2)
            return r;
        strcpy(reinterpret_cast<char*>(ASC_PTR(0xB6B600)), "Item\\ObjectComponents\\Quiver\\Quiver_A.mdx");
        strcpy(reinterpret_cast<char*>(ASC_PTR(0xB6B4F8)), "Item\\ObjectComponents\\Quiver\\Quiver_Alliance_A_04Brown.blp");
        // AddLink recebe o ItemDisplayInfoRec do cliente: no x64 o registro nativo (layout x64), nao a copia 12340.
#if ASC_X64
        if (const void* display = DisplayForClient(0x5350))
#else
        uint8_t display[0x64];
        if (AscClientDbc::Copy(0x4CFD90, 0xAD3DDC, 0x5350, display))
#endif
            reinterpret_cast<void(__cdecl*)(uintptr_t, uint32_t, char*, char*, int, const void*)>(ASC_PTR(0x4EAA70))(
                *reinterpret_cast<const uintptr_t*>(unit + ASC_OFF(0xB4, 0x138)), 0x1A, reinterpret_cast<char*>(ASC_PTR(0xB6B600)),
                reinterpret_cast<char*>(ASC_PTR(0xB6B4F8)), 0, display);
        return r;
    }
    // sub_102cafb0 over 0x832AB0 (__thiscall, seven stack arguments): passes everything through.
    typedef int(__fastcall* Fn7_t)(void* ASC_EDX_P(void*), uint32_t, uint32_t, uint32_t, uint32_t, float, uint32_t, uint32_t);   // 5o = speed (float)
    Fn7_t g_832AB0 = nullptr;
    int __fastcall Detour832AB0(void* ecx ASC_EDX_P(void* edx), uint32_t a, uint32_t b, uint32_t c, uint32_t d, float e, uint32_t f, uint32_t g)
    {
        return g_832AB0(ecx ASC_EDX_A(edx), a, b, c, d, e, f, g);
    }
    // sub_102ccf50 over 0x826E60 (__thiscall(model, a, u16* out), ret 8): remembers the value the human
    // male model gets; the override flag that would replay it is never set by the original.
    Fn2_t g_826E60 = nullptr;
    int __fastcall Detour826E60(void* ecx ASC_EDX_P(void* edx), uintptr_t a, uintptr_t outAddr)
    {
        const int r = g_826E60(ecx ASC_EDX_A(edx), a, outAddr);
        uint16_t* out = reinterpret_cast<uint16_t*>(outAddr);
        if (g_humanMaleOverride)
        {
            *out = g_humanMaleValue;
            return r;
        }
        const char* path = reinterpret_cast<const char*>(*reinterpret_cast<const uint8_t* const*>(static_cast<uint8_t*>(ecx) + ASC_OFF(0x2C, 0x48)) + ASC_OFF(0x3C, 0x74));
        if (reinterpret_cast<int(__stdcall*)(const char*, const char*, size_t)>(ASC_PTR(0x76E780))(path, "character\\human\\male\\humanmale.m2", 0x7FFFFFFF) == 0)
            g_humanMaleValue = *out;
        return r;
    }
    // FUN_102caed0 over 0x824ED0 (model release, __thiscall(model)): the last reference drops its id.
    Fn0_t g_824ED0 = nullptr;
    int __fastcall Detour824ED0(void* ecx ASC_EDX_P(void* edx))
    {
        if (*static_cast<const int32_t*>(ecx) == 1)
            g_modelIds.erase(ecx);
        return g_824ED0(ecx ASC_EDX_A(edx));
    }
    // FUN_102cab70 over 0x6E08C0 (__thiscall(unit, a, slot), ret 8): before the original, a player's
    // character component re-attaches the slot's collections for its current display id (+0x428 + index*4).
    Fn2_t g_6E08C0 = nullptr;
    int __fastcall Detour6E08C0(void* ecx ASC_EDX_P(void* edx), uintptr_t a, uint32_t slot)
    {
        uint8_t* unit = static_cast<uint8_t*>(ecx);
        if (*reinterpret_cast<const uint32_t*>(unit + ASC_OFF(0x14, 0x24)) == 4)
            if (uint8_t* component = *reinterpret_cast<uint8_t* const*>(unit + ASC_OFF(0xB4C, 0x1370)))
            {
                const int32_t index = SlotIndex(slot);
                if (index != -1)
                {
#if ASC_X64
                    const void* display = DisplayForClient(*reinterpret_cast<const uint32_t*>(component + ASC_OFF(0x428, 0x590) + index * 4));
                    Attach(component, static_cast<uint32_t>(index), static_cast<const uint8_t*>(display));
#else
                    uint8_t display[0x64];
                    const bool found = AscClientDbc::Copy(0x4CFD90, 0xAD3DDC, *reinterpret_cast<const uint32_t*>(component + ASC_OFF(0x428, 0x590) + index * 4), display) != 0;
                    Attach(component, static_cast<uint32_t>(index), found ? display : nullptr);
#endif
                }
            }
        return g_6E08C0(ecx ASC_EDX_A(edx), a, slot);
    }

    // 0x6E09E0 begins push esi / mov esi,ecx / call 0x7202C0: the relative call is re-targeted in the
    // trampoline, which then jumps back to 0x6E09E8.
    void* CallFixTrampoline()
    {
#if ASC_X64
        return nullptr;   // trampolim x86 (call rel32 + jmp) -- no x64 o hook entra por AscRuntime::Detour (MinHook)
#endif
        uint8_t* t = static_cast<uint8_t*>(VirtualAlloc(nullptr, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        t[0] = 0x56; t[1] = 0x8B; t[2] = 0xF1;
        t[3] = 0xE8;
        *reinterpret_cast<uint32_t*>(t + 4) = 0x7202C0 - reinterpret_cast<uint32_t>(t + 8);
        t[8] = 0xE9;
        *reinterpret_cast<uint32_t*>(t + 9) = 0x6E09E8 - reinterpret_cast<uint32_t>(t + 13);
        uint8_t jmp[8] = {0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90};
        *reinterpret_cast<uint32_t*>(jmp + 1) = reinterpret_cast<uint32_t>(&Detour6E09E0) - (0x6E09E0 + 5);
        DWORD old;
        VirtualProtect(reinterpret_cast<void*>(ASC_PTR(0x6E09E0)), 8, PAGE_EXECUTE_READWRITE, &old);
        memcpy(reinterpret_cast<void*>(ASC_PTR(0x6E09E0)), jmp, 8);
        VirtualProtect(reinterpret_cast<void*>(ASC_PTR(0x6E09E0)), 8, old, &old);
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(ASC_PTR(0x6E09E0)), 8);
        return t;
    }

    // ---- packet ---------------------------------------------------------------------------------
    // SMSG_PATCH_ITEM_DISPLAY_INFO_COLLECTIONS (0x56A, FUN_101e1b50): 0x20 bytes {id, displayId, attach,
    // attachHD, -, -, scale, scaleHD}, then the model and texture strings. The collections map is not
    // rebuilt (ReloadItemDisplayInfoCollections does that).
    void __cdecl OnPatchCollection(void*, uint32_t, uint32_t, CDataStore* p)
    {
        uint32_t row[8];
        memcpy(row, p->m_buffer + p->m_read, 0x20);
        p->m_read += 0x20;
        const char* model = reinterpret_cast<const char*>(p->m_buffer + p->m_read);
        p->m_read += static_cast<uint32_t>(strlen(model)) + 1;
        const char* texture = reinterpret_cast<const char*>(p->m_buffer + p->m_read);
        p->m_read += static_cast<uint32_t>(strlen(texture)) + 1;
        AscDbc::Table& t = Collections();
        row[4] = t.AddString(model);
        row[5] = t.AddString(texture);
        t.Upsert(row[0], std::vector<uint8_t>(reinterpret_cast<uint8_t*>(row), reinterpret_cast<uint8_t*>(row) + 0x20));
    }

    // ---- bindings -------------------------------------------------------------------------------
    int SetModelApplyComponents(lua_State* L)       // handler at 0x102CBFC0
    {
        g_applyComponents = AscLua::lua_toboolean(L, 1) != 0;
        return 0;
    }
    // 0x1020A860: the map is cleared and rebuilt. Deviation: the original re-runs its DBC loader
    // (FUN_101e7b10); here the map is rebuilt from the table as patched, in id order.
    int ReloadItemDisplayInfoCollections(lua_State*)
    {
        BuildCollections();
        return 0;
    }

    void Init()
    {
        typedef void* V;
        g_5989E0 = reinterpret_cast<Fn1_t>(AscRuntime::Detour(0x5989E0, 9, reinterpret_cast<V>(&Detour5989E0)));
        g_597FC0 = reinterpret_cast<Fn3_t>(AscRuntime::Detour(0x597FC0, 6, reinterpret_cast<V>(&Detour597FC0)));
        g_4F2640 = reinterpret_cast<Fn3_t>(AscRuntime::Detour(0x4F2640, 6, reinterpret_cast<V>(&Detour4F2640)));
        g_81F8F0 = reinterpret_cast<CreateModel_t>(AscRuntime::Detour(0x81F8F0, 6, reinterpret_cast<V>(&Detour81F8F0)));
        g_4EE6D0 = reinterpret_cast<Fn1_t>(AscRuntime::Detour(0x4EE6D0, 6, reinterpret_cast<V>(&Detour4EE6D0)));
        g_4EF840 = reinterpret_cast<Fn1_t>(AscRuntime::Detour(0x4EF840, 9, reinterpret_cast<V>(&Detour4EF840)));
        g_4F9F70 = reinterpret_cast<Fn4F9F70_t>(AscRuntime::Detour(0x4F9F70, 6, reinterpret_cast<V>(&Detour4F9F70)));
        g_6E1E10 = reinterpret_cast<Fn2_t>(AscRuntime::Detour(0x6E1E10, 6, reinterpret_cast<V>(&Detour6E1E10)));
        // x64: CallFixTrampoline devolve nullptr de proposito -- o AscSmallHooks ja hooka 0x6E09E0 com o mesmo efeito
        // (Hook6E09E0 = RefreshUnit), e um 2o MinHook no mesmo alvo falharia. No x86 vale o ultimo jmp escrito.
        g_6E09E0 = reinterpret_cast<Fn0_t>(CallFixTrampoline());
        g_72B7F0 = reinterpret_cast<Fn2_t>(AscRuntime::Detour(0x72B7F0, 9, reinterpret_cast<V>(&Detour72B7F0)));
        g_832AB0 = reinterpret_cast<Fn7_t>(AscRuntime::Detour(0x832AB0, 6, reinterpret_cast<V>(&Detour832AB0)));
        g_826E60 = reinterpret_cast<Fn2_t>(AscRuntime::Detour(0x826E60, 6, reinterpret_cast<V>(&Detour826E60)));
        g_824ED0 = reinterpret_cast<Fn0_t>(AscRuntime::Detour(0x824ED0, 6, reinterpret_cast<V>(&Detour824ED0)));
        g_6E08C0 = reinterpret_cast<Fn2_t>(AscRuntime::Detour(0x6E08C0, 7, reinterpret_cast<V>(&Detour6E08C0)));
        sDC.AddPacketHandler(0x56A, CNetClientCustomPacket((void*)&OnPatchCollection, nullptr));
    }

    const AscBindings::Binding kBindings[] = {
        {nullptr, "SetModelApplyComponents", SetModelApplyComponents},
        {nullptr, "ReloadItemDisplayInfoCollections", ReloadItemDisplayInfoCollections},
    };
    AscBindings::Module s_module(kBindings, sizeof(kBindings) / sizeof(kBindings[0]), &Init);
}
