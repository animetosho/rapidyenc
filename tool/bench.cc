#include <iostream>
#include <vector>
#include <chrono>
#include <cstring> // for std::strcmp

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
	if(k == RYKERN_RVV) return "RVV";
	if(k == RYKERN_ARMPMULL) return "ARM-CRC + PMULL";
	if(k == RYKERN_ZBC) return "Zbkc";
	return "unknown";
}
#define ARTICLE_SIZE 768000ULL
#define SINGLE_OP_NUM 100
#define REPETITIONS 1000

// Helper to parse command-line arguments for benchmark configuration
struct BenchConfig {
    size_t article_size = ARTICLE_SIZE;
    int repetitions = REPETITIONS;
    bool run_encode = true;
    bool run_decode = true;
    bool run_crc = true;
};

BenchConfig parse_args(int argc, char** argv) {
    BenchConfig cfg;
    for(int i=1; i<argc; ++i) {
        if(std::strcmp(argv[i], "--size") == 0 && i+1 < argc) {
            cfg.article_size = std::stoull(argv[++i]);
        } else if(std::strcmp(argv[i], "--reps") == 0 && i+1 < argc) {
            cfg.repetitions = std::stoi(argv[++i]);
        } else if(std::strcmp(argv[i], "--bench") == 0 && i+1 < argc) {
            std::string b(argv[++i]);
            cfg.run_encode = b.find("encode") != std::string::npos;
            cfg.run_decode = b.find("decode") != std::string::npos;
            cfg.run_crc = b.find("crc") != std::string::npos;
        }
    }
    return cfg;
}

int main(int argc, char** argv) {
    BenchConfig cfg = parse_args(argc, argv);
    // Allocate input data buffer and output buffer for encoded article
    std::vector<unsigned char> data(cfg.article_size);
    std::vector<unsigned char> article(rapidyenc_encode_max_length(cfg.article_size, 128));
    size_t article_length = 0;

    // Fill input buffer with random data for benchmarking
    for(auto& c : data)
        c = rand() & 0xff;

    // If decode is requested but encode is not, ensure article buffer is filled with valid yEnc data
#ifndef RAPIDYENC_DISABLE_ENCODE
    if(cfg.run_decode && !cfg.run_encode) {
        rapidyenc_encode_init();
        article_length = rapidyenc_encode(data.data(), article.data(), cfg.article_size);
    }
#else
    if(cfg.run_decode && !cfg.run_encode) {
        unsigned char* pOut = article.data();
        int col = 0;
        for(unsigned i=0; i<cfg.article_size; i++) {
            unsigned char c = data[i];
            if(c == 0 || c == '\r' || c == '\n' || c == '=' ||
              (col == 0 && c == '.') ||
              ((col % 128 == 0) && (c == '\t' || c == ' '))) {
                *pOut++ = '=';
                *pOut++ = c + 64;
                col++;
            } else {
                *pOut++ = c;
            }
            if(++col >= 128) {
                *pOut++ = '\r';
                *pOut++ = '\n';
                col = 0;
            }
        }
        article_length = pOut - article.data();
    }
#endif

    // --- Encode benchmark ---
#ifndef RAPIDYENC_DISABLE_ENCODE
    if(cfg.run_encode) {
        rapidyenc_encode_init();
        auto kernel = rapidyenc_encode_kernel();
        auto start = std::chrono::high_resolution_clock::now();
        for(int i=0; i<cfg.repetitions; i++) {
            // Encode the input data into yEnc format
            article_length = rapidyenc_encode(data.data(), article.data(), cfg.article_size);
        }
        auto stop = std::chrono::high_resolution_clock::now();
        float us = std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count();
        double ms = us / 1000.0;
        double speed = cfg.article_size * cfg.repetitions;
        speed = speed / us / 1.048576;
        std::cerr << "Encode (" << kernel_to_str(kernel) << ", size=" << cfg.article_size << ", reps=" << cfg.repetitions << "): "
                  << speed << " MB/s, time: " << ms << " ms" << std::endl;
    }
#else
    if(cfg.run_encode) {
        // Fallback: pseudo yEnc encode to get a valid-ish article if encoder is disabled
        unsigned char* pOut = article.data();
        int col = 0;
        for(unsigned i=0; i<cfg.article_size; i++) {
            unsigned char c = data[i];
            if(c == 0 || c == '\r' || c == '\n' || c == '=' ||
              (col == 0 && c == '.') ||
              ((col % 128 == 0) && (c == '\t' || c == ' '))) {
                *pOut++ = '=';
                *pOut++ = c + 64;
                col++;
            } else {
                *pOut++ = c;
            }
            if(++col >= 128) {
                *pOut++ = '\r';
                *pOut++ = '\n';
                col = 0;
            }
        }
        article_length = pOut - article.data();
    }
#endif

    // --- Decode benchmark ---
#ifndef RAPIDYENC_DISABLE_DECODE
    if(cfg.run_decode) {
        rapidyenc_decode_init();
        auto kernel = rapidyenc_decode_kernel();
        auto start = std::chrono::high_resolution_clock::now();
        for(int i=0; i<cfg.repetitions; i++) {
            // Decode the yEnc article back to original data
            rapidyenc_decode(article.data(), data.data(), article_length);
        }
        auto stop = std::chrono::high_resolution_clock::now();
        float us = std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count();
        double ms = us / 1000.0;
        double speed = article_length * cfg.repetitions;
        speed = speed / us / 1.048576;
        std::cerr << "Decode (" << kernel_to_str(kernel) << ", size=" << article_length << ", reps=" << cfg.repetitions << "): "
                  << speed << " MB/s, time: " << ms << " ms" << std::endl;
    }
#endif

    // --- CRC32 benchmark ---
#ifndef RAPIDYENC_DISABLE_CRC
    if(cfg.run_crc) {
        rapidyenc_crc_init();
        auto kernel = rapidyenc_crc_kernel();
        auto start = std::chrono::high_resolution_clock::now();
        for(int i=0; i<cfg.repetitions; i++) {
            // Compute CRC32 on the input data
            rapidyenc_crc(data.data(), cfg.article_size, 0);
        }
        auto stop = std::chrono::high_resolution_clock::now();
        float us = std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count();
        double ms = us / 1000.0;
        double speed = cfg.article_size * cfg.repetitions;
        speed = speed / us / 1.048576;
        std::cerr << "CRC32 (" << kernel_to_str(kernel) << ", size=" << cfg.article_size << ", reps=" << cfg.repetitions << "): "
                  << speed << " MB/s, time: " << ms << " ms" << std::endl;

        // --- CRC32 256^n benchmark ---
        std::vector<uint64_t> rnd_n(SINGLE_OP_NUM);
        std::vector<uint32_t> rnd_out(SINGLE_OP_NUM);
        for(auto& c : rnd_n)
            c = ((rand() & 0xffff) << 20) | (rand() & 0xfffff);  // 36-bit random numbers

        start = std::chrono::high_resolution_clock::now();
        for(int i=0; i<cfg.repetitions; i++) {
            for(unsigned j=0; j<SINGLE_OP_NUM; j++)
                // Compute CRC32 256^n for random n
                rnd_out[j] = rapidyenc_crc_256pow(rnd_n[j]);
        }
        stop = std::chrono::high_resolution_clock::now();
        us = std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count();
        ms = us / 1000.0;
        speed = SINGLE_OP_NUM * cfg.repetitions;
        speed = speed / us;
        std::cerr << "CRC32 256^n: " << speed << " Mop/s, time: " << ms << " ms" << std::endl;
    }
#endif

    return 0;
}
