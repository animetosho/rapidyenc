/*
 * rapidyenc CLI tool
 *
 * This file provides a simple command-line interface for encoding or decoding data
 * using the rapidyenc library. It reads from stdin and writes to stdout, supporting both
 * encoding and decoding modes, and optionally computes CRC32 checksums.
 *
 * Usage: cli {e|d} [infile [outfile]]
 *   e: encode stdin to stdout
 *   d: decode stdin to stdout
 *
 * The function handles buffer allocation, error checking, and calls the appropriate
 * rapidyenc API functions for the selected mode. It also prints CRC32 if enabled.
 *
 * Steps:
 *   1. Parse command-line arguments and print usage if invalid.
 *   2. Open input/output streams (stdin/stdout).
 *   3. Allocate input buffer (and output buffer for encoding).
 *   4. For encoding: loop reading, encoding, and writing output, updating CRC.
 *   5. For decoding: loop reading, decoding, and writing output, updating CRC.
 *   6. Print CRC32 if enabled.
 *   7. Clean up and exit.
 *
 * Error handling is performed at each step, and the function returns 1 on error, 0 on success.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "../rapidyenc.h"

static int print_usage(const char *app) {
    fprintf(stderr, "Sample rapidyenc application\n");
    fprintf(stderr, "Usage: %s (--encode|--decode) [--infile <file>] [--outfile <file>] [--crc-stdout] [-h|--help] [--version]\n", app);
    fprintf(stderr, "  --encode         Encode input to output (default: stdin/stdout)\n");
    fprintf(stderr, "  --decode         Decode input to output (default: stdin/stdout)\n");
    fprintf(stderr, "  --infile <file>  Input file (default: stdin)\n");
    fprintf(stderr, "  --outfile <file> Output file (default: stdout)\n");
    fprintf(stderr, "  --crc-stdout     Print CRC32 to stdout instead of stderr\n");
    fprintf(stderr, "  -h, --help       Show this help message\n");
    fprintf(stderr, "  --version        Show version information\n");
    return EXIT_FAILURE;
}

#define RAPIDYENC_CLI_VERSION "1.0.0"

#define BUFFER_SIZE 65536
#define LINE_SIZE 128

// Helper to print rapidyenc version as major.minor.patch
static void print_rapidyenc_version_string(int version) {
    int major = (version >> 16) & 0xFF;
    int minor = (version >> 8) & 0xFF;
    int patch = version & 0xFF;
    printf("linked rapidyenc library version: %d.%d.%d\n", major, minor, patch);
}

int main(int argc, char **argv) {
    // Argument parsing
    int encode = 0, decode = 0, crc_to_stdout = 0;
    const char *infile_name = NULL, *outfile_name = NULL;
    size_t buffer_size = 65536;
    for(int i = 1; i < argc; ++i) {
        if(strcmp(argv[i], "--version") == 0) {
            printf("rapidyenc CLI version %s\n", RAPIDYENC_CLI_VERSION);
            print_rapidyenc_version_string(rapidyenc_version());
            return 0;
        }
        if(strcmp(argv[i], "--encode") == 0) encode = 1;
        else if(strcmp(argv[i], "--decode") == 0) decode = 1;
        else if(strcmp(argv[i], "--infile") == 0 && i+1 < argc) infile_name = argv[++i];
        else if(strcmp(argv[i], "--outfile") == 0 && i+1 < argc) outfile_name = argv[++i];
        else if(strcmp(argv[i], "--crc-stdout") == 0) crc_to_stdout = 1;
        else if(strcmp(argv[i], "--buffer-size") == 0 && i+1 < argc) {
            buffer_size = (size_t)strtoul(argv[++i], NULL, 10);
            if(buffer_size == 0) buffer_size = 65536;
        }
        else if(strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) return print_usage(argv[0]);
        else if(argv[i][0] != '-') {
            // legacy positional: e|d infile outfile
            if(strcmp(argv[i], "e") == 0) encode = 1;
            else if(strcmp(argv[i], "d") == 0) decode = 1;
            else if(!infile_name) infile_name = argv[i];
            else if(!outfile_name) outfile_name = argv[i];
        }
    }
    if((encode && decode) || (!encode && !decode)) {
        return print_usage(argv[0]);
    }

    // Check if encoder/decoder is disabled at compile time
#ifdef RAPIDYENC_DISABLE_ENCODE
    if(encode) {
        fprintf(stderr, "encoder has been disabled in this build\n");
        return EXIT_FAILURE;
    }
#endif
#ifdef RAPIDYENC_DISABLE_DECODE
    if(decode) {
        fprintf(stderr, "decoder has been disabled in this build\n");
        return EXIT_FAILURE;
    }
#endif
    FILE* infile = stdin;
    FILE* outfile = stdout;
    if(infile_name) {
        infile = fopen(infile_name, "rb");
        if(!infile) {
            fprintf(stderr, "error opening input file '%s': %s\n", infile_name, strerror(errno));
            return EXIT_FAILURE;
        }
    }
    if(outfile_name) {
        outfile = fopen(outfile_name, "wb");
        if(!outfile) {
            fprintf(stderr, "error opening output file '%s': %s\n", outfile_name, strerror(errno));
            if(infile && infile != stdin) fclose(infile);
            return EXIT_FAILURE;
        }
#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
        // Restrict permissions to 0644 (rw-r--r--)
        fchmod(fileno(outfile), 0644);
#endif
    }

    // Allocate input buffer
    void* data = malloc(buffer_size);
    if(!data) {
        fprintf(stderr, "error allocating input buffer of size %zu bytes\n", buffer_size);
        fclose(infile);
        fclose(outfile);
        return EXIT_FAILURE;
    }

#ifndef RAPIDYENC_DISABLE_CRC
    // Initialize CRC32 computation if enabled
    rapidyenc_crc_init();
    uint32_t crc = 0;
#endif
    int has_error = 0;

#ifndef RAPIDYENC_DISABLE_ENCODE
    if(encode) {
        // --- Encoding mode ---
        // Allocate output buffer large enough for encoded data
        size_t output_size = rapidyenc_encode_max_length(buffer_size, LINE_SIZE);
        void* output = malloc(output_size);
        if(!output) {
            fprintf(stderr, "error allocating output buffer of size %zu bytes\n", output_size);
            fclose(infile);
            fclose(outfile);
            free(data);
            return EXIT_FAILURE;
        }
        rapidyenc_encode_init();

        int column = 0;
        while(1) {
            // Read a chunk from input
            size_t read = fread(data, 1, buffer_size, infile);
            int eof = feof(infile);
            if(read < buffer_size && !eof) {
                if(ferror(infile)) {
                    fprintf(stderr, "error reading input\n");
                } else {
                    fprintf(stderr, "error: got zero bytes when reading input\n");
                }
                has_error = 1;
                break;
            }
            // Encode the chunk
            size_t out_len = rapidyenc_encode_ex(LINE_SIZE, &column, data, output, read, eof);
#ifndef RAPIDYENC_DISABLE_CRC
            // Update CRC32 with original data
            crc = rapidyenc_crc(data, read, crc);
#endif
            // Write encoded data to output
            if(fwrite(output, 1, out_len, outfile) != out_len) {
                fprintf(stderr, "error writing output\n");
                has_error = 1;
                break;
            }
            if(eof) break;
        }
        free(output);
    }
#endif
#ifndef RAPIDYENC_DISABLE_DECODE
    if(decode) {
        // --- Decoding mode ---
        rapidyenc_decode_init();

        RapidYencDecoderState state = RYDEC_STATE_CRLF;
        while(1) {
            // Read a chunk from input
            size_t read = fread(data, 1, buffer_size, infile);
            RapidYencDecoderEnd ended;
            void* in_ptr = data;
            void* out_ptr = data;
            int eof = feof(infile);
            if(read < buffer_size && !eof) {
                if(ferror(infile)) {
                    fprintf(stderr, "error reading input\n");
                } else {
                    fprintf(stderr, "error: got zero bytes when reading input\n");
                }
                has_error = 1;
                break;
            }
            // Decode the chunk in-place (input and output buffer are the same)
            ended = rapidyenc_decode_incremental((const void**)&in_ptr, &out_ptr, read, &state);
            size_t out_len = (uintptr_t)out_ptr - (uintptr_t)data;
#ifndef RAPIDYENC_DISABLE_CRC
            // Update CRC32 with decoded data
            crc = rapidyenc_crc(data, out_len, crc);
#endif
            // Write decoded data to output
            if(fwrite(data, 1, out_len, outfile) != out_len) {
                fprintf(stderr, "error writing output\n");
                has_error = 1;
                break;
            }

            // Check for end-of-article or control line
            if(ended != RYDEC_END_NONE || eof) {
                if(ended == RYDEC_END_CONTROL)
                    fprintf(stderr, "yEnc control line found\n");
                else if(ended == RYDEC_END_ARTICLE)
                    fprintf(stderr, "End-of-article marker found\n");
                else
                    fprintf(stderr, "End of input reached\n");
                break;
            }
        }
    }
#endif

    // Clean up resources
    fflush(outfile);
    if(infile && infile != stdin) fclose(infile);
    if(outfile && outfile != stdout) fclose(outfile);
    free(data);

    // Print CRC32 if enabled and no error occurred
    if(!has_error) {
#ifndef RAPIDYENC_DISABLE_CRC
        FILE* crc_out = crc_to_stdout ? stdout : stderr;
        fprintf(crc_out, "Computed CRC32: %08x\n", crc);
#endif
    }

    return has_error ? EXIT_FAILURE : EXIT_SUCCESS;
}
