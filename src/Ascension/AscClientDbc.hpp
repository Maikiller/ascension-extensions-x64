#pragma once
// The client's WowClientDB containers (Ascension.exe 12340): +0xC max id, +0x10 min id, +0x20 the row pointer
// index. The original DLL reads and extends them with ~100 identical per-table functions -- the getters
// FUN_100b2500-style and the inserters FUN_100ae510-style (tools/client_dbc_map.py names the containers).
#include <cstdint>

namespace AscClientDbc
{
    // The getters: the row with this id, or nullptr (index missing, id out of [min, max], empty slot).
    uint8_t* Row(uint32_t container, uint32_t id);

    // The inserters: store `row` (its first dword is the id). Inside [min, max] it takes the slot; outside,
    // a new index spanning the old range and the id is allocated (SMemAlloc 0x76E540, "WowClientDB.h" line
    // 0x178), the old rows are copied -- ids min .. max-1 only, as in the original, so the row that held
    // the old maximum is lost (IMPROVEMENTS.md) -- and the old index freed (SMemFree 0x76E5A0).
    void Insert(uint32_t container, uint8_t* row);

    // Limites de id da tabela (x86: +0x10 / +0xC do container).
    uint32_t MinId(uint32_t container);
    uint32_t MaxId(uint32_t container);

    // Coluna string `off` de uma linha. x86: o char* do registro. x64: as linhas de DBC do cliente vem das tabelas
    // do proprio DLL no layout de arquivo (= layout do registro 12340), e a string e resolvida no bloco dela; se a
    // linha nao for de uma tabela dessas, le o char* cru (outras estruturas).
    const char* Str(const void* row, uint32_t off);

    // Copia o registro `id` para `out` no layout 12340 (x86) -- o WowClientDB::GetRecord(id, rec*) do cliente
    // (`getter`, ex. 0x4CFD90 ItemDisplayInfo, 0x4CFD20 Spell). x64: o cliente nao tem essa funcao; copia a
    // linha-sombra e registra `out` para Str() resolver as strings dele (out precisa ter o tamanho x86 do registro).
    bool Copy(uint32_t getter, uint32_t container, uint32_t id, void* out);

    // Campo de 4 bytes (offset x86 `off32`) do registro `id`, no registro real do cliente -- para ESCREVER (a linha
    // devolvida por Row e uma copia no x64). nullptr se nao ha linha ou o campo e ponteiro/desconhecido.
    uint32_t* Field(uint32_t container, uint32_t id, uint32_t off32);

    // O registro do proprio cliente (x64: layout x64, para repassar a funcoes do cliente). x86: o mesmo que Row.
    const void* Native(uint32_t container, uint32_t id);

    // Sobrescreve o registro do cliente com o id da 1a coluna de `row32` (layout 12340, colunas de string como
    // offset u32 em `strings`; offset com `strings` nulo vira nullptr), inclusive os ponteiros de string.
    // So x64 (campo a campo pelo mapa x86 -> x64); no x86 devolve false e o chamador faz Row + memcpy.
    // False sem linha, sem mapa ou em tabela comprimida (RLE).
    bool Overwrite(uint32_t container, const uint8_t* row32, const char* strings);

    // Escrita por campo no layout 12340 (os SMSG_PATCH_* de AscClientDbcPatch). x86: o registro e o proprio
    // layout (Row / operator new + Insert). x64: o registro do cliente tem outro layout -- cada campo de 4 bytes
    // x86 vai para o seu offset x64; coluna ponteiro (string) recebe o char* inteiro (`v` e uintptr_t).
    //   Writable: registro do cliente para escrever (nullptr sem linha, sem mapa ou tabela RLE comprimida);
    //   NewRecord: registro novo zerado no tamanho do cliente (x64: size64 do mapa), para PutField + InsertRecord;
    //   InsertRecord: o Insert com registro ja no layout do cliente.
    //   RecordSize: tamanho do registro do cliente (x64 size64; x86 = `size32`).
    uint8_t* Writable(uint32_t container, uint32_t id);
    bool PutField(uint32_t container, uint8_t* rec, uint32_t off32, uintptr_t v);
    uint8_t* NewRecord(uint32_t container, uint32_t size32);
    void InsertRecord(uint32_t container, uint8_t* rec);
    uint32_t RecordSize(uint32_t container, uint32_t size32);
    // x64: o registro guardado no indice, cru (RLE comprimido quando a tabela e comprimida). x86: Row.
    uint8_t* Raw(uint32_t container, uint32_t id);
    bool IsCompressed(uint32_t container);
}
