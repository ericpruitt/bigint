/**
 * This library provides structures and calculation interfaces for arbitrary
 * length ("big") integers. The implementation internally uses sign-magnitude.
 */
#include <errno.h>
#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bigint.h"

/**
 * The number of bits in each digit.
 */
#define DIGIT_BITS (sizeof(digit_tt) * CHAR_BIT)

/**
 * The maximum value a single digit can store.
 */
#define DIGIT_MAX ((digit_tt) -1)

/**
 * Perform integer division that rounds the result up if there if there would
 * be a remainder.
 *
 * Arguments:
 * - n: Numerator
 * - d: Denominator
 *
 * Return: `ceil(n / d)`
 */
#define CEIL_DIV(n, d) ((n) / (d) + ((n) % (d) != 0))

/**
 * Number of digits required to represent any intmax_t value.
 */
#define DIGITS_FOR_INTMAX CEIL_DIV(sizeof(intmax_t), sizeof(digit_tt))

/**
 * Maximum value stored in the small number cache.
 */
#define SMALL_NUMBER_CACHE_MAX 16

/**
 * Compute `a - b` and assign the result to "a". This can never fail because
 * "magnitude_delta" is zero copy when the destination is also the minuend.
 */
#define MAGNITUDE_RDELTA(a, b) magnitude_delta(a, a, b)

/**
 * The absolute value of `INTMAX_MIN` represented as a uintmax_t value. This is
 * implemented in such a way that the value correct even if `-INTMAX_MIN`
 * cannot produce a meaningful value.
 */
#if (INTMAX_MIN + INTMAX_MAX) >= 0
#define INTMAX_MIN_MAGNITUDE ( \
    (uintmax_t) INTMAX_MAX - (uintmax_t) (INTMAX_MIN + INTMAX_MAX) \
)
#else
#define INTMAX_MIN_MAGNITUDE ( \
    (uintmax_t) INTMAX_MAX + ((uintmax_t) -(INTMAX_MIN + INTMAX_MAX)) \
)
#endif

/**
 * Determine whether an integer is a power of two.
 *
 * Arguments:
 * - x: An integer.
 *
 * Return: A non-zero value if the value is a power of two and 0 otherwise.
 */
#define POWER_OF_2(x) ((((x) - 1) & (x)) == 0)

/**
 * Big integer representing the value of 10.
 */
#define TEN small_number_cache[10]

/**
 * Sign-magnitude representation of arbitrary-length ("big") integers.
 */
struct bigint_st {
    /**
     * Array containing the digits of the big integer with the least
     * significant bits in `digits[0]`.
     */
    digit_tt *digits;
    /**
     * The maximum number of digits that can be stored with the currently
     * allocated amount of memory.
     */
    size_t allocated;
    /**
     * The number of digits stored in the allocated space. When the value of
     * the number is 0, the length is also 0.
     */
    size_t length;
    /**
     * Value that indicates whether or not the value is negative. 0 is always
     * non-negative.
     */
    bool negative;
};

/**
 * Cache of pointers to pre-generated structures for small numbers. This is
 * populated by "bigint_init".
 */
static bigint_st* small_number_cache[SMALL_NUMBER_CACHE_MAX + 1];

/**
 * Variable used to track whether the library's internal state has been
 * initialized.
 */
static bool init_done = false;

#ifndef DIGIT_SUPER_TYPE
/**
 * Compute the sum of two unsigned, 64-bit integers.
 *
 * Arguments:
 * - msb: Pointer where the upper 64 bits of the existing value and the result
 *   are stored.
 * - lsb: Pointer where the lower 64 bits of the existing value and the result
 *   are stored.
 */
static void u128add64(uint64_t *msb, uint64_t *lsb, uint64_t addend)
{
    uint64_t new_lsb;

    if (addend != 0) {
        new_lsb = *lsb + addend;
        *msb += new_lsb < *lsb;
        *lsb = new_lsb;
    }
}

/**
 * Compute the product of two unsigned, 64-bit integers summed with an
 * additional value.
 *
 * Arguments:
 * - msb: Pointer where the upper 64 bits of the existing value and the result
 *   are stored.
 * - lsb: Pointer where the lower 64 bits of the existing value and the result
 *   are stored.
 * - a: Multiplicand.
 * - b: Multiplicand.
 * - c: Addend.
 */
static void u128fma64(
    uint64_t *msb, uint64_t *lsb, uint64_t a, uint64_t b, uint64_t c
)
{
    uint64_t al = a & (uint32_t) -1;
    uint64_t ah = a >> 32;
    uint64_t bl = b & (uint32_t) -1;
    uint64_t bh = b >> 32;

    uint64_t al_bl = al * bl;
    uint64_t ah_bl = ah * bl;
    uint64_t al_bh = al * bh;
    uint64_t ah_bh = ah * bh;

    uint64_t cross = (al_bl >> 32) + (ah_bl & (uint32_t) -1) + al_bh;

    *msb = (ah_bl >> 32) + (cross >> 32) + ah_bh;
    *lsb = (cross << 32) | (al_bl & (uint32_t) -1);

    u128add64(msb, lsb, c);
}
#endif

/**
 * This function works like _calloc(3)_, but when the total number of bytes
 * would lead to an integer overflow, the allocation fails.
 *
 * Arguments:
 * - nmemb: Number of members for which space should be allocated.
 * - size: The size of each member for which space should be allocated.
 *
 * Return: A pointer to the allocated memory if the allocation succeeds.
 */
static void *safe_calloc(size_t nmemb, size_t size)
{
    if (nmemb > SIZE_MAX / size) {
        errno = EOVERFLOW;
        return NULL;
    }

    return malloc(nmemb * size);
}

/**
 * This function works like _reallocarray(3)_, but when the total number of
 * bytes would lead to an integer overflow, the allocation fails.
 *
 * Arguments:
 * - nmemb: Number of members for which space should be allocated.
 * - size: The size of each member for which space should be allocated.
 *
 * Return: A pointer to the allocated memory if the allocation succeeds.
 */
static void *safe_reallocarray(void *ptr, size_t nmemb, size_t size)
{
    if (nmemb > SIZE_MAX / size) {
        errno = EOVERFLOW;
        return NULL;
    }

    return realloc(ptr, nmemb * size);
}

/**
 * Change the length of a number and reallocate the space available for the
 * digits as necessary.
 *
 * Arguments:
 * - x: A big integer.
 * - length: The new length.
 *
 * Return: 0 if the operation succeeds and -1 if it fails.
 */
static int resize(bigint_st *x, size_t length)
{
    size_t original_allocated = x->allocated;

    while (length >= x->allocated) {
        x->allocated *= 2;

        // If doubling causes an overflow, just use the exact length provided.
        if (x->allocated <= original_allocated) {
            x->allocated = length;
            break;
        }
    }

    if (x->allocated > original_allocated) {
        digit_tt *new = safe_reallocarray(
            x->digits, x->allocated, sizeof(digit_tt)
        );

        if (!new) {
            x->allocated = original_allocated;
            return -1;
        }

        x->digits = new;
        memset(
            x->digits + original_allocated,
            0,
            sizeof(digit_tt) * (x->allocated - original_allocated)
        );
    }

    x->length = length;
    return 0;
}

/**
 * Change the length of a number and reallocate the space available for the
 * digits as necessary to be the sum of two values. If that sum results in an
 * overflow, this function will fail.
 *
 * Arguments:
 * - x: A big integer.
 * - a: Addend.
 * - b: Addend.
 *
 * Return: 0 if the operation succeeds and -1 if it fails.
 */
static int resize_sum(bigint_st *x, size_t a, size_t b)
{
    size_t sum = a + b;

    if (sum < a) {
        errno = EOVERFLOW;
        return -1;
    }

    return resize(x, sum);
}

/**
 * A thin wrapper around _free(3)_ that ensures errno is preserved when the
 * call returns. See also: <https://www.austingroupbugs.net/view.php?id=385>.
 *
 * Arguments:
 *- ptr: Pointer to free.
 */
static void xfree(void *ptr)
{
    int saved_errno = errno;

    free(ptr);

    errno = saved_errno;
}

/**
 * Determines whether or not a big integer is a power of two.
 *
 * Arguments:
 * - x: Big integer.
 *
 * Return: True if the number is a power of two or false otherwise.
 */
bool bigint_is_power_of_2(bigint_st *x)
{
    if (bigint_eqz(x)) {
        return false;
    }

    for (size_t n = 0; n < x->length - 1; n++) {
        if (x->digits[n] != 0) {
            return false;
        }
    }

    return POWER_OF_2(x->digits[x->length - 1]);
}

/**
 * Release the resources associated with a big integer. This function is
 * guaranteed to preserve errno.
 *
 * Arguments:
 * - x: A big integer.
 */
void bigint_free(bigint_st *x)
{
    xfree(x->digits);
    xfree(x);
}

/**
 * Duplicate a big integer. The caller is responsible for calling "bigint_free"
 * when the structure is no longer needed.
 *
 * Arguments:
 * - x: Value to duplicate.
 *
 * Return: A pointer to the duplicated structure or `NULL` if it could not be
 * duplicated in which case "errno" will be set appropriately.
 */
bigint_st *bigint_dup(bigint_st *x)
{
    bigint_st *new = malloc(sizeof(*x));

    if (!new) {
        return NULL;
    }

    *new = *x;
    new->digits = safe_calloc(x->allocated, sizeof(digit_tt));

    if (!new->digits) {
        xfree(new);
        return NULL;
    }
    memset(new->digits, 0, x->allocated * sizeof(digit_tt));

    if (x->length != 0) {
        memcpy(new->digits, x->digits, x->length * sizeof(digit_tt));
    }

    return new;
}

/**
 * Return a value indicating whether the argument equals zero.
 *
 * Arguments:
 * - x: Big integer.
 *
 * Return: 0 if the value equals zero, and a non-zero value otherwise.
 */
bool bigint_eqz(const bigint_st *x)
{
    return x->length == 0;
}

/**
 * Return a value indicating whether the argument is not equal zero.
 *
 * Arguments:
 * - x: Big integer.
 *
 * Return: 0 if the value equals zero, and a non-zero value otherwise.
 */
bool bigint_nez(const bigint_st *x)
{
    return x->length != 0;
}

/**
 * Return a value indicating whether the argument is less than zero.
 *
 * Arguments:
 * - x: Big integer.
 *
 * Return: A boolean value indicating whether the condition is true.
 */
bool bigint_ltz(const bigint_st *x)
{
    return x->negative;
}

/**
 * Return a value indicating whether the argument is less than or equal to
 * zero.
 *
 * Arguments:
 * - x: Big integer.
 *
 * Return: A boolean value indicating whether the condition is true.
 */
bool bigint_lez(const bigint_st *x)
{
    return x->length == 0 || x->negative;
}

/**
 * Return a value indicating whether the argument is greater than zero.
 *
 * Arguments:
 * - x: Big integer.
 *
 * Return: A boolean value indicating whether the condition is true.
 */
bool bigint_gtz(const bigint_st *x)
{
    return !x->negative && x->length;
}

/**
 * Return a value indicating whether the argument is greater than or equal to
 * zero.
 *
 * Arguments:
 * - x: Big integer.
 *
 * Return: A boolean value indicating whether the condition is true.
 */
bool bigint_gez(const bigint_st *x)
{
    return !x->negative;
}

/**
 * Return the least of two big integers.
 *
 * Arguments:
 * - a: A big integer.
 * - b: A big integer.
 *
 * Return: The smaller of the two values.
 */
bigint_st *bigint_min(bigint_st *a, bigint_st *b)
{
    return bigint_cmp(a, b) <= 0 ? a : b;
}

/**
 * Return the greatest of two big integers.
 *
 * Arguments:
 * - a: A big integer.
 * - b: A big integer.
 *
 * Return: The larger of the two values.
 */
bigint_st *bigint_max(bigint_st *a, bigint_st *b)
{
    return bigint_cmp(a, b) >= 0 ? a : b;
}

/**
 * Assign the value of one big integer to another.
 *
 * Arguments:
 * - dest: Destination value.
 * - src: Source value.
 *
 * Return: 0 if the operation succeeds and -1 if it fails.
 */
int bigint_mov(bigint_st *dest, bigint_st *src)
{
    if (dest == src) {
        return 0;
    }

    if (src->length > dest->allocated && resize(dest, src->length)) {
        return -1;
    }

    memcpy(dest->digits, src->digits, src->length * sizeof(digit_tt));
    dest->length = src->length;
    return 0;
}

/**
 * Assign the value of a standard integer to an existing big integer.
 *
 * Arguments:
 * - dest: Destination value.
 * - src: Source value.
 */
void bigint_movi(bigint_st *dest, intmax_t src)
{
    uintmax_t magnitude;
    size_t offset;

    dest->negative = src < 0;

    if (src == 0) {
        dest->length = 0;
        return;
    }

    if (src > 0) {
        magnitude = (uintmax_t) src;
    } else {
#if (INTMAX_MIN + INTMAX_MAX) >= 0
        magnitude = (uintmax_t) -src;
#else
        if (src == INTMAX_MIN) {
            magnitude = INTMAX_MIN_MAGNITUDE;
        } else {
            magnitude = (uintmax_t) -src;
        }
#endif
    }

    if (DIGIT_BITS >= sizeof(magnitude) * CHAR_BIT) {
        dest->length = 1;
        dest->digits[0] = (digit_tt) magnitude;
    } else {
        offset = 0;

        while (magnitude != 0) {
            dest->digits[offset++] = magnitude & DIGIT_MAX;
            magnitude >>= DIGIT_BITS;
        }

        dest->length = offset;
    }
}

/**
 * Assign the value of an unsigned integer to an existing big integer.
 *
 * Arguments:
 * - dest: Destination value.
 * - src: Source value.
 */
void bigint_movui(bigint_st *dest, uintmax_t src)
{
    size_t offset;

    dest->negative = false;

    if (src == 0) {
        dest->length = 0;
    } else if (DIGIT_BITS >= sizeof(src) * CHAR_BIT) {
        dest->length = 1;
        dest->digits[0] = (digit_tt) src;
    } else {
        offset = 0;

        while (src != 0) {
            dest->digits[offset++] = src & DIGIT_MAX;
            src >>= DIGIT_BITS;
        }

        dest->length = offset;
    }
}

/**
 * Create a big integer from a standard unsigned integer.
 *
 * Arguments:
 * - value: Value of integer.
 *
 * Return: A pointer to a big integer if the instantiation succeeds and `NULL`
 * otherwise.
 */
bigint_st *bigint_from_uint(uintmax_t value)
{
    bigint_st *x;

    x = malloc(sizeof(bigint_st));

    if (!x) {
        return NULL;
    }

    // The minimum allocation is enough to for two intmax_t reducing the
    // likelihood of reallocations when working with smaller values.
    x->allocated = 2 * DIGITS_FOR_INTMAX;
    x->digits = safe_calloc(x->allocated, sizeof(digit_tt));

    if (!x->digits) {
        xfree(x);
        return NULL;
    }

    bigint_movui(x, value);
    return x;
}

/**
 * Create a big integer from a standard integer.
 *
 * Arguments:
 * - value: Value of integer.
 *
 * Return: A pointer to a big integer if the instantiation succeeds and `NULL`
 * otherwise.
 */
bigint_st *bigint_from_int(intmax_t value)
{
    bigint_st *x;

    x = malloc(sizeof(bigint_st));

    if (!x) {
        return NULL;
    }

    // The minimum allocation is enough to for two intmax_t reducing the
    // likelihood of reallocations when working with smaller values.
    x->allocated = 2 * DIGITS_FOR_INTMAX;
    x->digits = safe_calloc(x->allocated, sizeof(digit_tt));

    if (!x->digits) {
        xfree(x);
        return NULL;
    }

    bigint_movi(x, value);
    return x;
}

/**
 * Count the number of leading zeroes in the bits of the most significant digit
 * of a big integer.
 *
 * Arguments:
 * - x: A big integer.
 *
 * Return: The number of leading zeroes.
 */
static inline size_t clz(bigint_st *x)
{
    size_t result = 0;
    digit_tt msd = x->digits[x->length - 1];
    digit_tt mask = 1 << (DIGIT_BITS - 1);

    while ((mask & msd) == 0) {
        mask >>= 1;
        result++;
    }

    return result;
}

/**
 * Count the number of trailing zeroes of a big integer.
 *
 * Arguments:
 * - x: A big integer.
 *
 * Return: The number of leading zeroes.
 */
static inline size_t ctz(bigint_st *x)
{
    digit_tt digit;

    size_t result = 0;

    for (size_t n = 0; n < x->length; n++) {
        digit = x->digits[n];

        if (x->digits[n] == 0) {
            result += DIGIT_BITS;
        } else {
            while ((digit & 1) == 0) {
                result++;
                digit >>= 1;
            }

            break;
        }
    }

    return result;
}

/**
 * Reduce the length of the big integer to exclude any leading zeroes and
 * ensure the "negative" struct value is set to false if the value of the
 * integer is 0.
 *
 * Arguments:
 * - x: Big integer.
 */
static void normalize(bigint_st *x)
{
    while (x->length && !x->digits[x->length - 1]) {
        x->length--;
    }

    if (x->length == 0) {
        x->negative = false;
    }
}

/**
 * Decrement the magnitude of a non-zero number by 1.
 *
 * Arguments:
 * - x: A non-zero big integer.
 *
 * Return: 0 if the operation succeeds or a negative value otherwise.
 */
static int magnitude_dec(bigint_st *x)
{
    x->digits[0]--;

    if (x->digits[0] == DIGIT_MAX) {
        for (size_t i = 1; i < x->length; i++) {
            if (x->digits[i] != 0) {
                x->digits[i]--;
                break;
            }

            x->digits[i] = DIGIT_MAX;
        }
    }

    normalize(x);
    return 0;
}

/**
 * Increment the magnitude of a number by 1.
 *
 * Arguments:
 * - x: A big integer.
 *
 * Return: 0 if the operation succeeds or a negative value otherwise.
 */
static int magnitude_inc(bigint_st *x)
{
    digit_tt sum;

    digit_tt carry = 1;

    for (size_t i = 0; i < x->length; i++) {
        sum = x->digits[i] + carry;
        carry = sum < x->digits[i];
        x->digits[i] = sum & DIGIT_MAX;
    }

    if (carry != 0) {
        if (resize_sum(x, x->length, 1)) {
            return -1;
        }

        x->digits[x->length - 1] = carry;
    }

    normalize(x);
    return 0;
}

/**
 * Compare the magnitude value of two big integers to determine if the first
 * argument is greater than, equal to or less than the second argument.
 *
 * Arguments:
 * - a: A big integer.
 * - b: A big integer.
 *
 * Return: A positive number if "a" has a greater magnitude than "b", 0 if the
 * magnitude of "a" equals "b" and a negative number if the magnitude "a" is
 * less than "b".
 */
static int magnitude_cmp(const bigint_st *a, const bigint_st *b)
{
    digit_tt a_digit;
    digit_tt b_digit;

    int cmp = 0;

    if (a->length != 0 && a->length == b->length) {
        // We can't use memcmp here because the digits are stored with the
        // least significant digit at index 0 and, for digits with a width of
        // more than one character, due to differences in endianess on
        // different architectures.
        size_t index = a->length - 1;

        while (1) {
            a_digit = a->digits[index];
            b_digit = b->digits[index];
            cmp = (a_digit > b_digit) - (a_digit < b_digit);

            if (cmp == 0 || index-- == 0) {
                break;
            }
        }
    } else {
        cmp = (a->length > b->length) - (a->length < b->length);
    }

    return cmp;
}

/**
 * Compute the difference in magnitude of two big integers.
 *
 * **WARNING:** The minuend must be greater than or equal the subtrahend or
 * this function will not work correctly.
 *
 * Arguments:
 * - dest: Pointer to the output destination.
 * - m: Minuend.
 * - s: Subtrahend.
 *
 * Return: A big integer containing the difference in the magnitudes.
 */
static bigint_st *magnitude_delta(bigint_st *dest, bigint_st *m, bigint_st *s)
{
    bool copied_minuend = false;

    // If either input is also the output, the length will be mutated, so we
    // need to save the original values.
    size_t m_length = m->length;
    size_t s_length = s->length;

    if (resize(dest, m->length)) {
        return NULL;
    }

    for (size_t i = 0; i < m_length; i++) {
        if (i < s_length) {
            // If the minuend's digit is less than the subtrahend's, borrow
            // from the following digits of the minuend. The borrow logic
            // mutates the minuend, so we first make a copy of the original
            // minuend if the output pointer is not also the minuend.
            //
            // Note that we do not modify the subtrahend's digit. When doing
            // this on paper with base 10, the subtrahend's digit would have 10
            // added to it. In this library, that would be analogous to
            // `DIGIT_MAX + 1`. However, unsigned integers use two's complement
            // making adding `DIGIT_MAX + 1` a no-op.
            if (m->digits[i] < s->digits[i]) {
                if (m != dest && !copied_minuend) {
                    if (!(m = bigint_dup(m))) {
                        return NULL;
                    }

                    copied_minuend = true;
                }

                for (size_t j = i + 1; j < m_length; j++) {
                    if (m->digits[j] != 0) {
                        m->digits[j]--;
                        break;
                    }

                    m->digits[j] = DIGIT_MAX;
                }
            }

            dest->digits[i] = m->digits[i] - s->digits[i];
        } else {
            dest->digits[i] = m->digits[i];
        }
    }

    if (copied_minuend) {
        bigint_free(m);
    }

    normalize(dest);
    return dest;
}

/**
 * Compute the sum of the magnitude of two big integers.
 *
 * Arguments:
 * - dest: Pointer to the output destination.
 * - a: Addend.
 * - b: Addend.
 *
 * Return: A big integer containing the sums of the magnitudes.
 */
static bigint_st *magnitude_sum(bigint_st *dest, bigint_st *a, bigint_st *b)
{
    digit_tt sum;

    // We store these separately in case one of the inputs is the dest.
    size_t a_length = a->length;
    size_t b_length = b->length;
    size_t max_length = a_length > b_length ? a_length : b_length;

    bool carry = false;

    if (bigint_eqz(a) && bigint_eqz(b)) {
        bigint_movui(dest, 0);
        goto done;
    }

    if (resize(dest, max_length)) {
        return NULL;
    }

    for (size_t offset = 0; offset < max_length; offset++) {
        if (offset >= a_length) {
            sum = b->digits[offset] + carry;
            carry = sum < b->digits[offset];
        } else if (offset >= b_length) {
            sum = a->digits[offset] + carry;
            carry = sum < a->digits[offset];
        } else {
            sum = a->digits[offset] + b->digits[offset] + carry;
            carry = (sum - carry) < a->digits[offset];
        }

        dest->digits[offset] = sum;
    }

    if (carry != 0) {
        if (resize_sum(dest, dest->length, 1)) {
            return NULL;
        }

        dest->digits[max_length] = carry;
    }

done:
    normalize(dest);
    return dest;
}

/**
 * Clean up any resources created by "bigint_init". This function is one of two
 * in this library that is not thread safe with the other being "bigint_init".
 */
void bigint_cleanup(void)
{
    bigint_st *value;

    if (init_done) {
        for (size_t n = 0; n <= SMALL_NUMBER_CACHE_MAX; n++) {
            value = small_number_cache[n];

            if (value) {
                bigint_free(value);
                small_number_cache[n] = NULL;
            }
        }
    }
}

/**
 * Initialize the state required by the library. This function automatically
 * registers a cleanup function via _atexit(3)_. This function is one of two in
 * this library that is not thread safe with the other being "bigint_cleanup".
 *
 * Return: 0 if the operation succeeds and a negative number if it fails. When
 * specified, the digit super type must be at least twice the width of the base
 * digit type. If this is not the case, this function will set "errno" to
 * `ENOTRECOVERABLE` when it fails.
 */
int bigint_init(void)
{
    bigint_st *value;

    if (init_done) {
        return 0;
    }

#ifdef DIGIT_SUPER_TYPE
    if (sizeof(digit_super_tt) / sizeof(digit_tt) < 2) {
        errno = ENOTRECOVERABLE;
        return -1;
    }
#endif

    for (uintmax_t n = 0; n <= SMALL_NUMBER_CACHE_MAX; n++) {
        if (!(value = bigint_from_uint(n))) {
            for (size_t k = 0; k < n; k++) {
                bigint_free(small_number_cache[k]);
            }

            return -1;
        }

        small_number_cache[n] = value;
    }

    init_done = true;
    return 0;
}

/**
 * Convert a big integer to a standard unsigned integer. If the big integer
 * cannot be represented as an uintmax_t value, "errno" will be set to
 * `ERANGE`.
 *
 * Arguments:
 * - x: Big integer.
 *
 * Return: A normal integer representing the value.
 */
uintmax_t bigint_toui(bigint_st *x)
{
    uintmax_t accumulator = 0;
    uintmax_t previous_accumulator = 0;

    if (x->negative) {
        errno = ERANGE;
        return 0;
    }

    if (x->length > DIGITS_FOR_INTMAX) {
        errno = ERANGE;
        return UINTMAX_MAX;
    }

    for (size_t n = 0; n < x->length; n++) {
        if (!x->digits[n]) {
            continue;
        }

        accumulator |= (uintmax_t) x->digits[n] << (n * DIGIT_BITS);

        if (accumulator <= previous_accumulator) {
            errno = ERANGE;
            return UINTMAX_MAX;
        }

        previous_accumulator = accumulator;
    }

    return accumulator;
}

/**
 * Convert a big integer to a standard integer. If the big integer cannot be
 * represented as an intmax_t value, "errno" will be set to `ERANGE`.
 *
 * Arguments:
 * - x: Big integer.
 *
 * Return: A normal integer representing the value.
 */
intmax_t bigint_toi(bigint_st *x)
{
    uintmax_t accumulator = 0;
    uintmax_t value = 0;

    for (size_t n = 0; n < x->length; n++) {
        value = (uintmax_t) x->digits[n] << (n * DIGIT_BITS);

        if (x->digits[n] != 0 && !value) {
            errno = ERANGE;
            return x->negative ? INTMAX_MIN : INTMAX_MAX;
        }

        accumulator |= value;
    }

    if (!x->negative) {
        if (accumulator > (uintmax_t) INTMAX_MAX) {
            errno = ERANGE;
            return INTMAX_MAX;
        }

        return (intmax_t) accumulator;
    }

    if (accumulator > INTMAX_MIN_MAGNITUDE) {
        errno = ERANGE;
        return INTMAX_MIN;
    }

#if (INTMAX_MIN + INTMAX_MAX) >= 0
    return -((intmax_t) accumulator);
#else
    if (accumulator <= INTMAX_MAX) {
        return -((intmax_t) accumulator);
    } else {
        // This works even if signed integers don't use two's complement.
        return -INTMAX_MAX - (intmax_t) (accumulator - INTMAX_MAX);
    }
#endif
}

/**
 * Convert a big integer to a double. If the big integer cannot be represented
 * as a double, generally because the exponent is too large, "errno" will be
 * set to `EOVERFLOW` and a value representing infinity is returned. The
 * resulting value will be a truncated representation of the integer if it
 * exceeds the precision of the double's mantissa.
 *
 * Arguments:
 * - x: Big integer.
 *
 * Return: A double representing the value.
 */
double bigint_tod(bigint_st *x)
{
    intmax_t intval;
    uintmax_t uintval;
    double coefficient;
    uintmax_t exponent;
    double inf;
    size_t leading_zeroes;
    uintmax_t mantissa;

#ifdef DIGIT_SUPER_TYPE
    int from;
    digit_tt lsb;
    digit_tt msb;
#endif

    if (bigint_eqz(x)) {
        return 0.0;
    }

    errno = 0;

    if (bigint_ltz(x)) {
        intval = bigint_toi(x);

        if (errno == 0) {
            return (double) intval;
        }
    } else {
        uintval = bigint_toui(x);

        if (errno == 0) {
            return (double) uintval;
        }
    }

    // The exponent is the number of bits that will be truncated from the least
    // significant digits of the big integer.
    // TODO: handle multiplication overflow
    leading_zeroes = clz(x);
    exponent = (uintmax_t) (
        (x->length - DIGITS_FOR_INTMAX) * DIGIT_BITS - leading_zeroes
    );

    if (exponent > DBL_MAX_EXP - 1) {
        inf = strtod(x->negative ? "-inf" : "inf", NULL);
        errno = EOVERFLOW;
        return inf;
    } else {
        coefficient = pow(2.0, (double) exponent);
    }

    // Generate a uintmax_t by performing a left shift, if needed, so the first
    // non-zero bit of the big integer becomes the most significant bit of the
    // uintmax_t. After that, the mantissa is generated doing something akin to
    // "*((uintmax_t *) reversed(x->digits))".
    mantissa = 0;

// TODO: are both branches needed?
#ifdef DIGIT_SUPER_TYPE
    if (leading_zeroes) {
        for (size_t i = 0; i < DIGITS_FOR_INTMAX + 1; i++) {
            from = (int) (x->length - i);
            msb = x->digits[from - 1];
            lsb = from >= (int) x->length ? 0 : x->digits[from];
            mantissa <<= DIGIT_BITS;
            mantissa |= DIGIT_MAX & (
                lsb << leading_zeroes | msb >> (DIGIT_BITS - leading_zeroes)
            );
        }
    } else {
        for (size_t i = 0; i < DIGITS_FOR_INTMAX; i++) {
            mantissa <<= DIGIT_BITS;
            mantissa |= x->digits[x->length - i - 1];
        }
    }
#else
    if (x->length == 1) {
        mantissa = x->digits[x->length - 1] << leading_zeroes;
    } else {
        mantissa = (
            x->digits[x->length - 1] << leading_zeroes |
            x->digits[x->length - 2] >> (DIGIT_BITS - leading_zeroes)
        );
    }
#endif

    return (
        coefficient * (x->negative ? -((double) mantissa) : (double) mantissa)
    );
}

/**
 * Perform a left shift on a big integer with the number of bits specified as
 * a standard integer.
 *
 * Arguments:
 * - x: A big integer.
 * - n: Number of bits to shift left.
 *
 * Return: The result of the calculation or `NULL` if there was an error.
 */
bigint_st *bigint_shli(bigint_st *dest, bigint_st *x, size_t n)
{
    int from;
    digit_tt lsb;
    digit_tt msb;
    size_t msb_shift;
    size_t offset;
    size_t original_length;
    size_t new_digits;

    bool free_on_error = false;

    if (!dest) {
        if (!(dest = bigint_dup(x))) {
            return NULL;
        }

        free_on_error = true;
    }

    // If the shift is 0 or the value is 0, this function is a no-op.
    if (n == 0 || bigint_eqz(x)) {
        if (bigint_mov(dest, x)) {
            if (free_on_error) {
                bigint_free(dest);
            }

            return NULL;
        }

        goto done;
    }

    offset = n % DIGIT_BITS;
    original_length = x->length;
    new_digits = n / DIGIT_BITS + (offset != 0);

    if (resize_sum(dest, x->length, new_digits)) {
        if (free_on_error) {
            bigint_free(dest);
        }

        return NULL;
    }

    if (offset == 0) {
        for (size_t i = 0; i < original_length; i++) {
            dest->digits[original_length - 1 - i + new_digits] = (
                x->digits[original_length - 1 - i]
            );
        }
    } else {
        // When the shifts are not aligned with digit boundaries, "n" bits come
        // from one digit and a complementary number of bits (`DIGIT_BITS - n`)
        // come from another.
        msb_shift = DIGIT_BITS - offset;

        for (size_t i = 0; i <= original_length; i++) {
            from = (int) (original_length - i);
            msb = from < 1 ? 0 : x->digits[from - 1];
            lsb = from >= (int) original_length ? 0 : x->digits[from];
            dest->digits[(size_t) from + new_digits - 1] = (
                (lsb << offset | msb >> msb_shift) & DIGIT_MAX
            );
        }
    }

    // Zero out any holes that were created.
    memset(dest->digits, 0, sizeof(digit_tt) * (new_digits - (offset != 0)));

done:
    dest->negative = x->negative;
    normalize(dest);
    return dest;
}

/**
 * Perform a left shift with big integers.
 *
 * Arguments:
 * - x: A big integer.
 * - n: Number of bits to shift left.
 *
 * Return: The result of the calculation or `NULL` if there was an error.
 */
bigint_st *bigint_shl(bigint_st *dest, bigint_st *x, bigint_st* n)
{
    uintmax_t bits;

    if (bigint_ltz(n)) {
        errno = EDOM;
    } else {
        errno = 0;
        bits = bigint_toui(n);

        if (errno == 0) {
            return bigint_shli(dest, x, (size_t) bits);
        }
    }

    return NULL;
}

/**
 * Perform a right shift on a big integer with the number of bits specified as
 * a standard integer.
 *
 * Arguments:
 * - x: A big integer.
 * - n: Number of bits to shift right.
 *
 * Return: The result of the calculation or `NULL` if there was an error.
 */
bigint_st *bigint_shri(bigint_st *dest, bigint_st *x, size_t n)
{
    size_t offset;
    size_t shifted_digits;

    if (!dest && !(dest = bigint_dup(x))) {
        return NULL;
    }

    // If the shift is 0 or the value is 0, this function is a no-op.
    if (n == 0 || bigint_eqz(x)) {
        if (bigint_mov(dest, x)) {
            return NULL;
        }

        goto done;
    }

    if (n >= (DIGIT_BITS * x->length)) {
        bigint_movui(dest, 0);
        goto done;
    }

    offset = n % DIGIT_BITS;
    shifted_digits = n / DIGIT_BITS + (offset != 0);

    if (offset == 0) {
        for (size_t i = 0; i < x->length; i++) {
            dest->digits[i] = x->digits[i + shifted_digits];
        }
    } else {
        // As with the left shift logic, shifts for unaligned offsets depend on
        // two digits instead of one.

        for (size_t from, i = 0; i < x->length; i++) {
            from = shifted_digits + i;
            digit_tt lsb = x->digits[from - 1];
            digit_tt msb = from < x->length ? x->digits[from] : 0;
            dest->digits[i] = DIGIT_MAX & (
                msb << (DIGIT_BITS - offset) | lsb >> offset
            );
        }
    }

    // No error checking since shrinking cannot fail.
    (void) resize(dest, x->length - shifted_digits + (offset != 0)); // TODO: figure out why this works

done:
    dest->negative = x->negative;
    normalize(dest);
    return dest;
}

/**
 * Perform a right shift with big integers.
 *
 * Arguments:
 * - x: A big integer.
 * - n: Number of bits to shift right.
 *
 * Return: The result of the calculation or `NULL` if there was an error.
 */
bigint_st *bigint_shr(bigint_st *dest, bigint_st *x, bigint_st* n)
{
    uintmax_t bits;

    if (bigint_ltz(n)) {
        errno = EDOM;
    } else {
        errno = 0;
        bits = bigint_toui(n);

        if (errno == 0) {
            return bigint_shri(dest, x, bits);
        }
    }

    return NULL;
}

/**
 * Compare two big integers to determine if the first argument is greater than,
 * equal to or less than the second argument.
 *
 * Arguments:
 * - a: A big integer.
 * - b: A big integer.
 *
 * Return: A positive number if "a" is greater than "b", 0 if "a" equals "b"
 * and a negative number if "a" is less than "b".
 */
int bigint_cmp(const bigint_st *a, const bigint_st *b)
{
    int cmp;

    if (a->negative == b->negative) {
        cmp = magnitude_cmp(a, b);
        return a->negative ? -cmp : cmp;
    }

    return a->negative ? -1 : 1;
}

/**
 * Increment the value of a big integer by 1.
 *
 * Arguments:
 * - x: A big integer.
 *
 * Return: 0 if the operation succeeds and a negative value otherwise.
 */
int bigint_inc(bigint_st *x)
{
    return bigint_ltz(x) ? magnitude_dec(x) : magnitude_inc(x);
}

/**
 * Decrement the value of a big integer by 1.
 *
 * Arguments:
 * - x: A big integer.
 *
 * Return: 0 if the operation succeeds and a negative value otherwise.
 */
int bigint_dec(bigint_st *x)
{
    if (bigint_eqz(x)) {
        x->length = 1;
        x->digits[0] = 1;
        x->negative = true;
        return 0;
    }

    return bigint_ltz(x) ? magnitude_inc(x) : magnitude_dec(x);
}

/**
 * Add two big integers.
 *
 * Arguments:
 * - dest: Pointer to the output destination. If this is NULL, a heap pointer
 *   is returned that the caller is responsible for freeing with "bigint_free".
 * - a: Addend.
 * - b: Addend.
 *
 * Return: A pointer to the result of the calculation.
 */
bigint_st *bigint_add(bigint_st *dest, bigint_st *a, bigint_st *b)
{
    int cmp;
    bigint_st *result;

    bool free_dest_on_error = false;

    if (!dest) {
        if (!(dest = bigint_from_int(0))) {
            return NULL;
        }

        free_dest_on_error = true;
    }

    cmp = magnitude_cmp(a, b);

    // TODO: check return value of magnitude_sum, *_delta; free dest if allocated.
    if (a->negative == b->negative) {
        dest->negative = a->negative;
        result = magnitude_sum(dest, a, b);
    } else if (cmp > 0) {
        dest->negative = a->negative;
        result = magnitude_delta(dest, a, b);
    } else if (cmp < 0) {
        dest->negative = b->negative;
        result = magnitude_delta(dest, b, a);
    } else {
        bigint_movui(dest, 0);
        result = dest;
    }

    if (!result && free_dest_on_error) {
        bigint_free(dest);
    }

    return result;
}

/**
 * Subtract one big integer from another.
 *
 * Arguments:
 * - out: Pointer to the output destination. If this is NULL, a heap pointer is
 *   returned that the caller is responsible for freeing with "bigint_free".
 * - a: Minuend.
 * - b: Subtrahend.
 *
 * Return: A pointer to the result of the calculation.
 */
bigint_st *bigint_sub(bigint_st *dest, bigint_st *a, bigint_st *b)
{
    int cmp;
    bigint_st* result;

    bool free_dest_on_error = false;

    if (dest) {
        if (dest != a && dest != b) {
            bigint_movui(dest, 0);
        }
    } else if ((dest = bigint_from_int(0))) {
        free_dest_on_error = true;
    } else {
        return NULL;
    }

    cmp = magnitude_cmp(a, b);

    // +a, +b
    if (!a->negative && !b->negative) {
        if (cmp >= 0) {
            result = magnitude_delta(dest, a, b);
            dest->negative = false;
        } else {
            result = magnitude_delta(dest, b, a);
            dest->negative = true;
        }
    // +a, -b
    } else if (!a->negative && b->negative) {
        result = magnitude_sum(dest, a, b);
        dest->negative = false;
    // -a, +b
    } else if (a->negative && !b->negative) {
        result = magnitude_sum(dest, a, b);
        dest->negative = (bool) dest->length;
    // -a, -b
    } else if (cmp >= 0) {
        result = magnitude_delta(dest, a, b);
        dest->negative = (bool) dest->length;
    } else {
        result = magnitude_delta(dest, b, a);
        dest->negative = false;
    }

    if (!result && free_dest_on_error) {
        bigint_free(dest);
    }

    return result;
}

/**
 * Multiple two big integers.
 *
 * Arguments:
 * - dest: Pointer to the output destination. If this is NULL, a heap pointer
 *   is returned that the caller is responsible for freeing with "bigint_free".
 * - a: Multiplicand.
 * - b: Multiplicand.
 *
 * Return: A pointer to the result of the calculation if it succeeds and `NULL`
 * otherwise.
 */
bigint_st *bigint_mul(bigint_st *dest, bigint_st *a, bigint_st *b)
{
    digit_tt a_i;
    digit_tt b_j;
    size_t max_factor_length;
    bigint_st *original_dest;

#ifdef DIGIT_SUPER_TYPE
    digit_super_tt carry;
    digit_super_tt product;
#else
    digit_tt carry;
    digit_tt product;
#endif

    original_dest = dest;

    if (!dest) {
        dest = bigint_from_int(0);
    } else if (dest == a || dest == b) {
        // The destination value is used as an accumulator, so if the
        // destination happens to also be one of the inputs, we first make a
        // new structure which will be copied to the destination once the
        // calculations are done.
        if (!(dest = bigint_dup(dest))) {
            return NULL;
        }
    }

    if (!dest) {
        return NULL;
    }

    if (bigint_eqz(a) || bigint_eqz(b)) {
        dest->length = 0;
        dest->negative = false;
        return dest;
    }

    if (a->length > 1 && bigint_is_power_of_2(a)) {
        if (!bigint_shli(dest, b, ctz(a))) {
            // TODO: free dest if allocated
            return NULL;
        }

        goto done;
    }

    if (b->length > 1 && bigint_is_power_of_2(b)) {
        if (!bigint_shli(dest, a, ctz(b))) {
            // TODO: free dest if allocated
            return NULL;
        }

        goto done;
    }

    if (bigint_eqz(a) || bigint_eqz(b)) {
        bigint_movui(dest, 0);
        return dest;
    }

    // Allocate as much space as we could possibly need up front.
    if (resize_sum(dest, a->length, b->length)) {
        return NULL;
    } else {
        // Clear the contents of the destination because its digits are used as
        // accumulators during intermediate calculations.
        memset(dest->digits, 0, dest->allocated * sizeof(digit_tt));
    }

    max_factor_length = a->length > b->length ? a->length : b->length;

    for (size_t i = 0; i < max_factor_length; i++) {
        carry = 0;

        for (size_t j = 0; j < max_factor_length; j++) {
            a_i = i < a->length ? a->digits[i] : 0;
            b_j = j < b->length ? b->digits[j] : 0;

#ifdef DIGIT_SUPER_TYPE
            product = dest->digits[i + j] + carry + a_i * b_j;
            carry = DIGIT_MAX & (product >> DIGIT_BITS);
            product = DIGIT_MAX & product;
#else
            u128fma64(&carry, &product, a_i, b_j, carry);
            u128add64(&carry, &product, dest->digits[i + j]);
#endif
            dest->digits[i + j] = (digit_tt) product;

            if (product != 0 && (i + j + 1 > dest->length)) {
                dest->length = i + j + 1;
            }
        }

        if (carry != 0) {
            if (i + max_factor_length + 1 > dest->length) {
                dest->length = i + max_factor_length + 1;
            }

            dest->digits[i + max_factor_length] = (digit_tt) carry;
        }
    }

done:
    if (original_dest && dest != original_dest) {
        xfree(original_dest->digits);
        *original_dest = *dest;
        xfree(dest);
        dest = original_dest;
    }

    dest->negative = a->negative != b->negative;
    normalize(dest);
    return dest;
}

/**
 * Divide one big integer by another.
 *
 * Arguments:
 * - q: Quotient; pointer to the output destination. If this is NULL, a heap
 *   pointer is returned that the caller is responsible for freeing with
 *   "bigint_free".
 * - r: Optional output pointer for the remainder.
 * - n: Numerator.
 * - d: Denominator.
 *
 * Return: A pointer to the result of the calculation. If there were any
 * errors, this will be `NULL` and "errno" will be set accordingly. "EDOM" is
 * used to indicate division by zero.
 */
bigint_st *bigint_div(bigint_st *q, bigint_st **r, bigint_st *n, bigint_st *d)
{
    // TODO: handle q and/or r being n and/or d.
    int cmp;

    bigint_st *accumulator = NULL;
    bool free_q_on_failure = false;
    size_t hidden_digits = 0;
    bigint_st *intermediate = NULL;
    size_t quotient_digits = 0;

    // Cannot divide by 0.
    if (bigint_eqz(d)) {
        errno = EDOM;
        return NULL;
    }

    if (!q) {
        if (!(q = bigint_from_int(0))) {
            return NULL;
        }

        free_q_on_failure = true;
    }

    if (d->length == 1 && d->digits[0] == 1) {
        // Anything divided by 1 is itself with a remainder of 0.
        if (r) {
            if (*r) {
                bigint_movui(*r, 0);
            } else if (!(*r = bigint_from_int(0))) {
                goto error;
            }
        }

        if (bigint_mov(q, n)) {
            goto error;
        }

        goto set_signs;
    }

    cmp = magnitude_cmp(n, d);

    if (cmp == 0) {
        // If the numerator and denominator are equal, the result is always 1
        // with a remainder of 0.
        if (r) {
            if (*r) {
                bigint_movui(*r, 0);
            } else if (!(*r = bigint_from_int(0))) {
                goto error;
            }
        }

        bigint_movui(q, 1);
        goto set_signs;
    } else if (cmp < 0) {
        // If the numerator has a magnitude less than the denominator, the
        // result will always be 0 and the remainder will be the numerator.
        if (r) {
            if (*r) {
                if (bigint_mov(*r, n)) {
                    goto error;
                }
            } else if (!(*r = bigint_dup(n))) {
                goto error;
            }
        }

        bigint_movui(q, 0);
        goto set_signs;
    }

    if (bigint_is_power_of_2(d)) {
        if (!bigint_shri(q, n, ctz(d))) {
            goto error;
        }

        if (r) {
            if (*r) {
                bigint_movui(*r, 0);
            } else if (!(*r = bigint_dup(n))) {
                goto error;
            } else {
                bigint_movui(*r, 0);
            }
        }

        goto set_signs;
    }

    if (!(accumulator = bigint_from_int(0))) {
        goto error;
    }

    if (r && *r) {
        if (bigint_mov((intermediate = *r), n)) {
            goto error;
        }
    } else if (!(intermediate = bigint_dup(n))) {
        goto error;
    }

    if (resize(q, n->length)) {
        goto error;
    }

    // The intermediate quotient starts out with the contents of the numerator.
    // Then we truncate it so it has the same length as the denominator. The
    // truncation is done by modifying the metadata and array offset so the
    // hidden digits can be unveiled by undoing these adjustments.
    hidden_digits = n->length - d->length;
    intermediate->digits += hidden_digits;
    intermediate->length = d->length;

    do {
        // While the intermediate quotient is less than the divisor, pull down
        // a digit from the numerator by unveiling the next value in the digits
        // array.
        cmp = magnitude_cmp(intermediate, d);

        while (cmp < 0) {
            if (hidden_digits == 0) {
                goto quotient_complete;
            }

            intermediate->length++;
            intermediate->digits--;
            hidden_digits--;

            // If pulling down a digit did not create a number greater than or
            // equal to the divisor, the corresponding quotient digit is 0.
            if ((cmp = magnitude_cmp(intermediate, d)) < 0) {
                q->digits[q->length - quotient_digits++ - 1] = 0;
            }
        }

        // Once we have an intermediate quotient that is larger than the
        // denominator, we need to figure out what factor to multiply the
        // denominator by to get the largest value that is less than or equal
        // to the intermediate quotient.
        bigint_movui(accumulator, 0);

        for (digit_tt factor = 1; factor <= DIGIT_MAX; factor++) {
            if (!magnitude_sum(accumulator, accumulator, d)) {
                goto error;
            }

            cmp = magnitude_cmp(accumulator, intermediate);

            if (cmp >= 0 || factor == DIGIT_MAX) {
                // If accumulator overshot the intermediate quotient, undo the
                // last increment.
                if (cmp > 0) {
                    MAGNITUDE_RDELTA(accumulator, d);
                    factor--;
                }

                MAGNITUDE_RDELTA(intermediate, accumulator);
                q->digits[q->length - quotient_digits++ - 1] = factor;
                break;
            }
        }
    } while (hidden_digits);

quotient_complete:
    bigint_free(accumulator);

    // The final value of the intermediate quotient is the remainder.
    if (r) {
        *r = intermediate;
    } else {
        bigint_free(intermediate);
    }

    // When generating the result, we fill the quotient starting at the most
    // significant digit, so we need to shift the result to get rid of the
    // digits that were never populated.
    bigint_shri(q, q, DIGIT_BITS * (q->length - quotient_digits));
    q->length = quotient_digits;

set_signs:
    // Sign rules for the quotient and remainder use the same rules that C does
    // for standard integer types.
    if (n->negative && d->negative) {
        q->negative = false;

        if (r && bigint_nez(*r)) {
            (*r)->negative = true;
        }
    } else if (n->negative && !d->negative) {
        if (bigint_nez(q)) {
            q->negative = true;
        }

        if (r && bigint_nez(*r)) {
            (*r)->negative = true;
        }
    } else if (!n->negative && d->negative) {
        if (bigint_nez(q)) {
            q->negative = true;
        }

        if (r) {
            (*r)->negative = false;
        }
    }

    return q;

error:
    // We need to move the intermediate quotient's digits pointer back to its
    // original position so free(3) works.
    intermediate->digits -= hidden_digits;

    if (free_q_on_failure) {
        bigint_free(q);
    }

    if (intermediate) {
        bigint_free(intermediate);
    }

    if (accumulator) {
        bigint_free(accumulator);
    }

    return NULL;
}

/**
 * Compute the module of one integer by another.
 *
 * Arguments:
 * - r: Remainder; pointer to the output destination. If this is NULL, a heap
 *   pointer is returned that the caller is responsible for freeing with
 *   "bigint_free".
 * - n: Numerator.
 * - d: Denominator.
 *
 * Return: A pointer to the result of the calculation. If there were any
 * errors, this will be `NULL` and "errno" will be set accordingly. "EDOM" is
 * used to indicate division by zero.
 */
bigint_st *bigint_mod(bigint_st *r, bigint_st *n, bigint_st *d)
{
    bool free_r_on_error = false;
    bigint_st *q = NULL;

    if (!r) {
        if (!(r = bigint_from_int(0))) {
            return NULL;
        }

        free_r_on_error = true;
    }

    if (!bigint_div(q, &r, n, d)) {
        if (free_r_on_error) {
            bigint_free(r);
        }

        r = NULL;
    }

    bigint_free(q);
    return r;
}

/**
 * Compute the value of a number raise to an exponent.
 *
 * Arguments:
 * - base: The base big integer.
 * - exp: The exponent the base is raised to as a big integer.
 *
 * Return: A pointer to the result of the calculation if it succeeds or `NULL`
 * if it fails. If the exponent is less than 0, "errno" is set to `EDOM`.
 */
bigint_st *bigint_pow(bigint_st* dest, bigint_st *base, bigint_st* exp)
{
    bool free_dest_on_error = false;
    bool negative = bigint_nez(exp) && bigint_ltz(base) && exp->digits[0] & 1;

    if (bigint_ltz(exp)) {
        errno = EDOM;
        return NULL;
    }

    // TODO: handle exp = 1 and base = 0 in a way that avoid unnecessary dup
    // calls.
    if (!(exp = bigint_dup(exp))) {
        goto error;
    }

    if (!(base = bigint_dup(base))) {
        goto error;
    }

    if (dest) {
        bigint_movui(dest, 1);
    } else {
        dest = bigint_from_int(1);
        free_dest_on_error = true;
    }

    if (bigint_eqz(exp)) {
        bigint_movui(dest, 1);
        goto done;
    }

    if (bigint_eqz(base)) {
        bigint_movui(dest, 0);
        goto done;
    }

    if (!dest) {
        goto error;
    }

    while (1) {
        if ((exp->digits[0] & 1) && !bigint_mul(dest, dest, base)) {
            goto error;
        }

        bigint_shri(exp, exp, 1);

        if (bigint_eqz(exp)) {
            break;
        }

        if (!bigint_mul(base, base, base)) {
            goto error;
        }
    }

done:
    dest->negative = negative;
    bigint_free(base);
    bigint_free(exp);
    return dest;

error:
    if (base) {
        bigint_free(base);
    }

    if (free_dest_on_error) {
        bigint_free(dest);
    }

    if (exp) {
        bigint_free(exp);
    }

    return NULL;
}

/**
 * Convert a string to a big integer. This function supports hexadecimal
 * indicated by the prefix "0x" and "0X"; octal octal by "0o", "0O" or simply
 * "0"; and binary by "0b" and "0B". Otherwise, the string is parsed as a
 * decimal value. Decimal parsing supports scientific notation with integer
 * coefficients and positive exponents like "1e100" and "12E3".
 *
 * Arguments:
 * - str: Text to convert to a big integer.
 * - fraction: Optional output pointer that will be used to indicate where the
 *   unused fractional value of the input begins. It ends at the first
 *   non-decimal-digit character following this pointer.
 *
 * Return: A big integer if the parsing succeeds and `NULL` if it fails.
 * "errno" is set to `EINVAL` when the string is malformed.
 */
bigint_st *bigint_strtobif(const char *str, const char **fraction)
{
    bigint_st *dest;
    bigint_st *result;
    unsigned char value;

    const char *decimal = NULL;
    const char *eom = NULL;
    unsigned char base = 10;
    bigint_st *exponent = NULL;
    bool negative = false;

    if (!(result = bigint_from_int(0))) {
        return NULL;
    }

    dest = result;

    if (*str == '+') {
        str++;
    } else if (*str == '-') {
        str++;
        negative = true;
    }

    if (*str == '0') {
        switch (*++str) {
          case 'b':
          case 'B':
            base = 2;
            str++;
            break;

          case 'o':
          case 'O':
            base = 8;
            str++;
            break;

          case 'x':
          case 'X':
            base = 16;
            str++;
            break;

          default:
            // TODO: deal with 0e123
            base = strchr(str, '.') ? 10 : 8;
        }
    }

    for (; *str; str++) {
        if (base == 10) {
            if ((*str | 32) == 'e') {
                if (exponent) {
                    errno = EINVAL;
                    goto error;
                } else if (!(exponent = bigint_from_int(0))) {
                    goto error;
                } else {
                    if (decimal) {
                        eom = str - 1;
                    }

                    dest = exponent;
                    continue;
                }
            } else if (*str == '.') {
                // A decimal point can only appear before the exponent and only
                // once.
                if (exponent || decimal) {
                    errno = EINVAL;
                    goto error;
                }

                decimal = str;
                continue;
            }
        }

        if (*str >= '0' && *str <= '9') {
            value = (unsigned char) *str - '0';
        } else if ((*str | 32) >= 'a' && (*str | 32) <= 'z') {
            value = (unsigned char) (*str | 32) - 'a' + 10;
        } else {
            errno = EINVAL;
            goto error;
        }

        if (value >= base) {
            errno = EINVAL;
            goto error;
        }

        // This loop only processes values to the left of any decimal point and
        // in the exponent.
        if (!decimal || exponent) {
            if (!bigint_mul(dest, dest, small_number_cache[base])) {
                goto error;
            }

            if (!magnitude_sum(dest, dest, small_number_cache[value])) {
                goto error;
            }
        }
    }

    // Fail if there's no exponent after "e" or "E".
    if (exponent && (*(str - 1) | 32) == 'e') {
        errno = EINVAL;
        goto error;
    }

    if (exponent && decimal /* && base == 10 */) {
        if (!eom) {
            eom = str - 1;
        }

        // Move end-of-mantissa marker to the last significant digit after the
        // decimal point.
        while (*eom == '0') {
            eom--;
        }

        if (eom != decimal) {
            decimal++;

            while (bigint_nez(exponent) && decimal <= eom) {
                if (bigint_dec(exponent) || !bigint_mul(result, result, TEN)) {
                    goto error;
                }

                value = (unsigned char) *decimal++ - '0';

                if (!magnitude_sum(result, result, small_number_cache[value])) {
                    goto error;
                }
            }

            if (fraction && decimal != eom) {
                *fraction = decimal;
            }
        }
    }

    normalize(result);

    if (exponent) {
        if (!bigint_pow(exponent, TEN, exponent)) {
            goto error;
        }

        if (!bigint_mul(result, result, exponent)) {
            goto error;
        }

        bigint_free(exponent);
    }

    if (bigint_nez(result)) {
        result->negative = negative; // TODO: this used to be dest. Verify correct.
    }

    return result;

error:
    if (exponent) {
        bigint_free(exponent);
    }

    bigint_free(dest);
    return NULL;
}

bigint_st *bigint_strtobi(const char *str)
{
    return bigint_strtobif(str, NULL);
}

/**
 * Write a binary, octal, decimal or hexadecimal representation of a big
 * integer to a string buffer.
 *
 * Arguments:
 * - buf: The destination buffer.
 * - buflen: The size of the destination buffer.
 * - x: The big integer to write.
 * - base: The base. This must be 2, 8, 10 or 16.
 *
 * Return: If the operation succeeds, the number of characters written
 * excluding the terminating NUL byte. If the operation fails, a negative value
 * is returned. The buffer being too short is treated as an error, so this
 * function will return negative value in that scenario.
 */
int bigint_snbprint(char *buf, size_t buflen, bigint_st *x, unsigned char base)
{
    char *number_start;
    char hexoctbuf[33];
    int result;

    bigint_st *accumulator = NULL;
    char *cursor = buf;
    digit_tt numeral = 0;
    bigint_st *remainder = NULL;
    size_t written = 0;

    if (base != 2 && base != 8 && base != 10 && base != 16) {
        errno = EINVAL;
        return -1;
    } else if (base != 10) {
        if (buflen < 4 /* "0x", first digit, '\0' */ + x->negative) {
            errno = ERANGE;
            return -1;
        }

        if (base == 2) {
            strcpy(buf, x->negative ? "-0b" : "0b");
        } else if (base == 8) {
            strcpy(buf, x->negative ? "-0o" : "0o");
        } else if (base == 16) {
            strcpy(buf, x->negative ? "-0x" : "0x");
        }

        written = 2 /* "0x" */ + x->negative;
        cursor += written;
    } else if (buflen < 2 /* len(first digit + '\0') */ + x->negative) {
        errno = ERANGE;
        return -1;
    } else if (x->negative) {
        *cursor++ = '-';
        written++;
    }

    if (bigint_eqz(x)) {
        strcpy(cursor, "0");
        written++;
        return (int) written;
    }

    number_start = cursor;

    if (base == 8 || base == 16) {
        for (size_t n = 0; n < x->length; n++) {
            if (base == 8) {
                result = sprintf(
                    hexoctbuf, DIGIT_OCT_TEMPLATE, x->digits[x->length - n - 1]
                );
            } else if (base == 16) {
                result = sprintf(
                    hexoctbuf, DIGIT_HEX_TEMPLATE, x->digits[x->length - n - 1]
                );
            } else {
                // This should be unreachable.
                errno = EINVAL;
                result = -1;
            }

            if (result < 0) {
                return -1;
            }

            written += (size_t) result;

            if (written + 1 > buflen) {
                errno = ERANGE;
                return -1;
            }

            strcat(buf, hexoctbuf);
        }

        // Trim leading zeroes.
        if (*number_start == '0') {
            while (*++cursor == '0');
            while ((*number_start++ = *cursor++));
        }
    } else if (!(accumulator = bigint_dup(x))) {
        goto error;
    } else if (base == 10 && !(remainder = bigint_from_int(0))) {
        goto error;
    } else {
        while (bigint_nez(accumulator)) {
            if (base == 10) {
                if (!bigint_div(accumulator, &remainder, accumulator, TEN)) {
                    goto error;
                }

                numeral = remainder->digits[0];
            } else if (base == 2) {
                numeral = accumulator->digits[0] & 1;
                bigint_shri(accumulator, accumulator, 1);
            } else {
                // This should be unreachable.
                errno = EINVAL;
                goto error;
            }

            if (written + 2 /* +1 for next char, +1 for NUL */ > buflen) {
                errno = ERANGE;
                goto error;
            }

            *cursor++ = (char) ((numeral > 9 ? 'a' - 10 : '0') + numeral);
            written++;
        }

        *cursor = '\0';

        if (remainder) {
            bigint_free(remainder);
        }

        bigint_free(accumulator);

        // Reverse the numerical part of the string because it's generated
        // starting with the least significant digit.
        for (char temp, *a = number_start, *b = cursor - 1; a < b; a++, b--) {
            temp = *a;
            *a = *b;
            *b = temp;
        }
    }

    return (int) written;

error:
    if (accumulator) {
        bigint_free(accumulator);
    }

    if (remainder) {
        bigint_free(remainder);
    }

    return -1;
}

/**
 * Write the decimal representation of a big integer to a string buffer.
 *
 * Arguments:
 * - buf: The destination buffer.
 * - buflen: The size of the destination buffer.
 * - x: The big integer to write.
 *
 * Return: If the operation succeeds, the number of characters written
 * excluding the terminating NUL byte. If the operation fails, a negative value
 * is returned. The buffer being too short is treated as an error, so this
 * function will return negative value in that scenario.
 */
int bigint_snprint(char *buf, size_t buflen, bigint_st *x)
{
    return bigint_snbprint(buf, buflen, x, 10);
}

/**
 * Get a binary, octal, decimal or hexadecimal representation of a big
 * integer.
 *
 * Arguments:
 * - buf: The destination buffer.
 * - buflen: The size of the destination buffer.
 * - x: The big integer to write.
 * - base: The base. This must be 2, 8, 10 or 16.
 *
 * Return: If the operation succeeds, a heap-allocated string is retuned that
 * the caller must free. If the operation fails, NULL is returned.
 */
char *bigint_tostrb(bigint_st *x, unsigned char base)
{
    char *buffer;
    size_t bits_per_numeral;
    size_t max_length;

    size_t bits_in_value = DIGIT_BITS * (x->length != 0 ? x->length : 1);

    switch (base) {
      case 2:
        bits_per_numeral = 1;
        break;

      case 8:
      case 10:
        bits_per_numeral = 3;
        break;

      case 16:
        bits_per_numeral = 4;
        break;

      default:
        errno = EINVAL;
        return NULL;
    }

    max_length = (
        x->negative +                               // '-'
        (base == 10 ? 0 : 2) +                      // "0b"/"0o/"0x"
        CEIL_DIV(bits_in_value, bits_per_numeral) + // Space for numerals
        1                                           // '\0'
    );

    if (!(buffer = malloc(max_length))) {
        return NULL;
    }

    if (bigint_snbprint(buffer, max_length, x, base) < 0) {
        xfree(buffer);
        return NULL;
    }

    return buffer;
}

/**
 * Get the decimal representation of a big integer.
 *
 * - x: A big integer.
 *
 * Return: If the operation succeeds, a heap-allocated string is retuned that
 * the caller must free. If the operation fails, NULL is returned.
 */
char *bigint_tostr(bigint_st *x)
{
    return bigint_tostrb(x, 10);
}

/**
 * Compute the logarithm of a big integer with the base specified as a standard
 * unsigned integer.
 *
 * Arguments:
 * - dest: Output destination. If this is NULL, it will be allocated.
 * - x: Value for which the logarithm should be computed.
 * - base: Logarithm base.
 *
 * Return: A pointer to the absolute value or NULL if the function failed. If
 * the base is less than two, this function will fail with errno set to EDOM.
 */
bigint_st *bigint_logui(bigint_st *dest, bigint_st *x, uintmax_t base)
{
    bigint_st *base_bi;
    bigint_st *product;
    uintmax_t floor_log2;
    uintmax_t ratio;
    uintmax_t power;
    int cmp;

    bool free_dest_on_error = false;

    if (bigint_lez(x) || base < 2) {
        errno = EDOM;
        return NULL;
    }

    if (!dest) {
        dest = bigint_from_int(0);
        free_dest_on_error = true;
    }

    if (POWER_OF_2(base)) {
        if (SIZE_MAX / x->length < DIGIT_BITS) {
            errno = ERANGE;
            return NULL;
        }

        floor_log2 = DIGIT_BITS * x->length - clz(x) - 1;

        if (floor_log2 > INTMAX_MAX) {
            errno = ERANGE;
            return NULL;
        }

        for (ratio = 1; base > 2; ratio++) {
            base /= 2;
        }

        return bigint_from_uint(floor_log2 / ratio);
    } else {
        base_bi = bigint_from_uint(base);

        if (!base_bi) {
            return NULL;
        }

        product = bigint_from_int(1);

        if (!product) {
            goto error;
        }

        for (power = 0; (cmp = magnitude_cmp(x, product)) > 0; power++) {
            if (power == UINTMAX_MAX) {
                errno = ERANGE;
                goto error_after_product_alloc;
            } else if (!bigint_mul(product, product, base_bi)) {
                goto error_after_product_alloc;
            }
        }

        if (cmp == 0 && magnitude_inc(product)) {
            goto error_after_product_alloc;
        }

        bigint_movui(dest, power);
        bigint_free(product);
        bigint_free(base_bi);
        return dest;

error_after_product_alloc:
        bigint_free(product);

error:
        bigint_free(base_bi);

        if (free_dest_on_error) {
            bigint_free(dest);
        }

        return NULL;
    }
}

/**
 * Compute the absolute value of a big integer.
 *
 * Arguments:
 * - dest: Output destination. If this is NULL, it will be allocated.
 * - x: Target value.
 *
 * Return: A pointer to the absolute value or NULL if the function failed.
 */
bigint_st *bigint_abs(bigint_st *dest, bigint_st *x)
{
    if (dest != x) {
        if (!dest) {
            dest = bigint_dup(x);
        } else if (bigint_mov(dest, x)) {
            dest = NULL;
        }
    }

    if (dest) {
        dest->negative = false;
    }

    return dest;
}

/**
 * Get the greatest common denominator of two big integers.
 *
 * Arguments:
 * - a: A big integer.
 * - b: A big integer.
 *
 * Return: A pointer to the destination if the operation succeeded or NULL if
 * it failed.
 */
bigint_st *bigint_gcd(bigint_st *dest, bigint_st *a, bigint_st* b)
{
    size_t a_zeroes;
    size_t b_zeroes;
    size_t common_zeroes;
    bigint_st *temp;

    bool free_dest_on_error = false;

    if (!(a = bigint_dup(a))) {
        return NULL;
    }

    if (!(b = bigint_dup(b))) {
        goto error_after_a_dup;
    }

    if (!dest) {
        if (!(dest = bigint_from_int(0))) {
            goto error_after_b_dup;
        }

        free_dest_on_error = true;
    }

    // This function was adapted from
    // <https://en.wikipedia.org/w/index.php?title=Binary_GCD_algorithm&oldid=1203806811#Implementation>.
    a->negative = false;
    b->negative = false;

    if (bigint_eqz(a)) {
        if (bigint_mov(dest, b)) {
            goto error_after_dest_alloc;
        }
    } else if (bigint_eqz(b)) {
        if (bigint_mov(dest, a)) {
            goto error_after_dest_alloc;
        }
    } else {
        a_zeroes = ctz(a);
        b_zeroes = ctz(b);
        common_zeroes = a_zeroes < b_zeroes ? a_zeroes : b_zeroes;

        if (!bigint_shri(a, a, a_zeroes)) {
            goto error_after_dest_alloc;
        }

        if (!bigint_shri(b, b, b_zeroes)) {
            goto error_after_dest_alloc;
        }

        while (1) {
            if (magnitude_cmp(a, b) > 0) {
                temp = a;
                a = b;
                b = temp;
            }

            MAGNITUDE_RDELTA(b, a);

            if (bigint_eqz(b)) {
                if (bigint_shli(dest, a, common_zeroes)) {
                    break;
                }

                goto error_after_dest_alloc;
            }

            if (!bigint_shri(b, b, ctz(b))) {
                goto error_after_dest_alloc;
            }
        }
    }

    bigint_free(b);
    bigint_free(a);
    return dest;

error_after_dest_alloc:
    if (free_dest_on_error) {
        bigint_free(dest);
    }

error_after_b_dup:
    bigint_free(b);

error_after_a_dup:
    bigint_free(a);

    return NULL;
}
