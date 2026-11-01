"""Espelho do AscX64::RelFields/RebaseAt (AscX64.cpp): campos rel32 (E8/E9/0F 8x) e disp32 RIP-relative.
rebase(ref, live) -> (ref com os campos trocados pelos vivos de mesmo formato e |dif| <= 1 MB, n trocados)."""
import struct

PFX = {0x66, 0xF2, 0xF3, 0xF0, 0x2E, 0x3E, 0x26, 0x36, 0x64, 0x65}


def fields(b):
    n = len(b); i = 0; out = []
    while i < n:
        start = i; op16 = rexw = False
        while i < n and b[i] in PFX:
            op16 |= b[i] == 0x66; i += 1
        if i < n and b[i] & 0xF0 == 0x40:
            rexw = bool(b[i] & 8); i += 1
        if i >= n:
            break
        op = b[i]; i += 1; modrm = False; imm = 0; rel = -1
        if op == 0x0F:
            if i >= n: break
            o2 = b[i]; i += 1
            if 0x80 <= o2 <= 0x8F: rel = i
            elif o2 == 0x38:
                if i >= n: break
                i += 1; modrm = True
            elif o2 == 0x3A:
                if i >= n: break
                i += 1; modrm = True; imm = 1
            elif (0x10 <= o2 <= 0x1F or 0x28 <= o2 <= 0x2F or 0x40 <= o2 <= 0x6F or 0x74 <= o2 <= 0x7F or 0x90 <= o2 <= 0x9F
                  or o2 in (0xA3, 0xA5, 0xAB, 0xAD, 0xAF, 0xB0, 0xB1, 0xB3, 0xB6, 0xB7) or 0xBB <= o2 <= 0xC1 or o2 == 0xC7
                  or 0xD0 <= o2 <= 0xFE):
                modrm = True
            elif 0x70 <= o2 <= 0x73 or o2 in (0xA4, 0xAC, 0xBA, 0xC2) or 0xC4 <= o2 <= 0xC6:
                modrm = True; imm = 1
            else:
                break
        elif op in (0xE8, 0xE9): rel = i
        elif op == 0x90 or 0x50 <= op <= 0x5F or op in (0xC3, 0xCC, 0x98, 0x99, 0xC9): pass
        elif 0x70 <= op <= 0x7F or op in (0xEB, 0x6A, 0xA8) or 0xB0 <= op <= 0xB7 or (op < 0x40 and op & 7 == 4): i += 1
        elif op in (0x68, 0xA9) or (op < 0x40 and op & 7 == 5): i += 2 if op16 and op != 0x68 else 4
        elif 0xB8 <= op <= 0xBF: i += 8 if rexw else (2 if op16 else 4)
        elif ((op < 0x40 and op & 7 < 4) or op in (0x63, 0x8D, 0x8F, 0xFE, 0xFF) or 0x84 <= op <= 0x8B or 0xD0 <= op <= 0xD3
              or 0xD8 <= op <= 0xDF):
            modrm = True
        elif op in (0x80, 0x83, 0xC0, 0xC1, 0xC6, 0x6B): modrm = True; imm = 1
        elif op in (0x81, 0x69, 0xC7): modrm = True; imm = 2 if op16 else 4
        elif op in (0xF6, 0xF7):
            if i >= n: break
            modrm = True
            if (b[i] >> 3) & 7 < 2: imm = 1 if op == 0xF6 else (2 if op16 else 4)
        else:
            break
        if rel >= 0:
            if rel + 4 <= n: out.append((start, rel))
            i = rel + 4; continue
        if modrm:
            if i >= n: break
            m = b[i]; i += 1; mod, rm = m >> 6, m & 7
            if mod != 3 and rm == 4:
                if i >= n: break
                sib = b[i]; i += 1
                if mod == 0 and sib & 7 == 5: i += 4
            if mod == 0 and rm == 5:
                if i + 4 <= n: out.append((start, i))
                i += 4
            elif mod == 1: i += 1
            elif mod == 2: i += 4
            i += imm
    return out


def rebase(ref, live):
    ref = bytearray(ref); live = bytes(live); k = len(ref)
    fl = set(fields(live[:k])); fixed = 0
    for s, f in fields(bytes(ref)):
        if (s, f) not in fl or ref[s:f] != live[s:f]:
            continue
        a = struct.unpack_from('<i', ref, f)[0]; b = struct.unpack_from('<i', live, f)[0]
        if a != b and abs(b - a) <= 0x100000:
            ref[f:f + 4] = live[f:f + 4]; fixed += 1
    return bytes(ref), fixed
