/*
 * Copyright (c) 2016, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY LOG OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <assert.h>

void msrsave_test_mock_msr(void *buffer, size_t buffer_size, const char *path_format, int num_cpu);
void msrsave_test_check_msr(uint64_t *buffer, size_t num_check, const char *path_format, int num_cpu);

void msrsave_test_mock_msr(void *buffer, size_t buffer_size, const char *path_format, int num_cpu)
{
    /* Create mock msr files for each CPU */
    int i;
    char this_path[NAME_MAX] = {};
    for (i = 0; i < num_cpu; ++i) {
        snprintf(this_path, NAME_MAX, path_format, i);
        FILE *fid = fopen(this_path, "w");
        assert(fid != 0);
        fwrite(buffer, 1, buffer_size, fid);
        fclose(fid);
    }
}

void msrsave_test_check_msr(uint64_t *check_val, size_t num_check, const char *path_format, int num_cpu)
{
    /* Create mock msr files for each CPU */
    int i, j;
    char this_path[NAME_MAX] = {};
    uint64_t *read_val = malloc(num_check * sizeof(uint64_t));
    assert(read_val != NULL);
    for (i = 0; i < num_cpu; ++i) {
        snprintf(this_path, NAME_MAX, path_format, i);
        FILE *fid = fopen(this_path, "r");
        assert(fid != 0);
        fread(read_val, sizeof(uint64_t), num_check, fid);
        fclose(fid);
        for (j = 0; j < num_check; ++j) {
            assert(check_val[j] == read_val[j]);
        }
    }
    free(read_val);
}


int main(int argc, char **argv)
{
    int err = 0;
    const uint64_t whitelist_off[] = {0x0000000000000000ULL,
                                      0x0000000000000040ULL,
                                      0x0000000000000080ULL,
                                      0x00000000000000c0ULL,
                                      0x0000000000000100ULL,
                                      0x0000000000000140ULL,
                                      0x0000000000000180ULL,
                                      0x00000000000001c0ULL,
                                      0x0000000000000200ULL,
                                      0x0000000000000240ULL,
                                      0x0000000000000280ULL,
                                      0x00000000000002c0ULL,
                                      0x0000000000000300ULL,
                                      0x0000000000000340ULL,
                                      0x0000000000000380ULL,
                                      0x00000000000003c0ULL,
                                      0x0000000000000400ULL,
                                      0x0000000000000440ULL,
                                      0x0000000000000480ULL,
                                      0x00000000000004c0ULL};

    const uint64_t whitelist_mask[] = {0x0fffffffffffffffULL,
                                       0x0fffffffffffffffULL,
                                       0x0fffffffffffffffULL,
                                       0x0fffffffffffffffULL,
                                       0x0fffffffffffffffULL,
                                       0x0fffffffffffffffULL,
                                       0x0fffffffffffffffULL,
                                       0x0fffffffffffffffULL,
                                       0x0fffffffffffffffULL,
                                       0x0fffffffffffffffULL,
                                       0x0fffffffffffffffULL,
                                       0x0fffffffffffffffULL,
                                       0x0fffffffffffffffULL,
                                       0x0fffffffffffffffULL,
                                       0x0fffffffffffffffULL,
                                       0x0fffffffffffffffULL,
                                       0x0fffffffffffffffULL,
                                       0x0fffffffffffffffULL,
                                       0x0fffffffffffffffULL,
                                       0x0fffffffffffffffULL};

    enum {NUM_MSR = sizeof(whitelist_off) / sizeof(uint64_t)};
    assert(NUM_MSR == sizeof(whitelist_mask) / sizeof(uint64_t));
    const char *test_save_path = "msrsave_test_store";
    const char *test_whitelist_path = "msrsave_test_whitelist";
    const char *test_msr_path = "msrsave_test_msr.%d";
    const char *whitelist_format = "MSR: %.8llx Write Mask: %.16llx\n";
    const int num_cpu = 10;
    int i;

    /* Create a mock white list from the data in the constants above. */
    FILE *fid = fopen(test_whitelist_path, "w");
    assert(fid != NULL);
    for (i = 0; i < NUM_MSR; ++i) {
        fprintf(fid, whitelist_format, whitelist_off[i], whitelist_mask[i]);
    }
    fclose(fid);

    uint64_t lval = 0x0;
    uint64_t hval = 0xDEADBEEF;
    uint64_t msr_val[NUM_MSR];

    /* Create mock msr data*/
    for (i = 0; i < NUM_MSR; ++i) {
        lval = i;
        msr_val[i] = lval | (hval << 32);
    }

    msrsave_test_mock_msr(msr_val, sizeof(msr_val), test_msr_path, num_cpu);

    /* Save the current state to a file */
    msr_save(test_save_path, test_whitelist_path, test_msr_path, num_cpu);

    /* Overwrite the mock msr files with new data */
    hval = 0x1EADBEEF;
    for (i = 0; i < NUM_MSR; ++i) {
        lval = NUM_MSR - i;
        msr_val[i] = lval | (hval << 32);
    }

    msrsave_test_mock_msr(msr_val, sizeof(msr_val), test_msr_path, num_cpu);

    /* Restore to the original values */
    msr_restore(test_save_path, test_whitelist_path, test_msr_path, num_cpu);

    /* Check that the values that are writable have been restored. */
    /* Check that the values that are not writable have been unaltered. */
    hval = 0x9EADBEEF;
    for (i = 0; i < NUM_MSR; ++i) {
        lval = NUM_MSR - i;
        msr_val[i] = lval | (hval << 32);
    }
    msrsave_test_check_msr(msr_val, sizeof(msr_val), test_msr_path, num_cpu);

    char this_path[NAME_MAX] = {};
    for (i = 0; i < num_cpu; ++i) {
        snprintf(this_path, NAME_MAX, test_msr_path, i);
        unlink(this_path);
    }
    unlink(test_whitelist_path);
    unlink(test_save_path);
    return err;
}
