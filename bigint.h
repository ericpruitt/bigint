#ifndef ERICPRUITT_BIGINT_H
#define ERICPRUITT_BIGINT_H

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef DIGIT_WIDTH
#define DIGIT_WIDTH 8
#endif

#if DIGIT_WIDTH   ==  8
#define DIGIT_TYPE           uint8_t
#define DIGIT_SUPER_TYPE    uint16_t
#define DIGIT_HEX_TEMPLATE    "%02x"
#define DIGIT_OCT_TEMPLATE    "%03o"
#elif DIGIT_WIDTH == 16
#define DIGIT_TYPE          uint16_t
#define DIGIT_SUPER_TYPE    uint32_t
#define DIGIT_HEX_TEMPLATE    "%04x"
#define DIGIT_OCT_TEMPLATE    "%06o"
#elif DIGIT_WIDTH == 32
#define DIGIT_TYPE          uint32_t
#define DIGIT_SUPER_TYPE    uint64_t
#define DIGIT_HEX_TEMPLATE    "%08x"
#define DIGIT_OCT_TEMPLATE   "%011o"
#elif DIGIT_WIDTH == 64
#define DIGIT_TYPE          uint64_t
#define DIGIT_HEX_TEMPLATE    "%16x"
#define DIGIT_OCT_TEMPLATE   "%022o"
#endif

typedef DIGIT_TYPE digit_tt;

#ifdef DIGIT_SUPER_TYPE
typedef DIGIT_SUPER_TYPE digit_super_tt;
#endif

typedef struct bigint_st bigint_st;
int bigint_init(void);
void bigint_free(bigint_st *);
void bigint_movi(bigint_st *, intmax_t);
void bigint_movui(bigint_st *, uintmax_t);
int bigint_mov(bigint_st *, bigint_st *);
bigint_st *bigint_from_int(intmax_t);
bigint_st *bigint_dup(bigint_st *);
int bigint_cmp(const bigint_st *, const bigint_st *);
bool bigint_eqz(const bigint_st *);
bool bigint_nez(const bigint_st *);
bool bigint_ltz(const bigint_st *);
bool bigint_lez(const bigint_st *);
bool bigint_gtz(const bigint_st *);
bool bigint_gez(const bigint_st *);
int bigint_inc(bigint_st *);
int bigint_dec(bigint_st *);
bigint_st *bigint_add(bigint_st *, bigint_st *, bigint_st *);
bigint_st *bigint_div(bigint_st *, bigint_st **, bigint_st *, bigint_st *);
bool bigint_is_power_of_2(bigint_st *);
bigint_st *bigint_mul(bigint_st *, bigint_st *, bigint_st *);
intmax_t bigint_toi(bigint_st *);
bigint_st *bigint_shl(bigint_st *, bigint_st *, bigint_st*);
bigint_st *bigint_shr(bigint_st *, bigint_st *, bigint_st*);
bigint_st *bigint_shli(bigint_st *, bigint_st *, size_t);
bigint_st *bigint_shri(bigint_st *, bigint_st *, size_t);
bigint_st *bigint_sub(bigint_st *dest, bigint_st *a, bigint_st *b);
bigint_st *bigint_strtobi(const char *);
char *bigint_tostr(bigint_st *);
bigint_st *bigint_pow(bigint_st*, bigint_st *, bigint_st*);
int bigint_snprint(char *, size_t, bigint_st *);
double bigint_tod(bigint_st *);
bigint_st *bigint_gcd(bigint_st *, bigint_st *, bigint_st *);
int bigint_snbprint(char *, size_t, bigint_st *, unsigned char);
char *bigint_tostrb(bigint_st *, unsigned char);
void bigint_cleanup(void);
bigint_st *bigint_mod(bigint_st *, bigint_st *, bigint_st *);
bigint_st *bigint_strtobif(const char *str, const char **fraction);
bigint_st *bigint_max(bigint_st *, bigint_st *);
bigint_st *bigint_min(bigint_st *, bigint_st *);
bigint_st *bigint_from_uint(uintmax_t);
uintmax_t bigint_toui(bigint_st *);
bigint_st *bigint_abs(bigint_st *, bigint_st *);
bigint_st *bigint_logui(bigint_st *, bigint_st *, uintmax_t);
#endif
