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

## Memory Management ##

### bigint_init ###

**Signature:** `int bigint_init(void)`

**Description:**
Initialize the state required by the library. This function is one of two in
this library that is not thread safe with the other being "bigint_cleanup".

**Return:** 0 if the operation succeeds and a negative number if it fails. When
specified, the digit super type must be at least twice the width of the base
digit type. If this is not the case, this function will set "errno" to
`ENOTRECOVERABLE` when it fails.

### bigint_cleanup ###

**Signature:** `void bigint_cleanup(void)`

**Description:**
Clean up any resources created by "bigint_init". This function is one of two
in this library that is not thread safe with the other being "bigint_init".

### bigint_free ###

**Signature:** `void bigint_free(bigint_st *x)`

**Description:**
Release the resources associated with a big integer. This function is
guaranteed to preserve errno.

**Arguments:**
- **x:** A big integer.

### bigint_dup ###

**Signature:** `bigint_st *bigint_dup(bigint_st *x)`

**Description:**
Duplicate a big integer. The caller is responsible for calling "bigint_free"
when the structure is no longer needed.

**Arguments:**
- **x:** Value to duplicate.

**Return:** A pointer to the duplicated structure or `NULL` if it could not be
duplicated in which case "errno" will be set appropriately.

## Initialization and Assignments ##

### bigint_movi ###

**Signature:** `void bigint_movi(bigint_st *dest, intmax_t src)`

**Description:**
Assign the value of a standard integer to an existing big integer.

**Arguments:**
- **dest:** Destination value.
- **src:** Source value.

### bigint_movui ###

**Signature:** `void bigint_movui(bigint_st *dest, uintmax_t src)`

**Description:**
Assign the value of an unsigned integer to an existing big integer.

**Arguments:**
- **dest:** Destination value.
- **src:** Source value.

### bigint_mov ###

**Signature:** `int bigint_mov(bigint_st *dest, bigint_st *src)`

**Description:**
Assign the value of one big integer to another.

**Arguments:**
- **dest:** Destination value.
- **src:** Source value.

**Return:** 0 if the operation succeeds and -1 if it fails.

### bigint_from_int ###

**Signature:** `bigint_st *bigint_from_int(intmax_t value)`

**Description:**
Create a big integer from a standard integer.

**Arguments:**
- **value:** Value of integer.

**Return:** A pointer to a big integer if the instantiation succeeds and `NULL`
otherwise.

### bigint_from_uint ###

**Signature:** `bigint_st *bigint_from_uint(uintmax_t value)`

**Description:**
Create a big integer from a standard unsigned integer.

**Arguments:**
- **value:** Value of integer.

**Return:** A pointer to a big integer if the instantiation succeeds and `NULL`
otherwise.

## Arithmetic and Bitwise Operations ##

### bigint_div ###

**Signature:** `bigint_st *bigint_div(bigint_st *q, bigint_st **r, bigint_st *n, bigint_st *d)`

**Description:**
Divide one big integer by another.

**Arguments:**
- **q:** Quotient; pointer to the output destination. If this is NULL, a heap
  pointer is returned that the caller is responsible for freeing with
  "bigint_free".
- **r:** Optional output pointer for the remainder.
- **n:** Numerator.
- **d:** Denominator.

**Return:** A pointer to the result of the calculation. If there were any
errors, this will be `NULL` and "errno" will be set accordingly. "EDOM" is
used to indicate division by zero.

### bigint_add ###

**Signature:** `bigint_st *bigint_add(bigint_st *dest, bigint_st *a, bigint_st *b)`

**Description:**
Add two big integers.

**Arguments:**
- **dest:** Pointer to the output destination. If this is NULL, a heap pointer
  is returned that the caller is responsible for freeing with "bigint_free".
- **a:** Addend.
- **b:** Addend.

**Return:** A pointer to the result of the calculation.

### bigint_sub ###

**Signature:** `bigint_st *bigint_sub(bigint_st *dest, bigint_st *a, bigint_st *b)`

**Description:**
Subtract one big integer from another.

**Arguments:**
- **out:** Pointer to the output destination. If this is NULL, a heap pointer is
  returned that the caller is responsible for freeing with "bigint_free".
- **a:** Minuend.
- **b:** Subtrahend.

**Return:** A pointer to the result of the calculation.

### bigint_mul ###

**Signature:** `bigint_st *bigint_mul(bigint_st *dest, bigint_st *a, bigint_st *b)`

**Description:**
Multiple two big integers.

**Arguments:**
- **dest:** Pointer to the output destination. If this is NULL, a heap pointer
  is returned that the caller is responsible for freeing with "bigint_free".
- **a:** Multiplicand.
- **b:** Multiplicand.

**Return:** A pointer to the result of the calculation if it succeeds and `NULL`
otherwise.

### bigint_shli ###

**Signature:** `bigint_st *bigint_shli(bigint_st *dest, bigint_st *x, size_t n)`

**Description:**
Perform a left shift on a big integer with the number of bits specified as
a standard integer.

**Arguments:**
- **x:** A big integer.
- **n:** Number of bits to shift left.

**Return:** The result of the calculation or `NULL` if there was an error.

### bigint_shl ###

**Signature:** `bigint_st *bigint_shl(bigint_st *dest, bigint_st *x, bigint_st* n)`

**Description:**
Perform a left shift with big integers.

**Arguments:**
- **x:** A big integer.
- **n:** Number of bits to shift left.

**Return:** The result of the calculation or `NULL` if there was an error.

### bigint_shri ###

**Signature:** `bigint_st *bigint_shri(bigint_st *dest, bigint_st *x, size_t n)`

**Description:**
Perform a right shift on a big integer with the number of bits specified as
a standard integer.

**Arguments:**
- **x:** A big integer.
- **n:** Number of bits to shift right.

**Return:** The result of the calculation or `NULL` if there was an error.

### bigint_shr ###

**Signature:** `bigint_st *bigint_shr(bigint_st *dest, bigint_st *x, bigint_st* n)`

**Description:**
Perform a right shift with big integers.

**Arguments:**
- **x:** A big integer.
- **n:** Number of bits to shift right.

**Return:** The result of the calculation or `NULL` if there was an error.

### bigint_mod ###

**Signature:** `bigint_st *bigint_mod(bigint_st *r, bigint_st *n, bigint_st *d)`

**Description:**
Compute the module of one integer by another.

**Arguments:**
- **r:** Remainder; pointer to the output destination. If this is NULL, a heap
  pointer is returned that the caller is responsible for freeing with
  "bigint_free".
- **n:** Numerator.
- **d:** Denominator.

**Return:** A pointer to the result of the calculation. If there were any
errors, this will be `NULL` and "errno" will be set accordingly. "EDOM" is
used to indicate division by zero.

### bigint_pow ###

**Signature:** `bigint_st *bigint_pow(bigint_st* dest, bigint_st *base, bigint_st* exp)`

**Description:**
Compute the value of a number raise to an exponent.

**Arguments:**
- **base:** The base big integer.
- **exp:** The exponent the base is raised to as a big integer.

**Return:** A pointer to the result of the calculation if it succeeds or `NULL`
if it fails. If the exponent is less than 0, "errno" is set to `EDOM`.

### bigint_abs ###

**Signature:** `bigint_st *bigint_abs(bigint_st *dest, bigint_st *x)`

**Description:**
Compute the absolute value of a big integer.

**Arguments:**
- **dest:** Output destination. If this is NULL, it will be allocated.
- **x:** Target value.

**Return:** A pointer to the absolute value or NULL if the function failed.

### bigint_inc ###

**Signature:** `int bigint_inc(bigint_st *x)`

**Description:**
Increment the value of a big integer by 1.

**Arguments:**
- **x:** A big integer.

**Return:** 0 if the operation succeeds and a negative value otherwise.

### bigint_dec ###

**Signature:** `int bigint_dec(bigint_st *x)`

**Description:**
Decrement the value of a big integer by 1.

**Arguments:**
- **x:** A big integer.

**Return:** 0 if the operation succeeds and a negative value otherwise.

## Type Conversions/Casting ##

### bigint_toui ###

**Signature:** `uintmax_t bigint_toui(bigint_st *x)`

**Description:**
Convert a big integer to a standard unsigned integer. If the big integer
cannot be represented as an uintmax_t value, "errno" will be set to
`ERANGE`.

**Arguments:**
- **x:** Big integer.

**Return:** A normal integer representing the value.

### bigint_toi ###

**Signature:** `intmax_t bigint_toi(bigint_st *x)`

**Description:**
Convert a big integer to a standard integer. If the big integer cannot be
represented as an intmax_t value, "errno" will be set to `ERANGE`.

**Arguments:**
- **x:** Big integer.

**Return:** A normal integer representing the value.

### bigint_tod ###

**Signature:** `double bigint_tod(bigint_st *x)`

**Description:**
Convert a big integer to a double. If the big integer cannot be represented
as a double, generally because the exponent is too large, "errno" will be
set to `EOVERFLOW` and a value representing infinity is returned. The
resulting value will be a truncated representation of the integer if it
exceeds the precision of the double's mantissa.

**Arguments:**
- **x:** Big integer.

**Return:** A double representing the value.

### bigint_strtobif ###

**Signature:** `bigint_st *bigint_strtobif(const char *str, const char **fraction)`

**Description:**
Convert a string to a big integer. This function supports hexadecimal
indicated by the prefix "0x" and "0X"; octal octal by "0o", "0O" or simply
"0"; and binary by "0b" and "0B". Otherwise, the string is parsed as a
decimal value. Decimal parsing supports scientific notation with integer
coefficients and positive exponents like "1e100" and "12E3".

**Arguments:**
- **str:** Text to convert to a big integer.
- **fraction:** Optional output pointer that will be used to indicate where the
  unused fractional value of the input begins. It ends at the first
  non-decimal-digit character following this pointer.

**Return:** A big integer if the parsing succeeds and `NULL` if it fails.
"errno" is set to `EINVAL` when the string is malformed.

### bigint_strtobi ###

**Signature:** `bigint_st *bigint_strtobi(const char *str)`

**Description:**

### bigint_snbprint ###

**Signature:** `int bigint_snbprint(char *buf, size_t buflen, bigint_st *x, unsigned char base)`

**Description:**
Write a binary, octal, decimal or hexadecimal representation of a big
integer to a string buffer.

**Arguments:**
- **buf:** The destination buffer.
- **buflen:** The size of the destination buffer.
- **x:** The big integer to write.
- **base:** The base. This must be 2, 8, 10 or 16.

**Return:** If the operation succeeds, the number of characters written
excluding the terminating NUL byte. If the operation fails, a negative value
is returned. The buffer being too short is treated as an error, so this
function will return negative value in that scenario.

### bigint_snprint ###

**Signature:** `int bigint_snprint(char *buf, size_t buflen, bigint_st *x)`

**Description:**
Write the decimal representation of a big integer to a string buffer.

**Arguments:**
- **buf:** The destination buffer.
- **buflen:** The size of the destination buffer.
- **x:** The big integer to write.

**Return:** If the operation succeeds, the number of characters written
excluding the terminating NUL byte. If the operation fails, a negative value
is returned. The buffer being too short is treated as an error, so this
function will return negative value in that scenario.

### bigint_tostrb ###

**Signature:** `char *bigint_tostrb(bigint_st *x, unsigned char base)`

**Description:**
Get a binary, octal, decimal or hexadecimal representation of a big
integer.

**Arguments:**
- **buf:** The destination buffer.
- **buflen:** The size of the destination buffer.
- **x:** The big integer to write.
- **base:** The base. This must be 2, 8, 10 or 16.

**Return:** If the operation succeeds, a heap-allocated string is retuned that
the caller must free. If the operation fails, NULL is returned.

### bigint_tostr ###

**Signature:** `char *bigint_tostr(bigint_st *x)`

**Description:**
Get the decimal representation of a big integer.

- **x:** A big integer.

**Return:** If the operation succeeds, a heap-allocated string is retuned that
the caller must free. If the operation fails, NULL is returned.

## Comparators ##

### bigint_cmp ###

**Signature:** `int bigint_cmp(const bigint_st *a, const bigint_st *b)`

**Description:**
Compare two big integers to determine if the first argument is greater than,
equal to or less than the second argument.

**Arguments:**
- **a:** A big integer.
- **b:** A big integer.

**Return:** A positive number if "a" is greater than "b", 0 if "a" equals "b"
and a negative number if "a" is less than "b".

### bigint_eqz ###

**Signature:** `bool bigint_eqz(const bigint_st *x)`

**Description:**
Return a value indicating whether the argument equals zero.

**Arguments:**
- **x:** Big integer.

**Return:** 0 if the value equals zero, and a non-zero value otherwise.

### bigint_nez ###

**Signature:** `bool bigint_nez(const bigint_st *x)`

**Description:**
Return a value indicating whether the argument is not equal zero.

**Arguments:**
- **x:** Big integer.

**Return:** 0 if the value equals zero, and a non-zero value otherwise.

### bigint_ltz ###

**Signature:** `bool bigint_ltz(const bigint_st *x)`

**Description:**
Return a value indicating whether the argument is less than zero.

**Arguments:**
- **x:** Big integer.

**Return:** A boolean value indicating whether the condition is true.

### bigint_lez ###

**Signature:** `bool bigint_lez(const bigint_st *x)`

**Description:**
Return a value indicating whether the argument is less than or equal to
zero.

**Arguments:**
- **x:** Big integer.

**Return:** A boolean value indicating whether the condition is true.

### bigint_gtz ###

**Signature:** `bool bigint_gtz(const bigint_st *x)`

**Description:**
Return a value indicating whether the argument is greater than zero.

**Arguments:**
- **x:** Big integer.

**Return:** A boolean value indicating whether the condition is true.

### bigint_gez ###

**Signature:** `bool bigint_gez(const bigint_st *x)`

**Description:**
Return a value indicating whether the argument is greater than or equal to
zero.

**Arguments:**
- **x:** Big integer.

**Return:** A boolean value indicating whether the condition is true.

### bigint_max ###

**Signature:** `bigint_st *bigint_max(bigint_st *a, bigint_st *b)`

**Description:**
Return the greatest of two big integers.

**Arguments:**
- **a:** A big integer.
- **b:** A big integer.

**Return:** The larger of the two values.

### bigint_min ###

**Signature:** `bigint_st *bigint_min(bigint_st *a, bigint_st *b)`

**Description:**
Return the least of two big integers.

**Arguments:**
- **a:** A big integer.
- **b:** A big integer.

**Return:** The smaller of the two values.

## Miscellaneous ##

### bigint_logui ###

**Signature:** `bigint_st *bigint_logui(bigint_st *dest, bigint_st *x, uintmax_t base)`

**Description:**
Compute the logarithm of a big integer with the base specified as a standard
unsigned integer.

**Arguments:**
- **dest:** Output destination. If this is NULL, it will be allocated.
- **x:** Value for which the logarithm should be computed.
- **base:** Logarithm base.

**Return:** A pointer to the absolute value or NULL if the function failed. If
the base is less than two, this function will fail with errno set to EDOM.

### bigint_gcd ###

**Signature:** `bigint_st *bigint_gcd(bigint_st *dest, bigint_st *a, bigint_st* b)`

**Description:**
Get the greatest common denominator of two big integers.

**Arguments:**
- **a:** A big integer.
- **b:** A big integer.

**Return:** A pointer to the destination if the operation succeeded or NULL if
it failed.

### bigint_is_power_of_2 ###

**Signature:** `bool bigint_is_power_of_2(bigint_st *x)`

**Description:**
Determines whether or not a big integer is a power of two.

**Arguments:**
- **x:** Big integer.

**Return:** True if the number is a power of two or false otherwise.

