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
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>
#include <string.h>
#include "msrsave.h"

int msr_save(const char *out_path, const char *whitelist_path, const char *msr_path_format, int num_cpu)
{
    int err = 0;
    int tmp_err = 0;
    int i, j;
    int msr_fd;
    char err_msg[NAME_MAX];
    size_t size = 0;
    size_t num_scan = 0;
    size_t num_msr = 0;
    struct stat whitelist_stat;
    char *whitelist_buffer = NULL;
    char *whitelist_ptr = NULL;
    uint64_t *msr_offset = NULL;
    uint64_t *msr_mask = NULL;
    uint64_t *result = NULL;
    FILE *whitelist_fid = NULL;
    FILE *save_fid = NULL;

    /* Figure out how big the whitelist file is */
    err = stat(whitelist_path, &whitelist_stat);
    if (err != 0) {
        err = errno ? errno : -1;
        snprintf(err_msg, NAME_MAX, "stat() of %s failed! ", whitelist_path);
        perror(err_msg);
        goto exit;
    }

    if (whitelist_stat.st_size == 0) {
        err = errno ? errno : -1;
        snprintf(err_msg, NAME_MAX, "Whitelist file (%s) size is zero!", whitelist_path);
        perror(err_msg);
        goto exit;
    }

    /* Allocate buffer for file contents */
    whitelist_buffer = (char*)malloc(whitelist_stat.st_size);
    if (!whitelist_buffer) {
        err = errno ? errno : -1;
        snprintf(err_msg, NAME_MAX, "Could not allocate array of size %zu!", whitelist_stat.st_size);
        perror(err_msg);
        goto exit;
    }

    /* Open file */
    whitelist_fid = fopen(whitelist_path, "r");
    if (!whitelist_fid) {
        snprintf(err_msg, NAME_MAX, "Could not open whitelist file \"%s\"!", whitelist_path);
        perror(err_msg);
        err = errno;
        goto exit;
    }

    /* Read contents */
    size_t num_read = fread(whitelist_buffer, 1, whitelist_stat.st_size, whitelist_fid);
    if (num_read != whitelist_stat.st_size) {
        err = errno ? errno : -1;
        snprintf(err_msg, NAME_MAX, "Contents read from whitelist file is too small: %zu < %zu!", num_read, whitelist_stat.st_size);
        perror(err_msg);
        goto exit;
    }

    /* Count the number of new lines in the file */
    whitelist_ptr = whitelist_buffer;
    for (num_msr = 0;
         whitelist_ptr = strchr(whitelist_ptr, '\n');
         ++num_msr) {
        ++whitelist_ptr;
    }

    /* Allocate buffers for parsed whitelist */
    msr_offset = (uint64_t *)malloc(sizeof(uint64_t) * num_msr);
    if (!msr_offset) {
        err = errno ? errno : -1;
        snprintf(err_msg, NAME_MAX, "Unable to allocate msr offset data of size: %zu!", sizeof(uint64_t) * num_msr);
        perror(err_msg);
        goto exit;
    }

    msr_mask = (uint64_t *)malloc(sizeof(uint64_t) * num_msr);
    if (!msr_mask) {
        err = errno ? errno : -1;
        snprintf(err_msg, NAME_MAX, "Unable to allocate msr mask data of size: %zu!", sizeof(uint64_t) * num_msr);
        perror(err_msg);
        goto exit;
    }

    /* Parse the whitelist */
    const char *whitelist_format = "MSR: %llx Write Mask: %llx\n";
    whitelist_ptr = whitelist_buffer;
    for (i = 0; i < num_msr; ++i) {
        num_scan = sscanf(whitelist_ptr, whitelist_format, msr_offset + i, msr_mask + i);
        if (num_scan != 2) {
            err = -1;
            fprintf(stderr, "Error: Failed to parse whitelist file named \"%s\"\n", whitelist_path);
            goto exit;
        }
        whitelist_ptr = strchr(whitelist_ptr, '\n');
        whitelist_ptr++; /* Move the pointer to the next line */
        if (!whitelist_ptr) {
            err = -1;
            fprintf(stderr, "Error: Failed to parse whitelist file named \"%s\"\n", whitelist_path);
            goto exit;
        }
    }

    int close_err = fclose(whitelist_fid);
    if (close_err) {
        err = errno ? errno : -1;
        snprintf(err_msg, NAME_MAX, "Unable to close whitelist file called \"%s\"", whitelist_path);
        perror(err_msg);
        goto exit;
    }
    whitelist_fid = NULL;

    /* Allocate save buffer, a 2-D array over msr offset and then CPU
       (offset major ordering) */
    result = (uint64_t *)malloc(num_msr * num_cpu * sizeof(uint64_t));
    if (!result) {
        err = errno ? errno : -1;
        snprintf(err_msg, NAME_MAX, "Unable to allocate msr save state buffer of size: %zu!", num_msr * num_cpu * sizeof(uint64_t));
        perror(err_msg);
        goto exit;
    }

    /* Open all MSR files.
     * Read ALL existing data
     * Pass through the whitelist mask. */
    for (i = 0; i < num_cpu; ++i) {
        char msr_file_name[NAME_MAX];
        snprintf(msr_file_name, NAME_MAX, msr_path_format, i);
        msr_fd = open(msr_file_name, O_RDWR);
        if (msr_fd == -1) {
            err = errno ? errno : -1;
            snprintf(err_msg, NAME_MAX, "Could not open MSR file \"%s\"!", msr_file_name);
            perror(err_msg);
            goto exit;
        }
        for (j = 0; j < num_msr; ++j) {
            ssize_t read_count = pread(msr_fd, result + i * num_msr + j,
                                       sizeof(uint64_t), msr_offset[j]);
            if (read_count != sizeof(uint64_t)) {
                err = errno ? errno : -1;
                snprintf(err_msg, NAME_MAX, "Failed to read msr value from MSR file \"%s\"!", msr_file_name);
                perror(err_msg);
                goto exit;
            }
            result[i * num_msr + j] &= msr_mask[j];
        }
        tmp_err = close(msr_fd);
        msr_fd = -1;
        if (tmp_err) {
            err = errno ? errno : -1;
            snprintf(err_msg, NAME_MAX, "Could not close MSR file \"%s\"!", msr_file_name);
            perror(err_msg);
            goto exit;
        }
    }

    /* Open output file. */
    save_fid = fopen(out_path, "w");
    if (!save_fid) {
        err = errno ? errno : -1;
        snprintf(err_msg, NAME_MAX, "Could not open output file \"%s\"!", out_path);
        perror(err_msg);
        goto exit;
    }

    size_t num_write = fwrite(result, sizeof(uint64_t), num_msr * num_cpu, save_fid);
    if (num_write != num_msr * num_cpu) {
        err = errno ? errno : -1;
        snprintf(err_msg, NAME_MAX, "Could not write all values to output file \"%s\"!", out_path);
        perror(err_msg);
        goto exit;
    }

    tmp_err = fclose(save_fid);
    save_fid = NULL;
    if (tmp_err) {
        err = errno ? errno : -1;
        snprintf(err_msg, NAME_MAX, "Could not close MSR file \"%s\"!", out_path);
        perror(err_msg);
        goto exit;
    }

    /* Clean up memory and files */
exit:
    if (result) {
        free(result);
    }
    if (msr_offset) {
        free(msr_offset);
    }
    if (msr_mask) {
        free(msr_mask);
    }
    if (whitelist_buffer) {
        free(whitelist_buffer);
    }
    if (whitelist_fid) {
        fclose(whitelist_fid);
    }
    if (save_fid) {
        fclose(save_fid);
    }
    if (msr_fd != -1) {
        close(msr_fd);
    }
    return err;
}

int msr_restore(const char *file_name, const char *whitelist_path, const char *msr_path_format, int num_cpu)
{
    return 0;
}
