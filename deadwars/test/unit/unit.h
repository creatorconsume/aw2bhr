// Dead Wars - tiny host test framework for the tactical rules (see Makefile)
#ifndef UNIT_H
#define UNIT_H
#include <stdio.h>

// tac_fx_* calls recorded by stubs.c
typedef struct {
	int hits, misses, texts, noises;
	int last_d, last_dmg, nx, ny, nr;
	const char *l1, *l2;
} FxLog;
extern FxLog fx;

extern int checks, fails;
extern const char *test_name;

#define CHECK(c) do { \
	checks++; \
	if (!(c)) { fails++; printf("FAIL [%s] %s:%d: %s\n", test_name, __FILE__, __LINE__, #c); } \
} while (0)

#define EQ(a, b) do { \
	long _a = (long)(a), _b = (long)(b); \
	checks++; \
	if (_a != _b) { fails++; printf("FAIL [%s] %s:%d: %s = %ld, want %ld\n", test_name, __FILE__, __LINE__, #a, _a, _b); } \
} while (0)

// a failed check with a formatted message
#define FAILF(...) do { checks++; fails++; printf("FAIL [%s] ", test_name); printf(__VA_ARGS__); printf("\n"); } while (0)

#endif
