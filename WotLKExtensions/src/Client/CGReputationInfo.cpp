#include <Client/CGReputationInfo.hpp>
#include <Ascension/AscX64.hpp>

int32_t CGReputationInfo::GetFactionStanding(int32_t faction)
{
    return reinterpret_cast<int32_t (__cdecl*)(int32_t)>(ASC_PTR(0x5D05B0))(faction);
}
