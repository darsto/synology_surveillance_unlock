/* SPDX-License-Identifier: MIT
 * Copyright(c) 2024 Darek Stojaczyk
 */

#define _GNU_SOURCE
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>
#include <link.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <threads.h>

static thread_local char g_exename[64];

static const char *
local_getexename()
{
	int ret;
	char buf[512];

	if (g_exename[0] != 0) {
		return g_exename;
	}

	ret = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
	if (ret == -1) {
		fprintf(stderr, "readlink failed: %s\n", strerror(errno));
		exit(1);
	}
	buf[ret] = 0;

	char *name = basename(buf);
	snprintf(g_exename, sizeof(g_exename), "%s", name);
	return g_exename;
}

static void
patch_mem(uintptr_t addr, const char *buf, size_t num_bytes)
{
	uintptr_t addr_aligned = addr & ~0xFFF;
	size_t mprot_bytes = (addr - addr_aligned + num_bytes + 0xFFF) & ~0xFFF;

	mprotect((void *)addr_aligned, mprot_bytes, PROT_READ | PROT_WRITE);
	memcpy((void *)addr, buf, num_bytes);
	mprotect((void *)addr_aligned, mprot_bytes, PROT_READ | PROT_EXEC);
}

static void
u32_to_str(char *buf, uint32_t u32)
{
	union {
		char c[4];
		uint32_t u;
	} u;

	u.u = u32;
	buf[0] = u.c[0];
	buf[1] = u.c[1];
	buf[2] = u.c[2];
	buf[3] = u.c[3];
}

static void
patch_mem_u32(uintptr_t addr, uint32_t u32)
{
	union {
		char c[4];
		uint32_t u;
	} u;

	u.u = u32;
	patch_mem(addr, u.c, 4);
}

static uint64_t g_libssutils_org_off;

static int
dl_phdr_cb(struct dl_phdr_info *info, size_t size, void *ctx)
{
	if (strstr(info->dlpi_name, "libssutils.org.so") != NULL) {
		g_libssutils_org_off = info->dlpi_addr - 0x100000;
	}
	return 0;
}

static uint32_t g_num_licenses = 0x32;

__attribute__((constructor))
static void
init(void)
{
	const char *exename = local_getexename();

	dl_iterate_phdr(dl_phdr_cb, NULL);

	// change the number of default licences (instead of 2).
	// 0x41 is some magic offset, subtracted in a million different places
	// throughout the code
	if (g_libssutils_org_off != 0) {
		patch_mem_u32(g_libssutils_org_off + 0x34d9cd + 2, 0x41 + g_num_licenses);
	}

	// patch two additional safety checks fired about an hour after launch
	// which stop all cameras
	if (strcmp(exename, "sscored") == 0) {
		// replace CALL with NOPs on failed camera count check (against some cache)
		patch_mem(0x404f42, "\x90\x90\x90\x90\x90", 5);
		// replace CALL with NOPs on failed camera count check (against hardcoded values)
		patch_mem(0x405263, "\x90\x90\x90\x90\x90", 5);
	} else if (strcmp(exename, "ssdaemonmonitord") == 0) {
		// replace a conditional JMP with NOPs for the failed default licence cam
		// count check (against hardcoded values)
		// This wants to pkill "^sscamerad$"
		patch_mem(0x407756, "\x90\x90\x90\x90\x90\x90", 6);
	}

	// patch another safety check fired about 2-3 hours after launch.
	// This one tries to modify shared memory that sswebstreamd and sscamerad
	// check with every frame - *(uint32_t *)(SSShmCommonCfgAt() + 0x14).
	// This stops the frames from being processed, and the camera streams
	// eventually get stopped due to some timeout.
	// We replace atomic OR [R13 + 0x14], EAX with NOPs. It happens as a
	// result of failing some JSON comparison, not sure what's exactly checked.
	if (strcmp(exename, "ssexechelperd") == 0) {
		patch_mem(0x411244, "\x90\x90\x90\x90\x90", 5);
	} else if (strcmp(exename, "ssroutined") == 0) {
		patch_mem(0x417e29, "\x90\x90\x90\x90\x90", 5);
	} else if (strcmp(exename, "sscmshostd") == 0) {
		patch_mem(0x422bfc, "\x90\x90\x90\x90\x90", 5);
		patch_mem(0x4236fc, "\x90\x90\x90\x90\x90", 5);
	}

	// There's more time bombs in Synology Surveillance, but they fire
	// after ~27h, so just schedule `synopkg restart SurveillanceStation`
	// once a day and be fine.
	// (this results in less than 30 seconds of camera downtime per day).
}

// `synopkg restart SurveillanceStation` causes an annoying alert in every
// active web session, to disable, append 1 line to sds.js:
//   # tail -n 1 /var/packages/SurveillanceStation/target/ui/sds.js
//   SYNO.API.RedirectToDSMByErrorCode = () => { };
