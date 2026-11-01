#include <WorldData/DNInfo.hpp>
#include <Ascension/AscX64.hpp>

void DNInfo::AddZoneLight(void* thisDNInfo, int32_t lightId, float f)
{
    reinterpret_cast<void (__thiscall*)(void*, int32_t, float)>(ASC_PTR(0x7ED150))(thisDNInfo, lightId, f);
}

void* DNInfo::GetDNInfoPtr()
{
    return reinterpret_cast<void* (__stdcall*)()>(ASC_PTR(0x7ECEF0))();
}
