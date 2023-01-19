#include <iostream>
#include <vector>
#include <chrono>

#include "../rapidyenc.h"

static const char* kernel_to_str(int k) {
	if(k == RYKERN_GENERIC) return "generic";
	if(k == RYKERN_SSE2) return "SSE2";
	if(k == RYKERN_SSSE3) return "SSSE3";
	if(k == RYKERN_AVX) return "AVX";
	if(k == RYKERN_AVX2) return "AVX2";
	if(k == RYKERN_VBMI2) return "VBMI2";
	if(k == RYKERN_NEON) return "NEON";
	if(k == RYKERN_PCLMUL) return "PCLMUL";
	if(k == RYKERN_VPCLMUL) return "VPCLMUL";
	if(k == RYKERN_ARMCRC) return "ARM-CRC";
	return "unknown";
}
#define ARTICLE_SIZE 768000ULL
#define REPETITIONS 1000

int main(int, char**) {
	std::vector<unsigned char> data(ARTICLE_SIZE);
	std::vector<unsigned char> article(rapidyenc_encode_max_length(ARTICLE_SIZE, 128));
	size_t article_length, decoded_length;
	
	// fill with random data
	for(auto& c : data)
		c = rand() & 0xff;
	
	
	// encode benchmark
	rapidyenc_encode_init();
	auto kernel = rapidyenc_encode_kernel();
	auto start = std::chrono::high_resolution_clock::now();
	for(int i=0; i<REPETITIONS; i++) {
		article_length = rapidyenc_encode(data.data(), article.data(), ARTICLE_SIZE);
	}
	auto stop = std::chrono::high_resolution_clock::now();
	float us = std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count();
	double speed = ARTICLE_SIZE * REPETITIONS;
	speed = speed / us / 1.048576;
	std::cerr << "Encode (" << kernel_to_str(kernel) << "): " << speed << " MB/s" << std::endl;
	
	// decode
	rapidyenc_decode_init();
	kernel = rapidyenc_decode_kernel();
	start = std::chrono::high_resolution_clock::now();
	for(int i=0; i<REPETITIONS; i++) {
		decoded_length = rapidyenc_decode(article.data(), data.data(), article_length);
	}
	stop = std::chrono::high_resolution_clock::now();
	us = std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count();
	speed = article_length * REPETITIONS;
	speed = speed / us / 1.048576;
	std::cerr << "Decode (" << kernel_to_str(kernel) << "): " << speed << " MB/s" << std::endl;
	
	// CRC
	rapidyenc_crc_init();
	kernel = rapidyenc_crc_kernel();
	start = std::chrono::high_resolution_clock::now();
	for(int i=0; i<REPETITIONS; i++) {
		rapidyenc_crc(data.data(), decoded_length, 0);
	}
	stop = std::chrono::high_resolution_clock::now();
	us = std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count();
	speed = decoded_length * REPETITIONS;
	speed = speed / us / 1.048576;
	std::cerr << "CRC32 (" << kernel_to_str(kernel) << "): " << speed << " MB/s" << std::endl;
	
	return 0;
}
