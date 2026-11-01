#include <Ascension/AscX64.hpp>
// Borrowed from TSWoW

#pragma once

#define CLIENT_FUNCTION(name,addr,calltype,retval,...) \
    typedef retval (calltype *name##Type)__VA_ARGS__;\
    inline name##Type name = (name##Type)(ASC_PTR(addr));

#define CLIENT_ADDRESS(type,name,addr) inline type* name = (type*)ASC_PTR(addr);
