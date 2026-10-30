#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define XBOX_TAG_CACHE_ADDRESS 0x803A6000UL
#define XBOX_TAG_CACHE_BYTES 0x01600000UL
#define XBOX_TAG_HEADER_SIGNATURE_OFFSET 0x20

int main(void)
{
	uint32_t tag_data_address = 0x8069E000UL;
	uint32_t bytes[1];
	void *p = (void *)(unsigned long)(tag_data_address + XBOX_TAG_HEADER_SIGNATURE_OFFSET);

	printf("pointer %08lx\n", (unsigned long)p);
	fflush(stdout);
	memcpy(bytes, p, sizeof(bytes));
	printf("%08x %08x\n", bytes[0], (unsigned)(tag_data_address - XBOX_TAG_CACHE_ADDRESS < XBOX_TAG_CACHE_BYTES));
#pragma message("build ok")
	return 0;
}
