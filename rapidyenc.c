#ifdef BUILD_SHARED
# ifdef _MSC_VER
#  define RAPIDYENC_API __declspec(dllexport)
# else
#  define RAPIDYENC_API __attribute__((visibility("default")))
# endif
#endif

#include "rapidyenc.h"

int rapidyenc_version(void) {
	return RAPIDYENC_VERSION;
}

/*** ENCODER ***/
#include "src/encoder.h"
void rapidyenc_encode_init(void) {
	static int done = 0;
	if(done) return;
	done = 1;
	encoder_init();
}

size_t rapidyenc_encode_max_length(size_t length, int line_size) {
	size_t ret = length * 2    /* all characters escaped */
		+ 2 /* allocation for offset and that a newline may occur early */
#if !defined(YENC_DISABLE_AVX256)
		+ 64 /* allocation for YMM overflowing */
#else
		+ 32 /* allocation for XMM overflowing */
#endif
	;
	/* add newlines, considering the possibility of all chars escaped */
	if(line_size == 128) // optimize common case
		return ret + 2 * (length >> 6);
	return ret + 2 * ((length*2) / line_size);
}

size_t rapidyenc_encode(const void* __restrict src, void* __restrict dest, size_t src_length) {
	return rapidyenc_encode_ex(128, NULL, src, dest, src_length, 1);
}

size_t rapidyenc_encode_ex(int line_size, int* column, const void* __restrict src, void* __restrict dest, size_t src_length, int is_end) {
	int unusedColumn = 0;
	if(!column) column = &unusedColumn;
	return _do_encode(line_size, column, src, dest, src_length, is_end);
}

int rapidyenc_encode_kernel() {
	return encode_isa_level();
}

/*** DECODER ***/
#include "src/decoder.h"
void rapidyenc_decode_init(void) {
	static int done = 0;
	if(done) return;
	done = 1;
	decoder_init();
}

size_t rapidyenc_decode(const void* src, void* dest, size_t src_length) {
	return rapidyenc_decode_ex(1, src, dest, src_length, NULL);
}

size_t rapidyenc_decode_ex(int is_raw, const void* src, void* dest, size_t src_length, RapidYencDecoderState* state) {
	RapidYencDecoderState unusedState = RYDEC_STATE_CRLF;
	if(!state) state = &unusedState;
	return do_decode(is_raw, (const unsigned char*)src, (unsigned char*)dest, src_length, (YencDecoderState*)state);
}

RapidYencDecoderEnd rapidyenc_decode_incremental(const void** src, void** dest, size_t src_length, RapidYencDecoderState* state) {
	RapidYencDecoderState unusedState = RYDEC_STATE_CRLF;
	if(!state) state = &unusedState;
	return (RapidYencDecoderEnd)do_decode_end((const unsigned char**)src, (unsigned char**)dest, src_length, (YencDecoderState*)state);
}

int rapidyenc_decode_kernel() {
	return decode_isa_level();
}

/*** CRC32 ***/
#include "src/crc.h"
void rapidyenc_crc_init(void) {
	static int done = 0;
	if(done) return;
	done = 1;
	crc_init();
}

uint32_t rapidyenc_crc(const void* src, size_t src_length, uint32_t init_crc) {
	return do_crc32(src, src_length, init_crc);
}
uint32_t rapidyenc_crc_combine(uint32_t crc1, const uint32_t crc2, size_t length2) {
	return do_crc32_combine(crc1, crc2, length2);
}
uint32_t rapidyenc_crc_zeros(uint32_t init_crc, size_t length) {
	return do_crc32_zeros(init_crc, length);
}

int rapidyenc_crc_kernel() {
	return crc32_isa_level();
}

