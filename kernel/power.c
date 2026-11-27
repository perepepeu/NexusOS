#include <stddef.h>
#include <acpi_s5.h>
#include <interrupts.h>
#include <io.h>
#include <log.h>
#include <power.h>
#include <process.h>

static uint64_t direct_map;
static const struct limine_memmap_response *memory_map;
static uint16_t reset_port, pm1a, pm1b, smi_port;
static uint8_t reset_value, acpi_enable, sleep_a, sleep_b;
static bool sleep_supported;

static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint64_t u64(const uint8_t *p) { return u32(p) | (uint64_t)u32(p + 4) << 32; }
static bool signature(const uint8_t *p, const char *text, unsigned int size) {
    for (unsigned int i = 0; i < size; ++i) if (p[i] != (uint8_t)text[i]) return false;
    return true;
}
static bool checksum(const uint8_t *p, uint32_t size) {
    uint8_t sum = 0;
    for (uint32_t i = 0; i < size; ++i) sum += p[i];
    return sum == 0;
}

static const uint8_t *mapped(uint64_t physical, uint64_t size) {
    if (!physical || !size || !memory_map || memory_map->entry_count > 4096 ||
            physical > UINT64_MAX - size || physical + size > UINT64_MAX - direct_map) return 0;
    uint64_t cursor = physical, end = physical + size;
    while (cursor < end) {
        uint64_t next = cursor;
        for (uint64_t i = 0; i < memory_map->entry_count; ++i) {
            const struct limine_memmap_entry *entry = memory_map->entries[i];
            if (entry->type != LIMINE_MEMMAP_ACPI_RECLAIMABLE && entry->type != LIMINE_MEMMAP_ACPI_NVS &&
                    entry->type != LIMINE_MEMMAP_RESERVED_MAPPED) continue;
            if (entry->base <= cursor && entry->length <= UINT64_MAX - entry->base &&
                    cursor < entry->base + entry->length) { next = entry->base + entry->length; break; }
        }
        if (next == cursor) return 0;
        cursor = next < end ? next : end;
    }
    return (const uint8_t *)(uintptr_t)(direct_map + physical);
}

static const uint8_t *table(uint64_t physical, const char *expected) {
    const uint8_t *header = mapped(physical, 36);
    if (!header || !signature(header, expected, 4)) return 0;
    uint32_t size = u32(header + 4);
    if (size < 36 || size > 1024 * 1024) return 0;
    const uint8_t *full = mapped(physical, size);
    return full && checksum(full, size) ? full : 0;
}

static uint16_t gas_port(const uint8_t *gas, uint8_t width) {
    uint64_t address = u64(gas + 4);
    return gas[0] == 1 && gas[1] == width && gas[2] == 0 &&
        (gas[3] == 0 || gas[3] == (width == 8 ? 1 : 2)) && address && address <= 0xffff
        ? (uint16_t)address : 0;
}

void power_init(const void *rsdp_address, uint64_t hhdm, const struct limine_memmap_response *map) {
    direct_map = hhdm; memory_map = map;
    reset_port = pm1a = pm1b = smi_port = 0;
    sleep_supported = false;
    if ((uintptr_t)rsdp_address < hhdm) goto unavailable;
    const uint8_t *rsdp = mapped((uintptr_t)rsdp_address - hhdm, 20);
    if (!rsdp || !signature(rsdp, "RSD PTR ", 8) || !checksum(rsdp, 20)) goto unavailable;
    const uint8_t *root = 0;
    unsigned int entry_size = 4;
    if (rsdp[15] >= 2) {
        rsdp = mapped((uintptr_t)rsdp_address - hhdm, 36);
        if (!rsdp) goto unavailable;
        uint32_t size = u32(rsdp + 20);
        if (size < 36 || size > 4096 || !mapped((uintptr_t)rsdp_address - hhdm, size) ||
                !checksum(rsdp, size)) goto unavailable;
        root = table(u64(rsdp + 24), "XSDT");
        if (root) entry_size = 8;
    }
    if (!root) root = table(u32(rsdp + 16), "RSDT");
    if (!root || (u32(root + 4) - 36) % entry_size) goto unavailable;
    const uint8_t *fadt = 0;
    for (uint32_t i = 36; i < u32(root + 4); i += entry_size) {
        uint64_t address = entry_size == 8 ? u64(root + i) : u32(root + i);
        fadt = table(address, "FACP");
        if (fadt) break;
    }
    if (!fadt || u32(fadt + 4) < 116) goto unavailable;
    uint32_t size = u32(fadt + 4), flags = u32(fadt + 112);
    if (size >= 129 && (flags & (1u << 10))) {
        reset_port = gas_port(fadt + 116, 8);
        reset_value = fadt[128];
    }
    if (flags & (1u << 20)) goto unavailable; /* HW-reduced requer outro backend. */
    uint32_t a = u32(fadt + 64), b = u32(fadt + 68), smi = u32(fadt + 48);
    if (fadt[89] >= 2) { if (a && a <= 0xffff) pm1a = a; if (b && b <= 0xffff) pm1b = b; }
    if (!pm1a && size >= 184) pm1a = gas_port(fadt + 172, 16);
    if (!pm1b && size >= 196) pm1b = gas_port(fadt + 184, 16);
    if (smi && smi <= 0xffff) smi_port = smi;
    acpi_enable = fadt[52];
    uint64_t dsdt_address = size >= 148 && u64(fadt + 140) ? u64(fadt + 140) : u32(fadt + 40);
    const uint8_t *dsdt = table(dsdt_address, "DSDT");
    bool b_present = b || (size >= 196 && u64(fadt + 188));
    sleep_supported = pm1a && (!b_present || pm1b) && dsdt &&
        acpi_find_s5(dsdt + 36, u32(dsdt + 4) - 36, &sleep_a, &sleep_b);
    log_info("power", "ACPI: reset=%s S5=%s", reset_port ? "IO" : "PS/2 fallback",
        sleep_supported ? "pronto" : "indisponivel");
    return;
unavailable:
    log_warn("power", "ACPI ausente ou formato nao suportado; shutdown indisponivel.");
}

static void flush_serial(void) {
    for (unsigned int i = 0; i < 1000000; ++i) if (inb(0x3fd) & 0x40) return;
}

static bool reboot_unlocked(void) {
    log_info("power", "Reinicializacao solicitada."); flush_serial();
    if (reset_port) {
        log_info("power", "Reset ACPI via I/O."); flush_serial();
        outb(reset_port, reset_value);
        timer_wait(10);
    }
    log_warn("power", "Reset alternativo via controlador PS/2."); flush_serial();
    for (unsigned int i = 0; i < 1000000; ++i) {
        if (!(inb(0x64) & 2)) { outb(0x64, 0xfe); timer_wait(10); break; }
    }
    log_error("power", "Firmware nao executou a reinicializacao.");
    return false;
}

static bool shutdown_unlocked(void) {
    if (!sleep_supported) { log_error("power", "Desligamento ACPI S5 indisponivel."); return false; }
    if (!(inw(pm1a) & 1)) {
        if (!smi_port || !acpi_enable) return false;
        outb(smi_port, acpi_enable);
        unsigned int i;
        for (i = 0; i < 100 && !(inw(pm1a) & 1); ++i) timer_wait(1);
        if (i == 100) { log_error("power", "Timeout ao habilitar ACPI."); return false; }
    }
    log_info("power", "Desligamento solicitado: ACPI S5."); flush_serial();
    outw(pm1a, (inw(pm1a) & ~0x3c00u) | ((uint16_t)sleep_a << 10) | (1u << 13));
    if (pm1b) outw(pm1b, (inw(pm1b) & ~0x3c00u) | ((uint16_t)sleep_b << 10) | (1u << 13));
    timer_wait(10);
    log_error("power", "Firmware nao concluiu o desligamento.");
    return false;
}
bool power_reboot(void) {
    process_preempt_disable(); bool result = reboot_unlocked(); process_preempt_enable(); return result;
}
bool power_shutdown(void) {
    process_preempt_disable(); bool result = shutdown_unlocked(); process_preempt_enable(); return result;
}
