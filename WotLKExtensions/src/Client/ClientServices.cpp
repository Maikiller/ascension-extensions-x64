#include <Ascension/AscScript.hpp>
#include <Client/ClientServices.hpp>
#include <Ascension/AscX64.hpp>
#include <Misc/DataContainer.hpp>

uint8_t __fastcall ClientServices::CharacterLogoutEx(void* thisChar ASC_EDX_P(uint32_t unused), char a3, uint8_t a4)
{
    sDC.ResetData();

    return reinterpret_cast<uint8_t (__thiscall*)(void*, char, uint8_t)>(ASC_PTR(0x6B1930))(thisChar, a3, a4);
}

WoWGUID ClientServices::GetActivePlayer()
{
    return reinterpret_cast<WoWGUID (__cdecl*)()>(ASC_PTR(0x4D3790))();
}

uint8_t ClientServices::GetCharacterClass()
{
    return reinterpret_cast<uint8_t (__cdecl*)()>(ASC_PTR(0x6B1080))();
}

void* ClientServices::GetObjectPtr(WoWGUID objGUID, uint32_t typeMask)
{
    return AscScript::ObjectPtr(static_cast<uint64_t>(objGUID), typeMask);   // 0x4D4DB0 (guid, mask, arquivo, linha)
}

uint32_t ClientServices::GetPowerDivisor(int32_t powerType)
{
    switch (powerType)
    {
        case 1:
        case 6:
            return 10;
        case 4:
            return 1000;
        case 0:
        case 2:
        case 3:
        case 5:
        default:
            return 1;
    }
}

CGUnit* ClientServices::GetUnitFromName(const char* name)
{
    return reinterpret_cast<CGUnit* (__cdecl*)(const char*)>(ASC_PTR(0x60C1F0))(name);
}

void ClientServices::InitializePlayer()
{
    reinterpret_cast<void (__cdecl*)()>(ASC_PTR(0x6E83B0))();
}

void ClientServices::SendPacket(CDataStore* packet)
{
    reinterpret_cast<void (__cdecl*)(CDataStore*)>(ASC_PTR(0x6B0B50))(packet);
}

SkillLineAbilityRow* ClientServices::SpellTableLookupAbility(uint32_t raceID, uint32_t classID, int32_t spellID)
{
    return reinterpret_cast<SkillLineAbilityRow* (__cdecl*)(uint32_t, uint32_t, uint32_t)>(ASC_PTR(0x812410))(raceID, classID, spellID);
}
