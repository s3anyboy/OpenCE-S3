#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SHADOWRUN_GROUP_TAG_SWAP(word)              \
	(((unsigned long)(word) & 0x000000FFUL) << 24 | \
	 ((unsigned long)(word) & 0x0000FF00UL) << 8 |  \
	 ((unsigned long)(word) & 0x00FF0000UL) >> 8 |  \
	 ((unsigned long)(word) & 0xFF000000UL) >> 24)

#define SHADOWRUN_GROUP_TAG_IS(stored, group)             \
	((unsigned long)(stored) == (unsigned long)(group) || \
	 (unsigned long)(stored) == SHADOWRUN_GROUP_TAG_SWAP(group))

static unsigned long read_word(unsigned char const *p)
{
	return (unsigned long)p[0] | (unsigned long)p[1] << 8 |
		(unsigned long)p[2] << 16 | (unsigned long)p[3] << 24;
}

int main(int argc, char **argv)
{
	unsigned long const srud = (unsigned long)'srud';
	int const entries[] = { 0, 2442, 2526, 2608, 2672, 2662 };
	unsigned char *data;
	FILE *file;
	long size;
	int index;

	printf("'srud'                = %08lX\n", srud);
	printf("SWAP('srud')          = %08lX\n", SHADOWRUN_GROUP_TAG_SWAP(srud));
	printf("'scnr'                = %08lX\n", (unsigned long)'scnr');
	printf("SWAP('scnr')          = %08lX\n", (unsigned long)SHADOWRUN_GROUP_TAG_SWAP((unsigned long)'scnr'));

	if (argc < 2)
	{
		return 0;
	}
	file = fopen(argv[1], "rb");
	if (!file)
	{
		printf("cannot open %s\n", argv[1]);
		return 1;
	}
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fseek(file, 0, SEEK_SET);
	data = malloc((size_t)size);
	if (size <= 0 || !data || fread(data, 1, (size_t)size, file) != (size_t)size)
	{
		printf("cannot read %s\n", argv[1]);
		return 1;
	}
	fclose(file);

	/* the tag data at 0x5F3C400, its table at +0x24 (recomp.map) */
	for (index = 0; index < (int)(sizeof(entries) / sizeof(entries[0])); index++)
	{
		long at = 0x5F3C400L + 0x24 + (long)entries[index] * 0x20;
		unsigned long word = read_word(data + at);
		unsigned char bytes[5];

		memcpy(bytes, &word, 4);
		bytes[4] = 0;
		printf("  #%-5d word %08lX  bytes %02x%02x%02x%02x %s  IS %d  (index %08lX)\n",
			entries[index], word, bytes[0], bytes[1], bytes[2], bytes[3], bytes,
			SHADOWRUN_GROUP_TAG_IS(word, srud), read_word(data + at + 0x0C));
	}

	/* the tag header the file's tag data gives: its table's address, and its
	count, and where that table is in the file */
	{
		long tag_data = 0x5F3C400L;
		unsigned long table = read_word(data + tag_data);
		unsigned long count = read_word(data + tag_data + 0x0C);
		unsigned long linked = 0x806D0000UL;
		long at = tag_data + (long)(table - linked);
		int found = 0;
		int i;

		printf("tag header: table %08lX, count %lu, linked %08lX -> table at file %lX\n",
			table, count, linked, at);
		printf("entries at that place:\n");
		for (i = 0; i < 6; i++)
		{
			unsigned long word = read_word(data + at + (long)i * 0x20);
			unsigned char bytes[5];

			memcpy(bytes, &word, 4);
			bytes[4] = 0;
			printf("  #%-4d word %08lX %s index %08lX\n", i, word, bytes,
				read_word(data + at + (long)i * 0x20 + 0x0C));
		}
		/* how many entries of the declared table hold a group of four
		printable bytes, and how many carry 'srud' */
		for (i = 0; i < (int)count; i++)
		{
			unsigned long word = read_word(data + at + (long)i * 0x20);
			unsigned char bytes[4];
			int ok = 1;
			int k;

			memcpy(bytes, &word, 4);
			for (k = 0; k < 4; k++)
			{
				if (bytes[k] < 0x20 || bytes[k] > 0x7e)
					ok = 0;
			}
			if (ok && SHADOWRUN_GROUP_TAG_IS(word, srud))
				found++;
		}
		printf("of %lu entries at the declared place, %d carry 'srud'\n", count, found);
	}

	free(data);

	return 0;
}
