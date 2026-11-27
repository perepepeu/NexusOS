#include <stddef.h>
#include <stdbool.h>
#include <interrupts.h>
#include <io.h>
#include <log.h>
#include <panic.h>
#include <keyboard.h>
#include <process.h>

struct descriptor_pointer { uint16_t limit; uint64_t base; } __attribute__((packed));
struct tss64 {
    uint32_t reserved0;
    uint64_t rsp[3], reserved1, ist[7], reserved2;
    uint16_t reserved3, iomap;
} __attribute__((packed));
struct idt_gate {
    uint16_t low, selector;
    uint8_t ist, attributes;
    uint16_t middle;
    uint32_t high, reserved;
} __attribute__((packed));
_Static_assert(sizeof(struct tss64) == 104, "Tamanho do TSS");
_Static_assert(sizeof(struct idt_gate) == 16, "Tamanho do gate IDT");
_Static_assert(offsetof(struct interrupt_frame, vector) == 120, "ABI Assembly/C");

static uint64_t gdt[5] __attribute__((aligned(16)));
static struct tss64 tss;
static struct idt_gate idt[256] __attribute__((aligned(16)));
static uint8_t privilege_stack[16384] __attribute__((aligned(16)));
static uint8_t double_fault_stack[16384] __attribute__((aligned(16)));
static uint8_t nmi_stack[16384] __attribute__((aligned(16)));
static volatile uint64_t ticks, breakpoints;
static uint64_t apic_pdpt[512] __attribute__((aligned(4096)));
static uint64_t apic_pd[512] __attribute__((aligned(4096)));
static uint64_t apic_pt[512] __attribute__((aligned(4096)));
extern void *isr_table[256];
extern void gdt_load(const struct descriptor_pointer *pointer);
extern unsigned int interrupt_register_test(void);

static void gdt_init(void) {
    gdt[1] = 0x00af9a000000ffff; /* Ring 0: codigo 64 bits. */
    gdt[2] = 0x00cf92000000ffff; /* Ring 0: dados. */
    tss.rsp[0] = (uintptr_t)(privilege_stack + sizeof privilege_stack);
    tss.ist[0] = (uintptr_t)(double_fault_stack + sizeof double_fault_stack);
    tss.ist[1] = (uintptr_t)(nmi_stack + sizeof nmi_stack);
    tss.iomap = sizeof tss; /* Sem permissao de I/O em ring 3. */
    uint64_t base = (uintptr_t)&tss;
    uint64_t limit = sizeof tss - 1;
    gdt[3] = (limit & 0xffff) | ((base & 0xffffff) << 16)
        | (0x89ull << 40) | (((limit >> 16) & 15) << 48) | (((base >> 24) & 255) << 56);
    gdt[4] = base >> 32;
    struct descriptor_pointer pointer = {sizeof gdt - 1, (uintptr_t)gdt};
    gdt_load(&pointer);
    log_info("gdt", "GDT e TSS carregadas. CS=0x08 DS=0x10 TR=0x18.");
}

static void idt_init(void) {
    for (unsigned int i = 0; i < 256; ++i) {
        uint64_t address = (uintptr_t)isr_table[i];
        idt[i] = (struct idt_gate){
            .low = address, .selector = 0x08,
            .ist = i == 8 ? 1 : i == 2 ? 2 : 0,
            .attributes = 0x8e, .middle = address >> 16, .high = address >> 32,
        };
    }
    struct descriptor_pointer pointer = {sizeof idt - 1, (uintptr_t)idt};
    __asm__ volatile ("lidt %0" : : "m"(pointer) : "memory");
    log_info("idt", "IDT carregada: 256 vetores. IST para double fault e NMI.");
}

static void pic_write(uint16_t port, uint8_t value) {
    outb(port, value);
    outb(0x80, 0); /* Espera de I/O durante a inicializacao do PIC. */
}

static uint64_t read_msr(uint32_t index) {
    uint32_t low, high;
    __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(index));
    return ((uint64_t)high << 32) | low;
}

static void write_msr(uint32_t index, uint64_t value) {
    __asm__ volatile ("wrmsr" : : "a"((uint32_t)value), "d"((uint32_t)(value >> 32)),
        "c"(index) : "memory");
}

static void legacy_route(uint64_t hhdm, uint64_t physical, uint64_t virtual) {
    /* PIC em virtual wire: o LAPIC encaminha ExtINT; as IRQs continuam no PIC. */
    uint32_t a, b, c, d;
    __asm__ volatile ("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
    if (d & (1u << 9)) {
        uint64_t base = read_msr(0x1b);
        if (base & (1u << 10)) {
            write_msr(0x808, 0);
            write_msr(0x80f, 0x1ff);
            write_msr(0x835, 0x700);
            write_msr(0x836, 0x400);
        } else {
            KASSERT(base & (1u << 11));
            uintptr_t cr3, cr4;
            __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
            __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
            KASSERT(!(cr4 & (1u << 12))); /* Esta pagina usa tabelas de 4 niveis. */
            uint64_t *pml4 = (uint64_t *)(hhdm + (cr3 & 0x000ffffffffff000ull));
            const uintptr_t mmio = 0xfffffe0000000000ull;
            unsigned int slot = (mmio >> 39) & 511;
            KASSERT(pml4[slot] == 0);
            apic_pt[0] = (base & 0x000ffffffffff000ull) | 0x1b; /* RW, PCD, PWT. */
            apic_pd[0] = ((uintptr_t)apic_pt - virtual + physical) | 3;
            apic_pdpt[0] = ((uintptr_t)apic_pd - virtual + physical) | 3;
            pml4[slot] = ((uintptr_t)apic_pdpt - virtual + physical) | 3;
            __asm__ volatile ("invlpg (%0)" : : "r"(mmio) : "memory");
            volatile uint32_t *lapic = (volatile uint32_t *)mmio;
            lapic[0x80 / 4] = 0;
            lapic[0xf0 / 4] = 0x1ff;
            lapic[0x350 / 4] = 0x700;
            lapic[0x360 / 4] = 0x400;
        }
        outb(0x22, 0x70); outb(0x23, 1);
        log_info("irq", "LAPIC: ExtINT virtual wire para o PIC.");
    } else {
        outb(0x22, 0x70); outb(0x23, 0);
    }
}

static void pic_init(void) {
    pic_write(0x21, 0xff); pic_write(0xa1, 0xff);
    pic_write(0x20, 0x11); pic_write(0xa0, 0x11);
    pic_write(0x21, 0x20); pic_write(0xa1, 0x28);
    pic_write(0x21, 0x04); pic_write(0xa1, 0x02);
    pic_write(0x21, 0x01); pic_write(0xa1, 0x01);
    pic_write(0x21, 0xfe); pic_write(0xa1, 0xff); /* Apenas IRQ0 habilitada. */
    unsigned int divisor = 1193182 / SCHEDULER_PIT_HZ;
    outb(0x43, 0x36);
    outb(0x40, divisor & 255);
    outb(0x40, divisor >> 8);
    log_info("irq", "PIC remapeado: 32-47. PIT: 100 Hz, IRQ0 habilitada.");
}

static bool spurious(unsigned int irq) {
    if (irq == 7) { outb(0x20, 0x0b); return !(inb(0x20) & 0x80); }
    if (irq == 15) {
        outb(0xa0, 0x0b);
        if (!(inb(0xa0) & 0x80)) { outb(0x20, 0x20); return true; }
    }
    return false;
}

void interrupt_dispatch(struct interrupt_frame *frame) {
    if (frame->vector == 255) return; /* Spurious LAPIC: nao envia EOI. */
    if (frame->vector >= 32 && frame->vector < 48) {
        unsigned int irq = frame->vector - 32;
        if (spurious(irq)) return;
        if (irq == 0) { ++ticks; process_on_tick(ticks); }
        if (irq == 1) keyboard_irq();
        if (irq >= 8) outb(0xa0, 0x20);
        outb(0x20, 0x20);
        if (irq == 0) process_preempt(); /* EOI antes da troca: a proxima tarefa pode habilitar IRQs. */
        return;
    }
    if (frame->vector == 3) {
        ++breakpoints;
        log_info("idt", "Breakpoint INT3 tratado; retornando por IRETQ.");
        return;
    }
    uintptr_t cr2;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
    if (frame->vector == 8) {
        KASSERT((uintptr_t)frame >= (uintptr_t)double_fault_stack &&
            (uintptr_t)frame < (uintptr_t)(double_fault_stack + sizeof double_fault_stack));
    }
    static const char *const names[32] = {
        "Divide error", "Debug", "NMI", "Breakpoint", "Overflow", "Bound range",
        "Invalid opcode", "Device not available", "Double fault", "Coprocessor segment",
        "Invalid TSS", "Segment not present", "Stack fault", "General protection",
        "Page fault", "Reserved", "x87 exception", "Alignment check", "Machine check",
        "SIMD exception", "Virtualization", "Control protection", "Reserved", "Reserved",
        "Reserved", "Reserved", "Reserved", "Reserved", "Hypervisor injection",
        "VMM communication", "Security exception", "Reserved",
    };
    struct panic_context context = {
        .vector = frame->vector, .error = frame->error, .rip = frame->rip,
        .rsp = frame->rsp, .rflags = frame->rflags, .cr2 = cr2,
    };
    kernel_exception(frame->vector < 32 ? names[frame->vector] : "Interrupcao inesperada", &context);
}

void interrupts_init(uint64_t hhdm, uint64_t physical, uint64_t virtual) {
    __asm__ volatile ("cli" : : : "memory");
    gdt_init();
    idt_init();
    legacy_route(hhdm, physical, virtual);
    pic_init();
    __asm__ volatile ("sti" : : : "memory");
    log_info("irq", "Interrupcoes habilitadas.");
}

uint64_t timer_ticks(void) { return ticks; }

void interrupts_enable_irq(unsigned int irq) {
    KASSERT(irq < 16);
    uint64_t flags = irq_save();
    uint16_t port = irq < 8 ? 0x21 : 0xa1;
    outb(port, inb(port) & ~(1u << (irq % 8)));
    if (irq >= 8) outb(0x21, inb(0x21) & ~(1u << 2));
    irq_restore(flags);
}

void interrupts_disable_irq(unsigned int irq) {
    KASSERT(irq < 16);
    uint64_t flags = irq_save();
    uint16_t port = irq < 8 ? 0x21 : 0xa1;
    outb(port, inb(port) | (1u << (irq % 8)));
    irq_restore(flags);
}

void timer_wait(unsigned int count) {
    uint64_t start = ticks;
    while (ticks - start < count) __asm__ volatile ("hlt" : : : "memory");
}

void interrupts_selftest(void) {
    struct descriptor_pointer gdtr, idtr;
    uint16_t cs, ds, tr;
    __asm__ volatile ("sgdt %0" : "=m"(gdtr));
    __asm__ volatile ("sidt %0" : "=m"(idtr));
    __asm__ volatile ("mov %%cs, %0" : "=r"(cs));
    __asm__ volatile ("mov %%ds, %0" : "=r"(ds));
    __asm__ volatile ("str %0" : "=r"(tr));
    KASSERT(gdtr.base == (uintptr_t)gdt && gdtr.limit == sizeof gdt - 1);
    KASSERT(idtr.base == (uintptr_t)idt && idtr.limit == sizeof idt - 1);
    KASSERT(cs == 8 && ds == 16 && tr == 24);
    uint64_t before = breakpoints;
    KASSERT(interrupt_register_test());
    KASSERT(breakpoints == before + 1);
    timer_wait(5);
    log_info("test", "GDT IDT INT3 registros e PIT: OK. ticks=%u", (unsigned int)ticks);
}

__attribute__((noreturn)) void interrupts_idle(void) {
    for (;;) __asm__ volatile ("sti; hlt" : : : "memory");
}

__attribute__((noreturn)) void interrupts_test_double_fault(void) {
    /* #GP com gate ausente provoca #NP durante a entrega, promovida a #DF. */
    __asm__ volatile ("cli" : : : "memory");
    idt[13].attributes = 0;
    __asm__ volatile ("mov $0xfff8, %%ax; mov %%ax, %%ds" : : : "rax", "memory");
    PANIC(ERR_SELFTEST, "Double fault de teste nao foi gerado.");
}
