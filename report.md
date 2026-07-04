# SMP Implementation Plan — Noverix OS

## Overview

This document outlines the phased implementation of Symmetric Multiprocessing (SMP) support for Noverix. The OS currently runs on a single CPU (BSP) with legacy PIC, no locking, and no concurrency primitives. The plan is organized into 6 phases, each building on the previous.

---

## Phase 0 — Synchronization Primitives

Before multiple CPUs can safely run, we need basic concurrency primitives.

| Task | File | Description |
|------|------|-------------|
| **Spinlock** | `kernel/sync/spinlock.h` | `struct spinlock { volatile unsigned locked; }` — acquire with `lock xchg`, release with `mov $0` |
| **Atomic ops** | `kernel/sync/atomic.h` | `atomic_xchg(ptr, val)`, `atomic_inc(ptr)`, `atomic_cmpxchg(ptr, old, new)` — inline asm `lock` prefix |
| **Memory barrier** | `kernel/sync/barrier.h` | `mb()`, `rmb()`, `wmb()` — `mfence`/`lfence`/`sfence` (or `lock addl $0, (%esp)` on older CPUs) |
| **IRQ save/restore** | `kernel/sync/spinlock.h` | `spinlock_irqsave(lock, flags)` / `spinlock_irqrestore(lock, flags)` — disable interrupts while holding lock |

All four can be in a single file `kernel/sync/sync.h` to minimize new files.

**Dependencies:** None (standalone, uses only inline asm)

---

## Phase 1 — CPU Discovery + ACPI

Detect how many CPUs exist and their APIC IDs.

| Task | File | Description |
|------|------|-------------|
| **CPUID** | `kernel/cpu/cpuid.h` | `cpuid(eax, ecx, out *eax, *ebx, *ecx, *edx)` — feature flags, APIC availability |
| **RSDP find** | `kernel/acpi/rsdp.c` | Search EBDA (0x000E0000–0x000FFFFF) for "RSD PTR " signature |
| **RSDT parse** | `kernel/acpi/madt.c` | Walk RSDT → find MADT (Multiple APIC Description Table) |
| **MADT parse** | `kernel/acpi/madt.c` | Read Local APIC address, I/O APIC entries, processor entries (APIC ID + flags) |
| **cpu_info array** | `kernel/cpu/cpu.h` | `int cpu_count`, `cpu_info_t cpu_info[MAX_CPU]` — APIC ID, state, stack, etc. |

**Output:** `cpu_count`, per-CPU APIC IDs, LAPIC base address, I/O APIC address.

**Dependencies:** Phase 0

---

## Phase 2 — Per-CPU Infrastructure

Each CPU needs its own stack, GDT (with TSS), and per-CPU data area.

| Task | File | Description |
|------|------|-------------|
| **TSS entry** | `kernel/cpu/gdt.c` | Add TSS descriptor to GDT — needed for ring-0 stack switch on interrupt from ring 3 |
| **Per-CPU GDT** | `kernel/cpu/gdt.c` | Clone GDT per CPU — each has own TSS with different `ss0`/`esp0` |
| **Per-CPU stack** | `kernel/kernel.c` | `alloc_frame()` for kernel stack (4KB–8KB per CPU). BSP keeps current stack (0x90000). |
| **Per-CPU data** | `kernel/cpu/cpu.h` | `typedef struct { int cpu_id; int apic_id; void *stack_top; ... } cpu_t;` |
| **%gs segment** | `kernel/cpu/gdt.c` | Set GS base to per-CPU area via GDT entry with per-CPU base address |

On 32-bit x86 without FSGSBASE MSR, each CPU gets a GDT entry with a unique base pointing to its own `cpu_t` struct.

**Dependencies:** Phase 0–1

---

## Phase 3 — APIC Setup

Replace legacy PIC with Local APIC + I/O APIC.

| Task | File | Description |
|------|------|-------------|
| **Enable LAPIC** | `kernel/apic/lapic.c` | MSR 0x1B (IA32_APIC_BASE) → set bit 11 (ENABLE) + bit 10 (EXTD). Map LAPIC at 0xFEE00000. |
| **LAPIC init** | `kernel/apic/lapic.c` | Spurious vector (0xFF), error vector, LVT entries, task priority = 0 |
| **I/O APIC init** | `kernel/apic/ioapic.c` | Read redirection entries from MADT. Program IRQ routing. |
| **I/O APIC routing** | `kernel/apic/ioapic.c` | Map IRQs 0–15 to vectors 0x20–0x2F via redirection table |
| **Mask PIC** | `kernel/cpu/idt.c` | Disable PIC (outb 0xA1, 0xFF; outb 0x21, 0xFF) after I/O APIC takes over |
| **LAPIC timer** | `kernel/apic/lapic.c` | Program divide register + initial count. Replace PIT as primary timer. |
| **Spurious handler** | `kernel/cpu/interrupt.S` | ISR for spurious vector (0xFF) — just `iret` |

**Dependencies:** Phase 1–2

---

## Phase 4 — AP Startup (SIPI)

Wake up Application Processors.

| Task | File | Description |
|------|------|-------------|
| **Trampoline 16-bit** | `boot/ap_trampoline.asm` | Real-mode code at physical < 1MB (e.g., 0x8000): enable A20, load temp GDT, switch to PM |
| **Trampoline 32-bit** | `boot/ap_trampoline.asm` | 32-bit entry: read APIC ID, find per-CPU slot, set stack, load GDT+IDT+TSS, signal ready |
| **SIPI sequence** | `kernel/apic/ipi.c` | `start_aps()`: INIT IPI → wait 10ms → SIPI to trampoline → wait → SIPI again |
| **Ready polling** | `kernel/ap_startup.c` | BSP polls `cpu_info[id].state == CPU_RUNNING` for each AP |
| **Init order** | `kernel/kernel.c` | After `init_paging()`, before `heap_init()`: detect CPU → init APIC → start APs |

Trampoline layout (placed at 0x8000):
```
[org 0x8000]
[bits 16]
ap_start_16:
    ; enable A20 (port 0x92)
    ; load temp GDT (null + code32 + data32)
    ; set CR0.PE = 1
    ; jmp 0x08:ap_start_32

[bits 32]
ap_start_32:
    ; get APIC ID from LAPIC reg 0x20
    ; find cpu_info[id] by APIC ID
    ; set esp = cpu_info[id].stack_top
    ; load per-CPU GDT (with TSS)
    ; load shared IDT
    ; ltr (load task register)
    ; set gs = per-CPU data selector
    ; write cpu_info[id].state = CPU_RUNNING
    ; sti
    ; jmp ap_idle_loop
```

**Dependencies:** Phase 2–3

---

## Phase 5 — Lock Existing Resources

Add spinlocks to every module with global state.

| Module | Lock Name | What It Protects |
|--------|-----------|------------------|
| `pfa.c` | `pfa_lock` | Bitmap allocator (`alloc_frame`, `free_frame`, etc.) |
| `heap.c` | `heap_lock` | `malloc`/`free`/`realloc`/`calloc`/`heap_walk` |
| `paging.c` | `paging_lock` | `map_page`/`unmap_page`/`get_page_mapping` |
| `screen.c` | `screen_lock` | cursor_x/y, capture buffer, VGA memory writes |
| `serial.c` | `serial_lock` | COM1 port access |
| `keyboard.c` | `keyboard_lock` | Ring buffer (head/tail) |
| `tick_count` | (atomic) | `tick_count++` → `atomic_inc(&tick_count)` |
| `idc.c` | `idt_lock` | `register_interrupt_handler` |

Each lock uses `spinlock_irqsave`/`spinlock_irqrestore` so interrupts can be safely disabled while the lock is held.

**Dependencies:** Phase 0

---

## Phase 6 — Higher-Level SMP Features

After basic SMP works, add these in order of priority.

| Priority | Task | Description |
|----------|------|-------------|
| **High** | **TLB shootdown** | `smp_tlb_flush(virt)` — send IPI to all APs to `invlpg` a virtual address. Needed when `map_page`/`unmap_page` is called after APs are running. |
| **Medium** | **Preemptive scheduler** | Timer interrupt calls scheduler → switch tasks. Per-CPU run queue. |
| **Low** | **Per-CPU page tables** | Each process gets its own page directory; CR3 switched on context switch. |
| **Low** | **SMP-safe ELF loader** | Protect `elf_exec` with locks, ensure per-CPU user stacks. |

---

## Architecture After SMP

### BSP Init Order
```
kernel_main()
  ├── init_serial()
  ├── init_gdt()              ← extended: TSS + per-CPU GDT support
  ├── init_idt()              ← extended: APIC vectors added
  ├── init_screen()
  ├── init_keyboard()
  ├── init_timer()
  ├── pfa_init()
  ├── init_paging()
  ├── smp_init()              ← NEW: discover CPUs, init APIC, start APs
  │   ├── acpi_parse_madt()
  │   ├── lapic_init()
  │   ├── ioapic_init()
  │   ├── start_aps()         ← SIPI sequence
  │   └── wait_for_all_aps()
  ├── heap_init()
  ├── ata_init()
  ├── nvfs_mount()
  └── shell_loop()
```

### AP Entry Point
```
ap_entry():
  ├── init_per_cpu_gdt()      ← GDT with own TSS
  ├── init_per_cpu_tss()      ← set ss0:esp0 to per-CPU kernel stack
  ├── set_per_cpu_stack()     ← load esp from cpu_info[id].stack_top
  ├── signal_cpu_ready()      ← cpu_info[id].state = CPU_RUNNING
  ├── sti()
  └── idle_loop()
```

### New Directory Structure
```
kernel/
  ├── sync/
  │   └── sync.h              ← spinlock, atomic, barrier
  ├── acpi/
  │   ├── rsdp.c              ← RSDP discovery
  │   └── madt.c              ← MADT table parser
  ├── apic/
  │   ├── lapic.c             ← Local APIC + timer
  │   ├── ioapic.c            ← I/O APIC routing
  │   └── ipi.c               ← Inter-Processor Interrupts
  └── cpu/
      ├── cpu.h               ← cpu_info_t, cpu_count, MAX_CPU
      └── cpuid.h             ← CPUID wrapper
boot/
  └── ap_trampoline.asm       ← AP real-mode → PM startup
kernel/
  └── ap_startup.c            ← start_aps(), ap_main()
```

---

## Estimated Complexity

| Phase | New Files | Est. Lines | Risk Level |
|-------|-----------|------------|------------|
| **0** | 1–3 | ~100 | Low |
| **1** | 4 | ~350 | Medium (ACPI table format) |
| **2** | 2 | ~200 | Low |
| **3** | 4 | ~300 | **High** (APIC hardware behavior) |
| **4** | 2 | ~200 asm + ~150 C | **Very High** (trampoline debugging) |
| **5** | 0 | ~100 (in-place edits) | Low |
| **6** | ~2 | ~200 | Medium |

---

## Testing

All phases can be tested under QEMU:
```bash
qemu-system-i386 -smp 2 -drive file=os-image.bin,format=raw,if=floppy -serial stdio
```

Key checkpoints:
- Phase 0: spinlock acquire/release doesn't hang, atomics increment correctly
- Phase 1: `cpu_count` printed to serial log with APIC IDs
- Phase 2: per-CPU variables readable via `%gs`, each CPU has different stack pointer
- Phase 3: IRQs fire through I/O APIC, PIC fully masked
- Phase 4: "AP 1 booted" message appears in serial log
- Phase 5: concurrent alloc_frame from both CPUs doesn't corrupt bitmap
- Phase 6: TLB shootdown IPI works, preemptive round-robin visible
