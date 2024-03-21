#ifndef __YENC_CRC_H
#define __YENC_CRC_H
#include <stdlib.h> // for llabs

#ifdef __cplusplus
extern "C" {
#endif



typedef uint32_t (*crc_func)(const void*, size_t, uint32_t);
extern crc_func _do_crc32_incremental;

extern int _crc32_isa;
#define do_crc32 (*_do_crc32_incremental)
static inline int crc32_isa_level() {
	return _crc32_isa;
}


#if !defined(__GNUC__) && defined(_MSC_VER)
# include <intrin.h>
#endif
// computes `n % 0xffffffff` (well, almost), using some bit-hacks
static inline uint32_t crc32_powmod(uint64_t n) {
#ifdef __GNUC__
	unsigned res;
	unsigned carry = __builtin_uadd_overflow(n >> 32, n, &res);
	res += carry;
	return res;
#elif defined(_MSC_VER)
	unsigned res;
	unsigned char carry = _addcarry_u32(0, n >> 32, n, &res);
	_addcarry_u32(carry, res, 0, &res);
	return res;
#else
	n = (n >> 32) + (n & 0xffffffff);
	n += n >> 32;
	return n;
#endif
}
// computes `crc32_powmod(n*8)` avoiding overflow
static inline uint32_t crc32_bytepow(uint64_t n) {
#if defined(__GNUC__) || defined(_MSC_VER)
	unsigned res = crc32_powmod(n);
# ifdef _MSC_VER
	return _rotl(res, 3);
# else
	return (res << 3) | (res >> 29);
# endif
#else
	n = (n >> 32) + (n & 0xffffffff);
	n <<= 3;
	n += n >> 32;
	return n;
#endif
}

typedef uint32_t (*crc_mul_func)(uint32_t, uint32_t);
extern crc_mul_func _crc32_shift;
extern crc_mul_func _crc32_multiply;
#define crc32_shift (*_crc32_shift)
#define crc32_multiply (*_crc32_multiply)

static inline uint32_t crc32_combine(uint32_t crc1, uint32_t crc2, uint64_t len2) {
	return crc32_shift(crc1, crc32_bytepow(len2)) ^ crc2;
}
static inline uint32_t crc32_zeros(uint32_t crc1, uint64_t len) {
	return ~crc32_shift(~crc1, crc32_bytepow(len));
}
static inline uint32_t crc32_unzero(uint32_t crc1, uint64_t len) {
	return ~crc32_shift(~crc1, ~crc32_bytepow(len));
}
static inline uint32_t crc32_2pow(int64_t n) {
	uint32_t sign = (uint32_t)(n >> 63);
	return crc32_shift(0x80000000, crc32_powmod(llabs(n)) ^ sign);
}
static inline uint32_t crc32_256pow(uint64_t n) {
	return crc32_shift(0x80000000, crc32_bytepow(n));
}

void crc_init();



#ifdef __cplusplus
}
#endif
#endif
