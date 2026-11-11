#include <stdbool.h>
#include <stdint.h>
#include "include/limine.h"
#include "include/memory.h"
#include "include/serial.h"
#include "include/interrupts.h"
#include "include/vmm.h"
#include "include/smp.h"
#include "include/lapic.h"

#define LIMINE_REQUEST __attribute__((used, section(".limine_requests")))
#define LIMINE_MARKER __attribute__((used))

LIMINE_MARKER __attribute__((section(".limine_requests_start")))
static volatile uint64_t limine_requests_start[] = LIMINE_REQUESTS_START_MARKER;

LIMINE_REQUEST
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

LIMINE_REQUEST
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0,
    .response = 0
};

LIMINE_REQUEST
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0,
    .response = 0
};

LIMINE_REQUEST
static volatile struct limine_paging_mode_request paging_mode_request = {
    .id = LIMINE_PAGING_MODE_REQUEST_ID,
    .revision = 0,
    .response = 0,
    .mode = LIMINE_PAGING_MODE_X86_64_4LVL,
    .max_mode = LIMINE_PAGING_MODE_X86_64_4LVL,
    .min_mode = LIMINE_PAGING_MODE_X86_64_4LVL
};

LIMINE_REQUEST
static volatile struct limine_memmap_request memory_map_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0,
    .response = 0
};

LIMINE_REQUEST
static volatile struct limine_stack_size_request stack_size_request = {
    .id = LIMINE_STACK_SIZE_REQUEST_ID,
    .revision = 0,
    .response = 0,
    .stack_size = 65536
};

LIMINE_REQUEST
static volatile struct limine_mp_request mp_request = {
    .id = LIMINE_MP_REQUEST_ID,
    .revision = 0,
    .response = 0,
    .flags = 0
};

LIMINE_MARKER __attribute__((section(".limine_requests_end")))
static volatile uint64_t limine_requests_end[] = LIMINE_REQUESTS_END_MARKER;

static void serial_line(const char *label, uint64_t value) {
    nyr_serial_write(label);
    nyr_serial_write_u64(value);
    nyr_serial_write("\r\n");
}

static void framebuffer_fill(struct limine_framebuffer *fb, uint32_t pixel) {
    volatile uint32_t *pixels = (volatile uint32_t *)fb->address;
    const uint64_t stride = fb->pitch / sizeof(uint32_t);

    for (uint64_t y = 0; y < fb->height; ++y) {
        for (uint64_t x = 0; x < fb->width; ++x) {
            pixels[y * stride + x] = pixel;
        }
    }
}

static __attribute__((noreturn)) void halt_forever(void) {
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

__attribute__((noreturn))
void nyr_kernel_main(void) {
    nyr_serial_init();
    nyr_serial_write("NYR: kernel entered\r\n");

    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        nyr_serial_write("NYR: unsupported Limine base revision\r\n");
        halt_forever();
    }

    struct limine_mp_response *mp_response = mp_request.response;
    struct nyr_smp_topology topology;
    if (mp_response == 0) {
        nyr_serial_write("NYR: Limine MP response unavailable; BSP-only mode\r\n");
    } else {
        if (!nyr_smp_discover(mp_response, &topology)) {
            nyr_serial_write("NYR: invalid Limine SMP CPU inventory\r\n");
            halt_forever();
        }
        serial_line("NYR: SMP topology discovered CPUs ", topology.cpu_count);
        serial_line("NYR: BSP processor ID ", topology.bsp_processor_id);
        serial_line("NYR: BSP LAPIC ID ", topology.bsp_lapic_id);
        nyr_serial_write(topology.x2apic
            ? "NYR: SMP topology reports x2APIC mode\r\n"
            : "NYR: SMP topology reports xAPIC mode\r\n");
    }

    nyr_interrupts_init();
    const uint32_t bsp_lapic_id = mp_response != 0 ? topology.bsp_lapic_id : 0u;
    if (!nyr_interrupts_init_cpu(0u, bsp_lapic_id, true) ||
        !nyr_interrupts_self_test()) {
        nyr_serial_write("NYR: BSP descriptor/interrupt initialization failed\r\n");
        halt_forever();
    }
    nyr_serial_write("NYR: BSP GDT, TSS, IDT and breakpoint self-test passed\r\n");

    struct limine_framebuffer_response *fb_response = framebuffer_request.response;
    if (fb_response != 0 && fb_response->framebuffer_count > 0 &&
        fb_response->framebuffers != 0 && fb_response->framebuffers[0] != 0) {
        struct limine_framebuffer *fb = fb_response->framebuffers[0];

        if (fb->address != 0 && fb->bpp == 32 &&
            fb->memory_model == LIMINE_FRAMEBUFFER_RGB &&
            fb->pitch >= fb->width * sizeof(uint32_t)) {
            framebuffer_fill(fb, 0x00101824);
            nyr_serial_write("NYR: framebuffer initialized\r\n");
        } else {
            nyr_serial_write("NYR: framebuffer format unsupported\r\n");
        }
    } else {
        nyr_serial_write("NYR: framebuffer unavailable\r\n");
    }

    struct limine_paging_mode_response *paging_mode = paging_mode_request.response;
    if (paging_mode == 0 ||
        paging_mode->mode != LIMINE_PAGING_MODE_X86_64_4LVL) {
        nyr_serial_write("NYR: four-level paging mode unavailable\r\n");
        halt_forever();
    }

    struct limine_hhdm_response *hhdm = hhdm_request.response;
    struct limine_memmap_response *memory_map = memory_map_request.response;
    if (hhdm == 0 || memory_map == 0) {
        nyr_serial_write("NYR: memory map or HHDM response unavailable\r\n");
        halt_forever();
    }

    nyr_serial_write("NYR: HHDM offset ");
    nyr_serial_write_hex(hhdm->offset);
    nyr_serial_write("\r\n");
    serial_line("NYR: Limine memory-map entry count ", memory_map->entry_count);
    nyr_serial_write("NYR: starting physical memory manager\r\n");

    if (!nyr_pmm_init(memory_map, hhdm->offset)) {
        nyr_serial_write("NYR: physical memory manager initialization failed\r\n");
        halt_forever();
    }

    serial_line("NYR: PMM managed frames ", nyr_pmm_managed_frames());
    serial_line("NYR: PMM free frames ", nyr_pmm_free_frames());
    serial_line("NYR: PMM allocated frames ", nyr_pmm_allocated_frames());

    if (!nyr_pmm_self_test()) {
        nyr_serial_write("NYR: PMM self-test failed\r\n");
        halt_forever();
    }
    nyr_serial_write("NYR: PMM self-test passed\r\n");

    if (!nyr_vmm_enable_nx()) {
        nyr_serial_write("NYR: CPU NX support unavailable\r\n");
        halt_forever();
    }
    if (!nyr_vmm_self_test(hhdm->offset)) {
        nyr_serial_write("NYR: virtual memory translation self-test failed\r\n");
        halt_forever();
    }
    nyr_serial_write("NYR: virtual memory mapping, ownership, and TLB self-tests passed\r\n");
    if (mp_response != 0) {
        if (!nyr_lapic_prepare(hhdm->offset, topology.x2apic) ||
            !nyr_lapic_init_cpu(topology.bsp_lapic_id) ||
            !nyr_lapic_send_nmi(topology.bsp_lapic_id, 0u, UINT64_C(1000000))) {
            nyr_serial_write("NYR: local APIC initialization/self-test failed\r\n");
            halt_forever();
        }
        nyr_serial_write("NYR: local APIC initialized; BSP NMI self-test passed\r\n");
        if (!nyr_smp_start_aps(mp_response, &topology, UINT64_C(50000000))) {
            serial_line("NYR: SMP AP startup failed; online CPUs ", nyr_smp_online_count());
            serial_line("NYR: SMP AP startup failures ", nyr_smp_failed_count());
            halt_forever();
        }
        serial_line("NYR: SMP application processors online ", nyr_smp_online_count());
        if (!nyr_vmm_smp_self_test(hhdm->offset)) {
            nyr_serial_write("NYR: remote TLB shootdown self-test failed\r\n");
            halt_forever();
        }
        nyr_serial_write("NYR: remote TLB shootdown self-test passed\r\n");
        nyr_serial_write("NYR: NMI IPIs acknowledged by all APs\r\n");
    } else {
        nyr_serial_write("NYR: SMP AP startup skipped; no MP response\r\n");
    }
    nyr_serial_write("NYR: SMP CPUs parked with interrupts disabled; scheduler not enabled\r\n");
    nyr_serial_write("NYR: boot milestone reached; idle halt loop\r\n");
    halt_forever();
}
