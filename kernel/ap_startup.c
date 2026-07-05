#include "ap_startup.h"
#include "cpu/cpu.h"
#include "cpu/gdt.h"
#include "cpu/idt.h"
#include "apic/lapic.h"
#include "memory/pfa.h"
#include "memory/paging.h"
#include "task.h"
#include "lib.h"
#include "drivers/serial.h"
#include "scheduler/scheduler.h"

/* ── Trampoline binary embedded via ld -r -b binary ── */
extern char _binary_build_ap_trampoline_bin_start[];
extern char _binary_build_ap_trampoline_bin_end[];
#define TRAMPOLINE_SIZE  ((unsigned int)(_binary_build_ap_trampoline_bin_end - _binary_build_ap_trampoline_bin_start))
#define TRAMPOLINE_ADDR  0x70000U
#define TRAMPOLINE_DATA  (TRAMPOLINE_ADDR + 0x200)

void ap_main(unsigned int apic_id)
{
    /* Install shared IDT early */
    idt_install();

    /* Find cpu_info slot by APIC ID */
    int cpu_id = -1;
    for (int i = 0; i < cpu_count; i++) {
        if (cpu_info[i].apic_id == apic_id) {
            cpu_id = i;
            break;
        }
    }
    if (cpu_id < 0) {
        serial_write_string("[ap] unknown APIC ID ");
        serial_write_hex(apic_id);
        serial_write_string(" -- halting\n");
        __asm__ volatile ("cli; hlt");
        while (1);
    }

    serial_write_string("[ap] CPU ");
    serial_write_int(cpu_id);
    serial_write_string(" (APIC ");
    serial_write_int(apic_id);
    serial_write_string(") starting\n");

    /* Allocate 8KB stack */
    char *stack = (char *)alloc_frames(2);
    if (!stack) {
        serial_write_string("[ap] stack alloc FAIL -- halting\n");
        __asm__ volatile ("cli; hlt");
        while (1);
    }
    unsigned int stack_top = (unsigned int)stack + 0x2000;
    cpu_info[cpu_id].stack_top = (void *)stack_top;

    /* Set up per-CPU GDT/TSS/%gs */
    gdt_init_percpu(cpu_id);
    gdt_set_kernel_stack(cpu_id, stack_top);

    /* Create idle task so this AP can participate in SMP scheduling */
    task_t *idle = task_create(task_idle_loop);
    idle->state = TASK_RUNNING;
    idle->cpu_assigned = cpu_id;
    cpu_info[cpu_id].current_task = idle;
    cpu_info[cpu_id].state = CPU_RUNNING;

    /* Initialize LAPIC on this CPU so it can receive I/O APIC interrupts */
    lapic_init();

    serial_write_string("[ap] CPU ");
    serial_write_int(cpu_id);
    serial_write_string(" ready\n");

    /* Switch to idle task's stack and start it via IRET.
     * This ensures the idle task runs on its own allocated stack,
     * preventing stack sharing when SMP task migration occurs. */
    __asm__ volatile(
        "cli\n"
        "mov %0, %%esp\n"
        "popl %%gs\n"
        "popl %%fs\n"
        "popl %%es\n"
        "popl %%ds\n"
        "popa\n"
        "addl $8, %%esp\n"
        "iret\n"
        : : "r"(idle->kernel_esp));
}

void start_aps(void)
{
    serial_write_string("[smp] starting APs\n");

    if (cpu_count <= 1) {
        serial_write_string("[smp] no APs to start\n");
        return;
    }

    /* Ensure trampoline page is identity-mapped and writable */
    map_page(TRAMPOLINE_ADDR, TRAMPOLINE_ADDR, PAGE_WRITE);

    /* Copy trampoline code to low memory */
    lib_memcpy((void *)TRAMPOLINE_ADDR, _binary_build_ap_trampoline_bin_start, TRAMPOLINE_SIZE);

    /* Write CR3 for APs to use when enabling paging */
    unsigned int cr3 = read_cr3();
    *(volatile unsigned int *)TRAMPOLINE_DATA       = cr3;
    *(volatile unsigned int *)(TRAMPOLINE_DATA + 4) = (unsigned int)&ap_main;

    /* Start each AP */
    for (int i = 1; i < cpu_count; i++) {
        unsigned int apic_id = cpu_info[i].apic_id;
        serial_write_string("[smp] starting CPU ");
        serial_write_int(i);
        serial_write_string(" APIC ");
        serial_write_int(apic_id);
        serial_write_string("\n");

        lapic_start_ap(apic_id, TRAMPOLINE_ADDR >> 12);

        /* Brief spin to let AP finish (timer may be unreliable post-INIT) */
        for (volatile int j = 0; j < 5000000; j++);

        serial_write_string("[smp] CPU ");
        serial_write_int(i);
        serial_write_string(" state=");
        serial_write_int(cpu_info[i].state);
        serial_write_string("\n");
    }

    serial_write_string("[smp] all APs started\n");
}