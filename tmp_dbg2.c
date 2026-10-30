#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define XBOX_TAG_CACHE_ADDRESS 0x803A6000UL
#define XBOX_TAG_CACHE_BYTES 0x01600000UL
#define XBOX_TAG_HEADER_SIGNATURE_OFFSET 0x20

static uint32_t read_u32(uint8_t const *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int main(void)
{
	uint32_t tag_data_address = 0x8069E000UL;
	uint8_t bytes[4];
	uint8_t const *at;

	printf("start\n");
	fflush(stdout);
	at = (uint8_t const *)(unsigned long)(tag_data_address + XBOX_TAG_HEADER_SIGNATURE_OFFSET);
	printf("at %08lx\n", (unsigned long)at);
	fflush(stdout);
	memcpy(bytes, at, 4);
	printf("sig %08x\n", read_u32(bytes),
		(unsigned)(tag_data_address - XBOX_TAG_CACHE_ADDRESS < XBOX_TAG_CACHE_BYTES));
	return 0;
}
