/*
 * rapidyenc CLI tool
 *
 * This file provides a simple command-line interface for encoding or decoding data
 * using the rapidyenc library. It reads from stdin and writes to stdout, supporting both
 * encoding and decoding modes, and optionally computes CRC32 checksums.
 *
 * Usage: cli {e|d}
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
	fprintf(stderr, "Usage: %s {e|d}\n", app);
	fprintf(stderr, "  (e)ncodes or (d)ecodes stdin to stdout\n");
	return EXIT_FAILURE;
}

#define BUFFER_SIZE 65536
#define LINE_SIZE 128

int main(int argc, char **argv) {
    // Check for correct usage and mode selection
    if(argc < 2)
        return print_usage(argv[0]);
    if(argv[1][0] != 'e' && argv[1][0] != 'd')
        return print_usage(argv[0]);

    // Check if encoder/decoder is disabled at compile time
#ifdef RAPIDYENC_DISABLE_ENCODE
    if(argv[1][0] == 'e') {
        fprintf(stderr, "encoder has been disabled in this build\n");
        return EXIT_FAILURE;
    }
#endif
#ifdef RAPIDYENC_DISABLE_DECODE
    if(argv[1][0] == 'd') {
        fprintf(stderr, "decoder has been disabled in this build\n");
        return EXIT_FAILURE;
    }
#endif

    // Open input and output streams (stdin/stdout)
    FILE* infile = stdin; // could be replaced with file input
    if(!infile) {
        fprintf(stderr, "error opening input: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    FILE* outfile = stdout; // could be replaced with file output
    if(!outfile) {
        fprintf(stderr, "error opening output: %s\n", strerror(errno));
        fclose(infile);
        return EXIT_FAILURE;
    }

    // Allocate input buffer
    void* data = malloc(BUFFER_SIZE);
    if(!data) {
        fprintf(stderr, "error allocating input buffer\n");
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
    if(argv[1][0] == 'e') {
        // --- Encoding mode ---
        // Allocate output buffer large enough for encoded data
        void* output = malloc(rapidyenc_encode_max_length(BUFFER_SIZE, LINE_SIZE));
        if(!output) {
            fprintf(stderr, "error allocating output buffer\n");
            fclose(infile);
            fclose(outfile);
            free(data);
            return EXIT_FAILURE;
        }
        rapidyenc_encode_init();

        int column = 0;
        while(1) {
            // Read a chunk from input
            size_t read = fread(data, 1, BUFFER_SIZE, infile);
            int eof = feof(infile);
            if(read < BUFFER_SIZE && !eof) {
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
    if(argv[1][0] == 'd') {
        // --- Decoding mode ---
        rapidyenc_decode_init();

        RapidYencDecoderState state = RYDEC_STATE_CRLF;
        while(1) {
            // Read a chunk from input
            size_t read = fread(data, 1, BUFFER_SIZE, infile);
            RapidYencDecoderEnd ended;
            void* in_ptr = data;
            void* out_ptr = data;
            int eof = feof(infile);
            if(read < BUFFER_SIZE && !eof) {
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
    fclose(infile);
    fclose(outfile);
    free(data);

    // Print CRC32 if enabled and no error occurred
    if(!has_error) {
#ifndef RAPIDYENC_DISABLE_CRC
        fprintf(stderr, "Computed CRC32: %08x\n", crc);
#endif
    }

    return has_error ? EXIT_FAILURE : EXIT_SUCCESS;
}
