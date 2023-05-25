#include "crc_common.h"

#include "interface.h"
crcutil_interface::CRC* crc = NULL;

#if defined(PLATFORM_X86) && !defined(__ILP32__)
static uint32_t do_crc32_incremental_generic(const void* data, size_t length, uint32_t init) {
	// use optimised ASM on x86 platforms
	crcutil_interface::UINT64 tmp = init;
	crc->Compute(data, length, &tmp);
	return (uint32_t)tmp;
}
#else
// slice-by-8 algorithm from https://create.stephan-brumme.com/crc32/
static uint32_t* HEDLEY_RESTRICT crc_slice8_table;
static uint32_t do_crc32_incremental_generic(const void* data, size_t length, uint32_t init) {
	uint32_t crc = ~init;
	uint32_t* current = (uint32_t*)data;
	const int UNROLL_CYCLES = 2; // must be power of 2
	uint32_t* end = current + ((length/sizeof(uint32_t)) & -(UNROLL_CYCLES*2));
	while(current != end) {
		for(int unroll=0; unroll<UNROLL_CYCLES; unroll++) { // two cycle loop unroll
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
# ifdef __GNUC__
			uint32_t one = *current++ ^ __builtin_bswap32(crc);
# else
			uint32_t one = *current++ ^ (
				(crc >> 24) |
				((crc >> 16) & 0xff00) |
				((crc & 0xff00) << 16) |
				((crc & 0xff) << 24)
			);
# endif
			uint32_t two = *current++;
			crc = crc_slice8_table[two & 0xFF] ^
			      crc_slice8_table[0x100L + ((two >> 8) & 0xFF)] ^
			      crc_slice8_table[0x200L + ((two >> 16) & 0xFF)] ^
			      crc_slice8_table[0x300L + ((two >> 24) & 0xFF)] ^
			      crc_slice8_table[0x400L + (one & 0xFF)] ^
			      crc_slice8_table[0x500L + ((one >> 8) & 0xFF)] ^
			      crc_slice8_table[0x600L + ((one >> 16) & 0xFF)] ^
			      crc_slice8_table[0x700L + ((one >> 24) & 0xFF)];
#else
			uint32_t one = *current++ ^ crc;
			uint32_t two = *current++;
			crc = crc_slice8_table[(two >> 24) & 0xFF] ^
			      crc_slice8_table[0x100L + ((two >> 16) & 0xFF)] ^
			      crc_slice8_table[0x200L + ((two >> 8) & 0xFF)] ^
			      crc_slice8_table[0x300L + (two & 0xFF)] ^
			      crc_slice8_table[0x400L + ((one >> 24) & 0xFF)] ^
			      crc_slice8_table[0x500L + ((one >> 16) & 0xFF)] ^
			      crc_slice8_table[0x600L + ((one >> 8) & 0xFF)] ^
			      crc_slice8_table[0x700L + (one & 0xFF)];
#endif
		}
	}
	uint8_t* current8 = (uint8_t*)current;
	for(size_t i=0; i < (length & (sizeof(uint32_t)*2 * UNROLL_CYCLES -1)); i++) {
		crc = (crc >> 8) ^ crc_slice8_table[(crc & 0xFF) ^ current8[i]];
	}
	return ~crc;
}
static void generate_crc32_slice8_table() {
	crc_slice8_table = (uint32_t*)malloc(8*256*sizeof(uint32_t));
	for(int byte=0; byte<8; byte++)
		for(int v=0; v<256; v++) {
			uint32_t crc = v;
			for(int i = byte; i >= 0; i--) {
				for(int j = 0; j < 8; j++) {
					crc = (crc >> 1) ^ (-(crc & 1) & 0xEDB88320);
				}
			}
			crc_slice8_table[byte*256 + v] = crc;
		}
}
#endif

extern "C" {
	crc_func _do_crc32_incremental = &do_crc32_incremental_generic;
	int _crc32_isa = ISA_GENERIC;
}


uint32_t do_crc32_combine(uint32_t crc1, uint32_t crc2, size_t len2) {
	crcutil_interface::UINT64 crc1_ = crc1, crc2_ = crc2;
	crc->Concatenate(crc2_, 0, len2, &crc1_);
	return (uint32_t)crc1_;
}

uint32_t do_crc32_zeros(uint32_t crc1, size_t len) {
	crcutil_interface::UINT64 crc_ = crc1;
	crc->CrcOfZeroes(len, &crc_);
	return (uint32_t)crc_;
}

void crc_clmul_set_funcs();
void crc_clmul256_set_funcs();
void crc_arm_set_funcs();

#ifdef PLATFORM_X86
int cpu_supports_crc_isa();
#endif

#if defined(PLATFORM_ARM) && defined(_WIN32)
# define WIN32_LEAN_AND_MEAN
# include <Windows.h>
#endif
#ifdef PLATFORM_ARM
# ifdef __ANDROID__
#  include <cpu-features.h>
# elif defined(__APPLE__)
#  include <sys/types.h>
#  include <sys/sysctl.h>
# elif defined(__has_include)
#  if __has_include(<sys/auxv.h>)
#   include <sys/auxv.h>
#   ifdef __FreeBSD__
static unsigned long getauxval(unsigned long cap) {
	unsigned long ret;
	elf_aux_info(cap, &ret, sizeof(ret));
	return ret;
}
#   endif
#   if __has_include(<asm/hwcap.h>)
#    include <asm/hwcap.h>
#   endif
#  endif
# endif
#endif
void crc_init() {
	crc = crcutil_interface::CRC::Create(
		0xEDB88320, 0, 32, true, 0, 0, 0, 0, NULL);
	// instance never deleted... oh well...
	
#if !defined(PLATFORM_X86) || defined(__ILP32__)
	generate_crc32_slice8_table();
#endif
	
#ifdef PLATFORM_X86
	int support = cpu_supports_crc_isa();
	if(support == 2)
		crc_clmul256_set_funcs();
	else if(support == 1)
		crc_clmul_set_funcs();
#endif
#ifdef PLATFORM_ARM
# ifdef __APPLE__
	int supported = 0;
	size_t len = sizeof(supported);
	if(sysctlbyname("hw.optional.armv8_crc32", &supported, &len, NULL, 0))
		supported = 0;
# endif
	if(
# if defined(AT_HWCAP2) && defined(HWCAP2_CRC32)
		getauxval(AT_HWCAP2) & HWCAP2_CRC32
# elif defined(AT_HWCAP) && defined(HWCAP_CRC32)
		getauxval(AT_HWCAP) & HWCAP_CRC32
# elif defined(ANDROID_CPU_FAMILY_ARM) && defined(__aarch64__)
		android_getCpuFeatures() & ANDROID_CPU_ARM64_FEATURE_CRC32
# elif defined(ANDROID_CPU_FAMILY_ARM) /* aarch32 */
		android_getCpuFeatures() & ANDROID_CPU_ARM_FEATURE_CRC32
# elif defined(_WIN32)
		IsProcessorFeaturePresent(PF_ARM_V8_CRC32_INSTRUCTIONS_AVAILABLE)
# elif defined(__APPLE__)
		supported
# elif defined(__ARM_FEATURE_CRC32)
		true /* assume available if compiled as such */
# else
		false
# endif
	) {
		crc_arm_set_funcs();
	}
#endif
}
