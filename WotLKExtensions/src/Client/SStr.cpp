#include <Client/SStr.hpp>
#include <Ascension/AscX64.hpp>

#include <cstdarg>
#include <cstdio>

int32_t SStr::Printf(char* buf, uint32_t length, char* fmt, ...)
{
    char buffer[2048] = { 0 };
    va_list args;

    va_start(args, fmt);
    vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, fmt, args);
    va_end(args);

    // original SStr::Printf
    return reinterpret_cast<int32_t (__cdecl*)(char*, size_t, const char*, ...)>(ASC_PTR(0x76F070))(buf, length, "%s", buffer);
}

// in some IDA dbs known as SStrCopy_0
char* SStr::Append(char* dst, char* src, int32_t size)
{
    return reinterpret_cast<char* (__stdcall*)(char*, const char*, size_t)>(ASC_PTR(0x76EF70))(dst, src, static_cast<size_t>(size));
}

char* SStr::Copy(char* dst, char* src, int32_t size)
{
    return reinterpret_cast<char* (__stdcall*)(char*, const char*, size_t)>(ASC_PTR(0x76ED20))(dst, src, static_cast<size_t>(size));
}
