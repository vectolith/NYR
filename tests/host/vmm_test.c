#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../kernel/include/vmm.h"

void nyr_serial_write(const char *text) { (void)text; }

bool nyr_smp_tlb_shootdown(uint64_t virtual_address) {
    (void)virtual_address;
    return true;
}

#define PAGE_SIZE UINT64_C(4096)
#define P UINT64_C(0x001)
#define RW UINT64_C(0x002)
#define US UINT64_C(0x004)
#define PWT UINT64_C(0x008)
#define PCD UINT64_C(0x010)
#define PS UINT64_C(0x080)
#define NX (UINT64_C(1) << 63)

static uint64_t *tables;
static uint64_t hhdm_offset;

static int setup(void) {
    tables = aligned_alloc((size_t)PAGE_SIZE, (size_t)(4u * PAGE_SIZE));
    if (tables == NULL) return 1;
    memset(tables, 0, (size_t)(4u * PAGE_SIZE));
    hhdm_offset = (uint64_t)(uintptr_t)tables - PAGE_SIZE;
    tables[0] = UINT64_C(0x2000) | P | RW | US;
    tables[512] = UINT64_C(0x3000) | P | RW | US;
    tables[1024] = UINT64_C(0x4000) | P | RW | US;
    return 0;
}

static int test_4k(void) {
    tables[1536 + 1] = UINT64_C(0x5000) | P | RW | US | PWT | PCD;
    struct nyr_vmm_translation result;
    if (!nyr_vmm_translate_from_root(PAGE_SIZE, hhdm_offset,
                                     UINT64_C(0x1123), &result) ||
        result.physical_address != UINT64_C(0x5123) ||
        result.page_size != PAGE_SIZE || !result.writable ||
        !result.user_accessible || !result.executable || !result.uncached) {
        fputs("host VMM test: 4 KiB translation failed\n", stderr);
        return 1;
    }
    puts("host VMM test: 4 KiB translation and permissions passed");
    return 0;
}

static int test_effective_permissions(void) {
    tables[0] = UINT64_C(0x2000) | P | US | NX;
    struct nyr_vmm_translation result;
    const bool ok =
        nyr_vmm_translate_from_root(PAGE_SIZE, hhdm_offset,
                                    UINT64_C(0x1123), &result) &&
        !result.writable && !result.executable && result.user_accessible;
    tables[0] = UINT64_C(0x2000) | P | RW | US;
    if (!ok) {
        fputs("host VMM test: inherited permissions were not enforced\\n", stderr);
        return 1;
    }
    puts("host VMM test: effective permissions propagated across levels");
    return 0;
}

static int test_2m(void) {
    tables[1024 + 1] = UINT64_C(0x800000) | P | RW | PS | PWT | PCD;
    struct nyr_vmm_translation result;
    if (!nyr_vmm_translate_from_root(PAGE_SIZE, hhdm_offset,
                                     UINT64_C(0x212345), &result) ||
        result.physical_address != UINT64_C(0x812345) ||
        result.page_size != (UINT64_C(1) << 21) || !result.uncached) {
        fputs("host VMM test: 2 MiB translation failed\n", stderr);
        return 1;
    }
    puts("host VMM test: 2 MiB huge-page translation passed");
    return 0;
}

static int test_1g(void) {
    tables[512 + 1] = UINT64_C(0x40000000) | P | RW | PS;
    struct nyr_vmm_translation result;
    if (!nyr_vmm_translate_from_root(PAGE_SIZE, hhdm_offset,
                                     UINT64_C(0x40002345), &result) ||
        result.physical_address != UINT64_C(0x40002345) ||
        result.page_size != (UINT64_C(1) << 30)) {
        fputs("host VMM test: 1 GiB translation failed\n", stderr);
        return 1;
    }
    puts("host VMM test: 1 GiB huge-page translation passed");
    return 0;
}

static int test_reserved_huge_bits(void) {
    struct nyr_vmm_translation result;
    tables[512 + 1] = UINT64_C(0x40000000) | P | RW | PS | (UINT64_C(1) << 13);
    if (nyr_vmm_translate_from_root(PAGE_SIZE, hhdm_offset,
                                    UINT64_C(0x40002345), &result)) {
        fputs("host VMM test: reserved 1 GiB page bits accepted\\n", stderr);
        return 1;
    }
    tables[512 + 1] = UINT64_C(0x3000) | P | RW | US;
    tables[1024 + 1] = UINT64_C(0x800000) | P | RW | PS | (UINT64_C(1) << 13);
    if (nyr_vmm_translate_from_root(PAGE_SIZE, hhdm_offset,
                                    UINT64_C(0x212345), &result)) {
        fputs("host VMM test: reserved 2 MiB page bits accepted\\n", stderr);
        return 1;
    }
    puts("host VMM test: reserved huge-page address bits rejected");
    return 0;
}

static int test_rejections(void) {
    struct nyr_vmm_translation result;
    if (nyr_vmm_translate_from_root(PAGE_SIZE, hhdm_offset,
                                    UINT64_C(0x0000800000000000), &result) ||
        nyr_vmm_translate_from_root(PAGE_SIZE + 1u, hhdm_offset,
                                    UINT64_C(0x1000), &result) ||
        nyr_vmm_translate_from_root(PAGE_SIZE, hhdm_offset,
                                    UINT64_C(0x3000), &result)) {
        fputs("host VMM test: invalid address or missing mapping accepted\n", stderr);
        return 1;
    }
    puts("host VMM test: non-canonical, unaligned-root, and absent mappings rejected");
    return 0;
}

static int test_mapping_argument_validation(void) {
    if (nyr_vmm_map_page(0, UINT64_C(0xffffc00000000001),
                         PAGE_SIZE, 0, NYR_VMM_PAGE_BORROWED) ||
        nyr_vmm_map_page(0, UINT64_C(0xffffc00000000000),
                         PAGE_SIZE + 1u, 0, NYR_VMM_PAGE_BORROWED) ||
        nyr_vmm_map_page(0, UINT64_C(0xffffc00000000000),
                         PAGE_SIZE, NYR_VMM_MAP_WRITE | NYR_VMM_MAP_EXECUTE,
                         NYR_VMM_PAGE_BORROWED) ||
        nyr_vmm_map_page(0, UINT64_C(0xffffc00000000000),
                         PAGE_SIZE, 0, (enum nyr_vmm_page_ownership)9)) {
        fputs("host VMM test: invalid map arguments accepted\\n", stderr);
        return 1;
    }
    if (nyr_vmm_map_range(0, UINT64_C(0xffffc00000000000),
                          PAGE_SIZE, 0, 0, NYR_VMM_PAGE_BORROWED) ||
        nyr_vmm_map_range(0, UINT64_C(0xffffc00000000000),
                          PAGE_SIZE, UINT64_MAX, 0, NYR_VMM_PAGE_BORROWED) ||
        nyr_vmm_map_range(0, UINT64_C(0xffffc00000000000),
                          PAGE_SIZE, 1,
                          NYR_VMM_MAP_WRITE | NYR_VMM_MAP_EXECUTE,
                          NYR_VMM_PAGE_BORROWED) ||
        nyr_vmm_protect_page(0, UINT64_C(0xffffc00000000000),
                             NYR_VMM_MAP_WRITE | NYR_VMM_MAP_EXECUTE) ||
        nyr_vmm_protect_page(0, UINT64_C(0xffffc00000000001), 0)) {
        fputs("host VMM test: invalid protection arguments accepted\\n", stderr);
        return 1;
    }
    if (nyr_vmm_unmap_page(0, UINT64_C(0xffffc00000000001), NULL)) {
        fputs("host VMM test: invalid unmap arguments accepted\\n", stderr);
        return 1;
    }
    puts("host VMM test: alignment, W^X, ownership, protection and unmap arguments validated");
    return 0;
}

int main(void) {
    if (setup() != 0) {
        fputs("host VMM test: allocation failed\n", stderr);
        return 1;
    }
    int result = test_4k() | test_effective_permissions() | test_2m() | test_1g() | test_reserved_huge_bits() | test_rejections() | test_mapping_argument_validation();
    free(tables);
    return result;
}
