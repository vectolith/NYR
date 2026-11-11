#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../kernel/include/vmm.h"

#define PAGE_SIZE UINT64_C(4096)
#define P UINT64_C(0x001)
#define RW UINT64_C(0x002)
#define US UINT64_C(0x004)
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
    tables[1536 + 1] = UINT64_C(0x5000) | P | RW | US;
    struct nyr_vmm_translation result;
    if (!nyr_vmm_translate_from_root(PAGE_SIZE, hhdm_offset,
                                     UINT64_C(0x1123), &result) ||
        result.physical_address != UINT64_C(0x5123) ||
        result.page_size != PAGE_SIZE || !result.writable ||
        !result.user_accessible || !result.executable) {
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
    tables[1024 + 1] = UINT64_C(0x800000) | P | RW | PS;
    struct nyr_vmm_translation result;
    if (!nyr_vmm_translate_from_root(PAGE_SIZE, hhdm_offset,
                                     UINT64_C(0x212345), &result) ||
        result.physical_address != UINT64_C(0x812345) ||
        result.page_size != (UINT64_C(1) << 21)) {
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

int main(void) {
    if (setup() != 0) {
        fputs("host VMM test: allocation failed\n", stderr);
        return 1;
    }
    int result = test_4k() | test_effective_permissions() | test_2m() | test_1g() | test_rejections();
    free(tables);
    return result;
}
