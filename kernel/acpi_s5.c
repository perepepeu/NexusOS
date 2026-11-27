#include <acpi_s5.h>

/* Leitor limitado de AML constante. Nao executa metodos nem pesquisa dentro
   de buffers/metodos: um texto _S5_ nesses objetos nao e um objeto de energia. */
static bool package(const uint8_t **cursor, const uint8_t *end, const uint8_t **stop) {
    const uint8_t *start = *cursor;
    if (start >= end) return false;
    unsigned int extra = *start >> 6;
    if ((size_t)(end - start) < extra + 1) return false;
    uint32_t length = *start & (extra ? 15 : 63);
    for (unsigned int i = 0; i < extra; ++i) length |= (uint32_t)start[i + 1] << (4 + 8 * i);
    if (length < extra + 1 || (size_t)(end - start) < length) return false;
    *cursor = start + extra + 1;
    *stop = start + length;
    return true;
}

static bool integer(const uint8_t **cursor, const uint8_t *end, uint64_t *value) {
    if (*cursor >= end) return false;
    uint8_t opcode = *(*cursor)++;
    if (opcode == 0 || opcode == 1 || opcode == 0xff) {
        *value = opcode == 0xff ? UINT64_MAX : opcode;
        return true;
    }
    unsigned int count = opcode == 0x0a ? 1 : opcode == 0x0b ? 2 :
        opcode == 0x0c ? 4 : opcode == 0x0e ? 8 : 0;
    if (!count || (size_t)(end - *cursor) < count) return false;
    *value = 0;
    for (unsigned int i = 0; i < count; ++i) *value |= (uint64_t)*(*cursor)++ << (8 * i);
    return true;
}

static bool name(const uint8_t **cursor, const uint8_t *end, bool *root, bool *s5) {
    const uint8_t *p = *cursor;
    bool absolute = p < end && *p == '\\';
    if (absolute) ++p;
    bool parent = false;
    while (p < end && *p == '^') { ++p; parent = true; }
    if (p >= end) return false;
    unsigned int segments = 1;
    if (*p == 0) { ++p; segments = 0; }
    else if (*p == 0x2e) { ++p; segments = 2; }
    else if (*p == 0x2f) { if (++p >= end) return false; segments = *p++; if (!segments) return false; }
    if (segments > 32 || (size_t)(end - p) < segments * 4) return false;
    *root = absolute && !segments;
    *s5 = !parent && segments == 1 && p[0] == '_' && p[1] == 'S' && p[2] == '5' && p[3] == '_';
    for (unsigned int i = 0; i < segments * 4; ++i) {
        uint8_t c = p[i];
        if (!(c == '_' || (c >= 'A' && c <= 'Z') || (i % 4 && c >= '0' && c <= '9'))) return false;
    }
    *cursor = p + segments * 4;
    return true;
}

static bool skip_data(const uint8_t **cursor, const uint8_t *end) {
    if (*cursor >= end) return false;
    uint8_t opcode = **cursor;
    if (opcode == 0x11 || opcode == 0x12 || opcode == 0x13) {
        ++*cursor;
        const uint8_t *stop;
        if (!package(cursor, end, &stop)) return false;
        *cursor = stop;
        return true;
    }
    if (opcode == 0x0d) {
        ++*cursor;
        while (*cursor < end && **cursor) ++*cursor;
        if (*cursor >= end) return false;
        ++*cursor;
        return true;
    }
    const uint8_t *saved = *cursor;
    uint64_t ignored;
    if (integer(cursor, end, &ignored)) return true;
    *cursor = saved;
    bool root, s5;
    return name(cursor, end, &root, &s5);
}

static bool walk(const uint8_t *p, const uint8_t *end, unsigned int depth, uint8_t *a, uint8_t *b) {
    if (depth > 8) return false;
    while (p < end) {
        uint8_t opcode = *p++;
        bool root, s5;
        if (opcode == 0x08) {
            if (!name(&p, end, &root, &s5)) return false;
            if (s5) {
                if (p >= end || *p++ != 0x12) return false;
                const uint8_t *stop;
                uint64_t first, second;
                if (!package(&p, end, &stop) || p >= stop) return false;
                unsigned int elements = *p++;
                if (elements < 2 ||
                        !integer(&p, stop, &first) || !integer(&p, stop, &second) ||
                        first > 7 || second > 7) return false;
                for (unsigned int i = 2; i < elements; ++i) {
                    uint64_t ignored;
                    if (!integer(&p, stop, &ignored)) return false;
                }
                if (p != stop) return false;
                *a = first; *b = second;
                return true;
            }
            if (!skip_data(&p, end)) return false;
        } else if (opcode == 0x10 || opcode == 0x14 || opcode == 0xa0 || opcode == 0xa1 || opcode == 0xa2) {
            const uint8_t *stop;
            if (!package(&p, end, &stop)) return false;
            if (opcode == 0x10) {
                if (!name(&p, stop, &root, &s5)) return false;
                if (root && walk(p, stop, depth + 1, a, b)) return true;
            }
            p = stop;
        } else if (opcode == 0x5b) {
            if (p >= end) return false;
            uint8_t extended = *p++;
            if (extended == 0x80) {
                if (!name(&p, end, &root, &s5) || p >= end) return false;
                ++p;
                if (!skip_data(&p, end) || !skip_data(&p, end)) return false;
            } else if (extended == 1 || extended == 2) {
                if (!name(&p, end, &root, &s5)) return false;
                if (extended == 1) { if (p >= end) return false; ++p; }
            } else if (extended >= 0x81 && extended <= 0x87) {
                const uint8_t *stop;
                if (!package(&p, end, &stop)) return false;
                p = stop;
            } else return false;
        } else if (opcode == 0x06) {
            if (!name(&p, end, &root, &s5) || !name(&p, end, &root, &s5)) return false;
        } else if (opcode == 0x15) {
            if (!name(&p, end, &root, &s5) || end - p < 2) return false;
            p += 2;
        } else if (opcode != 0xa3) return false;
    }
    return false;
}

bool acpi_find_s5(const uint8_t *aml, size_t size, uint8_t *a, uint8_t *b) {
    if (!aml || !a || !b || size > 1024 * 1024) return false;
    return walk(aml, aml + size, 0, a, b);
}
