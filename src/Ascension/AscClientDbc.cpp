// See AscClientDbc.hpp. Transcribed from FUN_100ae510 (every inserter is this code with another container).
#include <Ascension/AscClientDbc.hpp>
#include <Ascension/AscX64.hpp>
#include <Ascension/AscDbc.hpp>
#include <Ascension/AscLog.hpp>
#include <cstring>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <map>

namespace
{
#if ASC_X64
    // WowClient64 (PDB): WowClientDB_Base vptr +0, m_loaded +8, m_numRecords +0xC, m_maxID +0x10, m_minID +0x14,
    // m_strings +0x18; WowClientDB<T>: IDatabase vptr +0x20, m_records +0x28, m_recordsById +0x30 (RLE: m_data +0x20,
    // m_records +0x28, m_recordsById +0x30). `container` continua sendo o endereco 12340 da tabela.
    uint8_t* C(uint32_t c) { return static_cast<uint8_t*>(AscX64::Ptr(c)); }
    uint32_t& Max(uint32_t c) { return *reinterpret_cast<uint32_t*>(C(c) + 0x10); }
    uint32_t& Min(uint32_t c) { return *reinterpret_cast<uint32_t*>(C(c) + 0x14); }
    uint8_t**& Index(uint32_t c) { return *reinterpret_cast<uint8_t***>(C(c) + 0x30); }   // m_recordsById (x64; NAO e CVar)
#else
    uint32_t& Max(uint32_t c) { return *reinterpret_cast<uint32_t*>(c + 0xC); }
    uint32_t& Min(uint32_t c) { return *reinterpret_cast<uint32_t*>(c + 0x10); }
    uint8_t**& Index(uint32_t c) { return *reinterpret_cast<uint8_t***>(c + 0x20); }
#endif
}

#if ASC_X64
namespace
{
    // Registro 12340 (x86) <-> WowClient64: para cada campo de 4 bytes do x86, o offset no x64 e se e ponteiro.
    struct FieldMap { uint16_t off32, off64; uint8_t ptr; };
    struct RecMap { uint32_t container; const char* rec; uint32_t size32, size64; const FieldMap* f; uint32_t n; };
#include "AscClientDbcMaps.generated.inc"

    const RecMap* MapOf(uint32_t container)
    {
        for (const RecMap& m : kRecMaps)
            if (m.container == container)
                return &m;
        static uint32_t lastMiss = 0;
        if (container != lastMiss)
            AscLog::Printf("x64: AscClientDbc container 0x%06X sem mapa de registro", lastMiss = container);
        return nullptr;
    }

    // Copia no layout x86 de um registro x64; ponteiros (strings) ficam 0 na copia e sao lidos por Str().
    // src = registro x64 do cliente; data = de onde os campos sao lidos (src, ou a copia descomprimida em `dec`
    // quando a tabela e WowClientDB_RLE e g_clientDbPerformCompress (0xC5DEA0) esta ligado).
    struct Shadow { const RecMap* map; const uint8_t* src; std::vector<uint8_t> bytes; std::vector<uint8_t> dec; const uint8_t* data; };

    // WowClientDB_RLE: Spell (0xAD49D0) e ItemDisplayInfo (0xAD3DDC); os demais guardam o registro cru.
    bool Compressed(uint32_t container)
    {
        return (container == 0xAD49D0 || container == 0xAD3DDC) && *static_cast<const int32_t*>(AscX64::Ptr(0xC5DEA0)) != 0;
    }
    const uint8_t* Decompress(const RecMap* m, const uint8_t* src, std::vector<uint8_t>& dec)
    {
        dec.resize(m->size64);
        typedef uint8_t* (*DecompressRow_t)(const uint8_t*, uint32_t, uint8_t*);   // DecompressRow 0x4CFBB0
        reinterpret_cast<DecompressRow_t>(AscX64::Ptr(0x4CFBB0))(src, m->size64, dec.data());
        return dec.data();
    }
    std::unordered_map<const uint8_t*, Shadow*>& BySrc() { static std::unordered_map<const uint8_t*, Shadow*> m; return m; }
    std::map<const uint8_t*, Shadow*>& ByCopy() { static std::map<const uint8_t*, Shadow*> m; return m; }

    uint8_t* ShadowOf(const RecMap* m, const uint8_t* src)
    {
        Shadow*& sh = BySrc()[src];
        if (!sh)
        {
            sh = new Shadow{m, src, std::vector<uint8_t>(m->size32, 0), {}, src};
            ByCopy()[sh->bytes.data()] = sh;
        }
        sh->data = Compressed(m->container) ? Decompress(m, src, sh->dec) : src;
        for (uint32_t i = 0; i < m->n; ++i)   // atualiza a cada leitura (o registro pode mudar)
            if (!m->f[i].ptr)
                memcpy(&sh->bytes[m->f[i].off32], sh->data + m->f[i].off64, 4);
        return sh->bytes.data();
    }
}
#endif

uint32_t AscClientDbc::MinId(uint32_t container)
{
#if ASC_X64
    return AscX64::Ptr(container) ? Min(container) : 1;
#else
    return Min(container);
#endif
}

uint32_t AscClientDbc::MaxId(uint32_t container)
{
#if ASC_X64
    return AscX64::Ptr(container) ? Max(container) : 0;
#else
    return Max(container);
#endif
}

const char* AscClientDbc::Str(const void* rowp, uint32_t off)
{
    const uint8_t* row = static_cast<const uint8_t*>(rowp);
#if ASC_X64
    // linha-copia de um registro do cliente: o char* vem do registro x64 original
    auto it = ByCopy().upper_bound(row);
    if (it != ByCopy().begin())
    {
        const auto& kv = *--it;
        const Shadow* sh = kv.second;
        if (row + off < kv.first + sh->bytes.size())
        {
            const uint32_t at = static_cast<uint32_t>((row - kv.first) + off);
            for (uint32_t i = 0; i < sh->map->n; ++i)
                if (sh->map->f[i].off32 == at)
                {
                    const char* p = *reinterpret_cast<const char* const*>(sh->data + sh->map->f[i].off64);
                    return p ? p : "";
                }
            return "";
        }
    }
    if (AscDbc::Table* t = AscDbc::Owner(row))   // tabelas proprias do DLL (layout de arquivo)
        return t->Str(row, off);
#endif
    return *reinterpret_cast<const char* const*>(row + off);
}

// x64: a linha vem da tabela do proprio DLL (layout de arquivo = registro 12340, strings como offset: use Str).
uint8_t* AscClientDbc::Row(uint32_t container, uint32_t id)
{
#if ASC_X64
    const RecMap* m = MapOf(container);
    if (!m || !AscX64::Ptr(container))
        return nullptr;
    uint8_t** index64 = Index(container);
    if (!index64 || id < Min(container) || id > Max(container))
        return nullptr;
    const uint8_t* src = index64[id - Min(container)];
    return src ? ShadowOf(m, src) : nullptr;
#endif
    uint8_t** index = Index(container);
    if (!index || id < Min(container) || id > Max(container))
        return nullptr;
    return index[id - Min(container)];
}

bool AscClientDbc::Copy(uint32_t getter, uint32_t container, uint32_t id, void* out)
{
#if ASC_X64
    (void)getter;
    const RecMap* m = MapOf(container);
    const uint8_t* row = Row(container, id);
    if (!m || !row)
        return false;
    memcpy(out, row, m->size32);
    Shadow* sh = ByCopy()[row];
    ByCopy()[static_cast<uint8_t*>(out)] = sh;   // alias: Str(out, off) le as strings do registro x64
    return true;
#else
    typedef int(__thiscall* GetRec_t)(void*, uint32_t, void*);
    return reinterpret_cast<GetRec_t>(ASC_PTR(getter))(reinterpret_cast<void*>(ASC_PTR(container)), id, out) != 0;
#endif
}

uint32_t* AscClientDbc::Field(uint32_t container, uint32_t id, uint32_t off32)
{
#if ASC_X64
    const RecMap* m = MapOf(container);
    if (Compressed(container))
    {
        // registro guardado em RLE: escrever no lugar corromperia a linha comprimida
        AscLog::Printf("x64: AscClientDbc::Field(0x%06X, id %u, +0x%X) ignorado -- tabela comprimida (RLE)", container, id, off32);
        return nullptr;
    }
    uint8_t* rec = const_cast<uint8_t*>(static_cast<const uint8_t*>(Native(container, id)));
    if (!m || !rec)
        return nullptr;
    for (uint32_t i = 0; i < m->n; ++i)
        if (m->f[i].off32 == off32)
            return m->f[i].ptr ? nullptr : reinterpret_cast<uint32_t*>(rec + m->f[i].off64);
    return nullptr;
#else
    uint8_t* row = Row(container, id);
    return row ? reinterpret_cast<uint32_t*>(row + off32) : nullptr;
#endif
}

const void* AscClientDbc::Native(uint32_t container, uint32_t id)
{
#if ASC_X64
    if (!AscX64::Ptr(container))
        return nullptr;
    uint8_t** index64 = Index(container);
    if (!index64 || id < Min(container) || id > Max(container))
        return nullptr;
    const uint8_t* src = index64[id - Min(container)];
    if (src && Compressed(container))
    {
        // devolve a copia descomprimida (somente leitura) guardada na sombra
        const RecMap* m = MapOf(container);
        if (!m)
            return nullptr;
        ShadowOf(m, src);
        return BySrc()[src]->data;
    }
    return src;
#else
    return Row(container, id);
#endif
}

bool AscClientDbc::Overwrite(uint32_t container, const uint8_t* row32, const char* strings)
{
    const uint32_t id = *reinterpret_cast<const uint32_t*>(row32);
#if ASC_X64
    const RecMap* m = MapOf(container);
    if (!m || Compressed(container))
        return false;
    uint8_t* rec = const_cast<uint8_t*>(static_cast<const uint8_t*>(Native(container, id)));
    if (!rec)
        return false;
    for (uint32_t i = 0; i < m->n; ++i)
    {
        const FieldMap& f = m->f[i];
        uint32_t v;
        memcpy(&v, row32 + f.off32, 4);
        if (f.ptr)
            *reinterpret_cast<const char**>(rec + f.off64) = strings ? strings + v : nullptr;
        else
            memcpy(rec + f.off64, &v, 4);
    }
    return true;
#else
    (void)container, (void)id, (void)strings;   // so x64: no x86 o chamador copia a linha (Row + memcpy)
    return false;
#endif
}

void AscClientDbc::Insert(uint32_t container, uint8_t* row)
{
#if ASC_X64
    // a linha vem no layout do .dbc (strings = offset u32); o registro x64 tem char*. Precisa conversao por tipo
    // (AscClientDbcPatch usa NewRecord + PutField + InsertRecord).
    AscLog::Printf("x64: AscClientDbc::Insert(0x%06X, id %u) ignorado -- conversao de registro nao portada", container, *reinterpret_cast<const uint32_t*>(row));
    return;
#endif
    InsertRecord(container, row);
}

void AscClientDbc::InsertRecord(uint32_t container, uint8_t* row)
{
#if ASC_X64
    if (!AscX64::Ptr(container) || !Index(container))
        return;
#endif
    const uint32_t id = *reinterpret_cast<const uint32_t*>(row);
    const uint32_t oldMax = Max(container), oldMin = Min(container);
    if (id <= oldMax && id >= oldMin)
    {
        Index(container)[id - oldMin] = row;
        return;
    }
    const uint32_t lo = id < oldMin ? id : oldMin;
    const uint32_t hi = oldMax < id ? id : oldMax;
    const uint32_t size = (hi - lo + 1) * static_cast<uint32_t>(sizeof(uint8_t*));   // x86: (hi-lo)*4+4
    uint8_t** index = static_cast<uint8_t**>(
        reinterpret_cast<void*(__stdcall*)(size_t, const char*, int, uint32_t)>(ASC_PTR(0x76E540))(size, "WowClientDB.h", 0x178, 0));
    memset(index, 0, size);
#if ASC_X64
    // x64: copia ate o maximo antigo inclusive. O "< oldMax" do original perde a linha do id maximo -- com ids
    // novos chegando em ordem crescente (SMSG_PATCH 0x96B/0x92A), cada insercao apagava a anterior.
    for (uint32_t i = oldMin; i <= oldMax; ++i)
#else
    for (uint32_t i = oldMin; i < oldMax; ++i)   // the old maximum is not copied (original bug)
#endif
        if (i >= lo && i <= hi)
            if (uint8_t* r = Index(container)[i - oldMin])
                index[i - lo] = r;
    index[id - lo] = row;
#if ASC_X64
    // x64: so libera indice que NOS alocamos. O do cliente pode ser ponteiro no MEIO de um bloco: em
    // WowClientDB_RLE (Spell 0xAD49D0, ItemDisplayInfo 0xAD3DDC) m_recordsById = &m_records[m_numRecords]
    // (WowClientDB.h, Load do RLE) -- SMemFree nele corrompia o heap (teste 2 31/10: o jogo fecha sem Crash.txt
    // logo apos SMSG 0x96B). O indice original do cliente vaza uma vez por tabela; m_records segue valido.
    {
        static std::unordered_set<void*> ours;
        void* old = Index(container);
        if (ours.erase(old))
            reinterpret_cast<void(__stdcall*)(void*, const char*, uint32_t, uint32_t)>(ASC_PTR(0x76E5A0))(old, nullptr, 0, 0);
        ours.insert(index);
    }
#else
    reinterpret_cast<void(__stdcall*)(void*, const char*, uint32_t, uint32_t)>(ASC_PTR(0x76E5A0))(Index(container), nullptr, 0, 0);
#endif
    Index(container) = index;
    Min(container) = lo;
    Max(container) = hi;
}

#if ASC_X64
uint8_t* AscClientDbc::Raw(uint32_t container, uint32_t id)
{
    if (!AscX64::Ptr(container))
        return nullptr;
    uint8_t** index64 = Index(container);
    if (!index64 || id < Min(container) || id > Max(container))
        return nullptr;
    return index64[id - Min(container)];
}

bool AscClientDbc::IsCompressed(uint32_t container) { return Compressed(container); }

uint8_t* AscClientDbc::Writable(uint32_t container, uint32_t id)
{
    if (!MapOf(container) || Compressed(container))
        return nullptr;
    return Raw(container, id);
}

bool AscClientDbc::PutField(uint32_t container, uint8_t* rec, uint32_t off32, uintptr_t v)
{
    const RecMap* m = MapOf(container);
    if (!m || !rec)
        return false;
    for (uint32_t i = 0; i < m->n; ++i)
        if (m->f[i].off32 == off32)
        {
            if (m->f[i].ptr)
                memcpy(rec + m->f[i].off64, &v, sizeof v);
            else
            {
                const uint32_t v32 = static_cast<uint32_t>(v);
                memcpy(rec + m->f[i].off64, &v32, 4);
            }
            return true;
        }
    AscLog::Printf("x64: AscClientDbc::PutField(0x%06X, +0x%X) sem campo no mapa", container, off32);
    return false;
}

uint32_t AscClientDbc::RecordSize(uint32_t container, uint32_t size32)
{
    const RecMap* m = MapOf(container);
    return m ? m->size64 : size32;
}

uint8_t* AscClientDbc::NewRecord(uint32_t container, uint32_t size32)
{
    const RecMap* m = MapOf(container);
    if (!m)
        return nullptr;
    const uint32_t n = m->size64 > size32 ? m->size64 : size32;
    uint8_t* rec = static_cast<uint8_t*>(operator new(n));
    memset(rec, 0, n);
    return rec;
}
#else
uint8_t* AscClientDbc::Raw(uint32_t container, uint32_t id) { return Row(container, id); }
bool AscClientDbc::IsCompressed(uint32_t container) { return container == 0xAD49D0 && *reinterpret_cast<const uint8_t*>(ASC_PTR(0xC5DEA0)) != 0; }
uint8_t* AscClientDbc::Writable(uint32_t container, uint32_t id) { return Row(container, id); }
bool AscClientDbc::PutField(uint32_t, uint8_t* rec, uint32_t off32, uintptr_t v)
{
    const uint32_t v32 = static_cast<uint32_t>(v);
    memcpy(rec + off32, &v32, 4);
    return true;
}
uint32_t AscClientDbc::RecordSize(uint32_t, uint32_t size32) { return size32; }
uint8_t* AscClientDbc::NewRecord(uint32_t, uint32_t size32)
{
    uint8_t* rec = static_cast<uint8_t*>(operator new(size32));
    memset(rec, 0, size32);
    return rec;
}
#endif
