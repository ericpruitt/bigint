bigint
======

This is a library for arbitrary precision ("big") integers. It does not aim to
be fast or suitable for cryptographic uses, just functional and easy to
understand. The internal representation of the numbers is sign-magnitude. The
code makes extensive use of dynamic allocations and currently does not support
fixed-sized buffers. By default, each digit within the integers is represented
by an unsigned, 8-bit value, but this library also supports using 16-bit,
32-bit and 64-bit integers for the digits. The is controlled by defining
DIGIT_WIDTH which should be set to 16, 32 or 64 accordingly. Although 8 is the
default, the library user can also explicitly set DIGIT_WIDTH to this value.

This library is a work-in-progress. All functions within the code are fully
documented, but although the API implements many common operations and
comparators, there is no user guide, and the unit tests are incomplete (there
is an existing suite of tests that have not yet been committed to this
repository).

API
---

### Assignments and Initialization ###

- `int bigint_mov(bigint_st *dest, bigint_st *src)`
- `void bigint_movi(bigint_st *dest, intmax_t src)`
- `void bigint_movui(bigint_st *dest, uintmax_t src)`
- `bigint_st *bigint_from_uint(uintmax_t value)`
- `bigint_st *bigint_from_int(intmax_t value)`

### Arithmetic ###

- `bigint_st *bigint_div(bigint_st *q, bigint_st **r, bigint_st *n, bigint_st *d)`
- `bigint_st *bigint_add(bigint_st *dest, bigint_st *a, bigint_st *b)`
- `bigint_st *bigint_sub(bigint_st *dest, bigint_st *a, bigint_st *b)`
- `bigint_st *bigint_mul(bigint_st *dest, bigint_st *a, bigint_st *b)`
- `bigint_st *bigint_shli(bigint_st *dest, bigint_st *x, size_t n)`
- `bigint_st *bigint_shl(bigint_st *dest, bigint_st *x, bigint_st* n)`
- `bigint_st *bigint_shri(bigint_st *dest, bigint_st *x, size_t n)`
- `bigint_st *bigint_shr(bigint_st *dest, bigint_st *x, bigint_st* n)`
- `int bigint_inc(bigint_st *)`
- `int bigint_dec(bigint_st *)`
- `bigint_st *bigint_mod(bigint_st *r, bigint_st *n, bigint_st *d)`
- `bigint_st *bigint_pow(bigint_st* dest, bigint_st *base, bigint_st* exp)`
- `bigint_st *bigint_logui(bigint_st *dest, bigint_st *x, uintmax_t base)`
- `bigint_st *bigint_abs(bigint_st *dest, bigint_st *x)`
- `bigint_st *bigint_gcd(bigint_st *dest, bigint_st *a, bigint_st* b)`

### Type Conversions/Casting ###

- `uintmax_t bigint_toui(bigint_st *x)`
- `intmax_t bigint_toi(bigint_st *x)`
- `double bigint_tod(bigint_st *x)`
- `bigint_st *bigint_strtobif(const char *str, const char **fraction)`
- `int bigint_snbprint(char *buf, size_t buflen, bigint_st *x, unsigned char base)`
- `int bigint_snprint(char *buf, size_t buflen, bigint_st *x)`
- `char *bigint_tostrb(bigint_st *x, unsigned char base)`
- `char *bigint_tostr(bigint_st *x)`

### Comparators ###

- `int bigint_cmp(const bigint_st *a, const bigint_st *b)`
- `bool bigint_eqz(const bigint_st *x)`
- `bool bigint_nez(const bigint_st *x)`
- `bool bigint_ltz(const bigint_st *x)`
- `bool bigint_lez(const bigint_st *x)`
- `bool bigint_gtz(const bigint_st *x)`
- `bool bigint_gez(const bigint_st *x)`

### Miscellaneous ###

- `bool bigint_is_power_of_2(bigint_st *x)`
- `bigint_st *bigint_min(bigint_st *a, bigint_st *b)`
- `bigint_st *bigint_max(bigint_st *a, bigint_st *b)`
