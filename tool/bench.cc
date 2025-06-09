#include <iostream>
#include <vector>
#include <chrono>
#include <cstring> // for std::strcmp
#include <thread>
#include <iomanip>
#include <string>

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
    int threads = 1;
};

BenchConfig parse_args(int argc, char** argv) {
    BenchConfig cfg;
    int i = 1;
    while (i < argc) {
        if(std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            std::cout << "Usage: rapidyenc_bench [--size <bytes>] [--reps <num>] [--bench <encode,decode,crc>] [--threads <n>] [--help]" << std::endl;
            std::cout << "  --size <bytes>   Set the article size in bytes (default: 768000)" << std::endl;
            std::cout << "  --reps <num>     Set the number of repetitions (default: 1000)" << std::endl;
            std::cout << "  --bench <list>   Comma-separated list of benchmarks to run (encode,decode,crc)" << std::endl;
            std::cout << "  --threads <n>    Number of threads to use (default: 1)" << std::endl;
            std::cout << "  --help, -h       Show this help message and exit" << std::endl;
            std::exit(0);
        } else if(std::strcmp(argv[i], "--size") == 0 && i+1 < argc) {
            cfg.article_size = std::stoull(argv[++i]);
            ++i;
        } else if(std::strcmp(argv[i], "--reps") == 0 && i+1 < argc) {
            cfg.repetitions = std::stoi(argv[++i]);
            ++i;
        } else if(std::strcmp(argv[i], "--bench") == 0 && i+1 < argc) {
            std::string b(argv[++i]);
            cfg.run_encode = b.find("encode") != std::string::npos;
            cfg.run_decode = b.find("decode") != std::string::npos;
            cfg.run_crc = b.find("crc") != std::string::npos;
            ++i;
        } else if(std::strcmp(argv[i], "--threads") == 0 && i+1 < argc) {
            cfg.threads = std::stoi(argv[++i]);
            if(cfg.threads < 1) cfg.threads = 1;
            ++i;
        } else {
            ++i;
        }
    }
    return cfg;
}

int main(int argc, char** argv) {
    std::cout << "STARTED" << std::endl;
    std::cout.flush();
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

    // Only run threaded benchmarks if threads > 1, else run single-threaded
    if(cfg.threads > 1) {
        std::cout << std::left
                  << std::setw(12) << "Benchmark"
                  << std::setw(10) << "Kernel"
                  << std::setw(10) << "Size"
                  << std::setw(10) << "Reps"
                  << std::setw(10) << "Threads"
                  << std::setw(18) << "Speed(MB/s|Mop/s)"
                  << std::setw(10) << "Time(ms)" << std::endl;
        // --- Encode benchmark ---
#ifndef RAPIDYENC_DISABLE_ENCODE
        if(cfg.run_encode) {
            rapidyenc_encode_init();
            auto kernel = rapidyenc_encode_kernel();
            std::vector<std::thread> threads;
            std::vector<size_t> thread_article_length(cfg.threads, 0);
            auto start = std::chrono::high_resolution_clock::now();
            for(int t=0; t<cfg.threads; ++t) {
                threads.emplace_back([&, t]() {
                    std::vector<unsigned char> data_t = data;
                    std::vector<unsigned char> article_t = article;
                    int reps = cfg.repetitions / cfg.threads + (t < (cfg.repetitions % cfg.threads) ? 1 : 0);
                    size_t local_len = 0;
                    for(int i=0; i<reps; i++) {
                        local_len = rapidyenc_encode(data_t.data(), article_t.data(), cfg.article_size);
                    }
                    thread_article_length[t] = local_len;
                });
            }
            for(auto& th : threads) th.join();
            auto stop = std::chrono::high_resolution_clock::now();
            float us = std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count();
            double ms = us / 1000.0;
            double speed = cfg.article_size * cfg.repetitions;
            speed = speed / us / 1.048576;
            std::cout << std::left
                      << std::setw(12) << "Encode"
                      << std::setw(10) << kernel_to_str(kernel)
                      << std::setw(10) << cfg.article_size
                      << std::setw(10) << cfg.repetitions
                      << std::setw(10) << cfg.threads
                      << std::setw(18) << speed
                      << std::setw(10) << ms << std::endl;
            article_length = thread_article_length[0];
            std::cout << std::flush;
        }
#endif
        // --- Decode benchmark ---
#ifndef RAPIDYENC_DISABLE_DECODE
        if(cfg.run_decode) {
            rapidyenc_decode_init();
            auto kernel = rapidyenc_decode_kernel();
            std::vector<std::thread> threads;
            auto start = std::chrono::high_resolution_clock::now();
            for(int t=0; t<cfg.threads; ++t) {
                threads.emplace_back([&, t]() {
                    std::vector<unsigned char> data_t(article_length);
                    std::vector<unsigned char> article_t = article;
                    int reps = cfg.repetitions / cfg.threads + (t < (cfg.repetitions % cfg.threads) ? 1 : 0);
                    for(int i=0; i<reps; i++) {
                        rapidyenc_decode(article_t.data(), data_t.data(), article_length);
                    }
                });
            }
            for(auto& th : threads) th.join();
            auto stop = std::chrono::high_resolution_clock::now();
            float us = std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count();
            double ms = us / 1000.0;
            double speed = article_length * cfg.repetitions;
            speed = speed / us / 1.048576;
            std::cout << std::left
                      << std::setw(12) << "Decode"
                      << std::setw(10) << kernel_to_str(kernel)
                      << std::setw(10) << article_length
                      << std::setw(10) << cfg.repetitions
                      << std::setw(10) << cfg.threads
                      << std::setw(18) << speed
                      << std::setw(10) << ms << std::endl;
            std::cout << std::flush;
        }
#endif
        // --- CRC32 benchmark ---
#ifndef RAPIDYENC_DISABLE_CRC
        if(cfg.run_crc) {
            rapidyenc_crc_init();
            auto kernel = rapidyenc_crc_kernel();
            std::vector<std::thread> threads;
            auto start = std::chrono::high_resolution_clock::now();
            for(int t=0; t<cfg.threads; ++t) {
                threads.emplace_back([&, t]() {
                    std::vector<unsigned char> data_t = data;
                    int reps = cfg.repetitions / cfg.threads + (t < (cfg.repetitions % cfg.threads) ? 1 : 0);
                    for(int i=0; i<reps; i++) {
                        rapidyenc_crc(data_t.data(), cfg.article_size, 0);
                    }
                });
            }
            for(auto& th : threads) th.join();
            auto stop = std::chrono::high_resolution_clock::now();
            float us = std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count();
            double ms = us / 1000.0;
            double speed = cfg.article_size * cfg.repetitions;
            speed = speed / us / 1.048576;
            std::cout << std::left
                      << std::setw(12) << "CRC32"
                      << std::setw(10) << kernel_to_str(kernel)
                      << std::setw(10) << cfg.article_size
                      << std::setw(10) << cfg.repetitions
                      << std::setw(10) << cfg.threads
                      << std::setw(18) << speed
                      << std::setw(10) << ms << std::endl;
            std::cout << std::flush;
            // --- CRC32 256^n benchmark ---
            std::vector<uint64_t> rnd_n(SINGLE_OP_NUM);
            std::vector<uint32_t> rnd_out(SINGLE_OP_NUM);
            for(auto& c : rnd_n)
                c = ((rand() & 0xffff) << 20) | (rand() & 0xfffff);
            start = std::chrono::high_resolution_clock::now();
            std::vector<std::thread> threads2;
            for(int t=0; t<cfg.threads; ++t) {
                threads2.emplace_back([&, t]() {
                    int reps = cfg.repetitions / cfg.threads + (t < (cfg.repetitions % cfg.threads) ? 1 : 0);
                    for(int i=0; i<reps; i++) {
                        for(unsigned j=0; j<SINGLE_OP_NUM; j++)
                            rnd_out[j] = rapidyenc_crc_256pow(rnd_n[j]);
                    }
                });
            }
            for(auto& th : threads2) th.join();
            auto stop2 = std::chrono::high_resolution_clock::now();
            float us2 = std::chrono::duration_cast<std::chrono::microseconds>(stop2 - start).count();
            double ms2 = us2 / 1000.0;
            double speed2 = SINGLE_OP_NUM * cfg.repetitions;
            speed2 = speed2 / us2;
            std::cout << std::left
                      << std::setw(12) << "CRC32_256^n"
                      << std::setw(10) << "-"
                      << std::setw(10) << "-"
                      << std::setw(10) << cfg.repetitions
                      << std::setw(10) << cfg.threads
                      << std::setw(18) << speed2
                      << std::setw(10) << ms2 << std::endl;
            std::cout << std::flush;
        }
#endif
    } else {
        // Tabular header for single-threaded output
        std::cout << std::left
                  << std::setw(12) << "Benchmark"
                  << std::setw(10) << "Kernel"
                  << std::setw(10) << "Size"
                  << std::setw(10) << "Reps"
                  << std::setw(10) << "Threads"
                  << std::setw(18) << "Speed(MB/s|Mop/s)"
                  << std::setw(10) << "Time(ms)" << std::endl;
        std::cout << std::flush;

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
            std::cout << std::left
                      << std::setw(12) << "Encode"
                      << std::setw(10) << kernel_to_str(kernel)
                      << std::setw(10) << cfg.article_size
                      << std::setw(10) << cfg.repetitions
                      << std::setw(10) << 1
                      << std::setw(18) << speed
                      << std::setw(10) << ms << std::endl;
            std::cout << std::flush;
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
            std::cout << std::left
                      << std::setw(12) << "Decode"
                      << std::setw(10) << kernel_to_str(kernel)
                      << std::setw(10) << article_length
                      << std::setw(10) << cfg.repetitions
                      << std::setw(10) << 1
                      << std::setw(18) << speed
                      << std::setw(10) << ms << std::endl;
            std::cout << std::flush;
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
            std::cout << std::left
                      << std::setw(12) << "CRC32"
                      << std::setw(10) << kernel_to_str(kernel)
                      << std::setw(10) << cfg.article_size
                      << std::setw(10) << cfg.repetitions
                      << std::setw(10) << 1
                      << std::setw(18) << speed
                      << std::setw(10) << ms << std::endl;
            std::cout << std::flush;

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
            std::cout << std::left
                      << std::setw(12) << "CRC32_256^n"
                      << std::setw(10) << "-"
                      << std::setw(10) << "-"
                      << std::setw(10) << cfg.repetitions
                      << std::setw(10) << 1
                      << std::setw(18) << speed
                      << std::setw(10) << ms << std::endl;
            std::cout << std::flush;
        }
#endif
    }

    std::cout << "DONE" << std::endl;
    std::cout.flush();
    //std::cerr << "About to return from main" << std::endl;
    return 0;
}
