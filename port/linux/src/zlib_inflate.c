/*
ZLIB_INFLATE.C

One zlib stream, inflated into a buffer the caller owns. The Master Chief
Collection's Saber packages (saber_paks.c) are stored as these. The port's
zlib (port/third_party/zlib), whose names are prefixed, not the game's.
*/

#include "zlib_prefixed.h"

/* the bytes written, which are `destination_size`, or -1 */
long port_zlib_inflate(
	void const *source,
	unsigned long source_size,
	void *destination,
	unsigned long destination_size)
{
	uLongf written = destination_size;

	if (!source || !destination || !source_size || !destination_size)
		return -1;
	if (uncompress(destination, &written, source, source_size) != Z_OK || written != destination_size)
		return -1;

	return (long)written;
}
