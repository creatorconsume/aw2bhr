// Dead Wars - minimal newlib support: heap in EWRAM after static data
#include "gba.h"
#include <errno.h>

extern char __ewram_bss_end[];
#define HEAP_END ((char *)0x02040000)

void *_sbrk(ptrdiff_t inc)
{
	static char *brk = __ewram_bss_end;
	char *old = brk;
	if (brk + inc > HEAP_END) {
		errno = ENOMEM;
		return (void *)-1;
	}
	brk += inc;
	return old;
}
