/*
 * This file is part of clr-boot-manager.
 *
 * Copyright (C) 2016 Intel Corporation
 *
 * clr-boot-manager is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 */

#pragma once

#define _GNU_SOURCE
#include <mntent.h>
#include <stdarg.h>
#include <stdbool.h>
#include <sys/stat.h>

#include "util.h"

typedef FILE FILE_MNT;

DEF_AUTOFREE(FILE_MNT, endmntent)

/**
 * Get the SHA-1 hashsum for the given path as a hex string
 *
 * @param path Path of the file to hash
 * @return a newly allocated string, or NULL on error
 */
char *get_sha1sum(const char *path);

/**
 * Get the PartUUID for a given path
 *
 * @param path Path to get the PartUUID for
 * @return a newly allocated string, or NULL on error
 */
char *get_part_uuid(const char *path);

/**
 * Return the UEFI device that is used for booting (/boot)
 *
 * @return a newly allocated string, or NULL on error
 */
char *get_boot_device(void);

bool cbm_file_exists(const char *path);

/**
 * Determine if the files match in content by comparing
 * their checksums
 */
bool cbm_files_match(const char *p1, const char *p2);

/**
 * Return the parent path for a given file
 *
 * @note This is an allocated string, and must be freed by the caller
 * @param p Path to file
 * @return a newly allocated string
 */
char *cbm_get_file_parent(const char *p);

/**
 * Recursively make the directories for the given path
 *
 * @param path Path to directory to create
 * @param mode Mode to create new directories with
 *
 * @return A boolean value, indicating success or failure
 */
bool mkdir_p(const char *path, mode_t mode);

/**
 * Recursively remove the tree at @path
 *
 * @param path The path to remove (and all children)
 *
 * @return A boolean value, indicating success or failure
 */
bool rm_rf(const char *path);

/**
 * Quick utility function to write small text files
 *
 * @note This will _always_ overwrite an existing file
 *
 * @param path Path of the file to be written
 * @param text Contents of the new file
 *
 * @return True if this succeeded
 */
bool file_set_text(const char *path, char *text);

/**
 * Quick utility for reading very small files into a string
 *
 * @param path Path of the file to be read
 * @param text Pointer to store newly allocated string
 *
 * @return True if this succeeded, otherwise no allocation is performed
 */
bool file_get_text(const char *path, char **out_buf);

/**
 * Simple utility to copy path @src to path @dst, with mode @mode
 *
 * @note This will truncate the target if it exists, and does
 * not preserve stat information (As we're interested in copying
 * to an ESP only)
 *
 * Note that the implementation uses sendfile() for performance reasons.
 *
 * @param src Path to the source file
 * @param dst Path to the destination file
 * @param mode Mode of the new file when creating
 *
 * @return True if this succeeded
 */
bool copy_file(const char *src, const char *dst, mode_t mode);

/**
 * Wrapper around copy_file to ensure an atomic update of files. This requires
 * that a new file first be written with a new unique name, and only when this
 * has happened, and is sync()'d, we remove the target path if it exists,
 * renaming our newly copied file to match the originally intended filename.
 *
 * This is designed to make the file replacement operation as atomic as
 * possible.
 */
bool copy_file_atomic(const char *src, const char *dst, mode_t mode);

/**
 * Attempt to determine if the given path is actually mounted or not
 *
 * @param path Path to test is mounted or not
 */
bool cbm_is_mounted(const char *path, bool *error);

/**
 * Determine the mountpoint for the given device
 */
char *cbm_get_mountpoint_for_device(const char *device);

/**
 * Gradually build up a valid path, which may point within an existing
 * tree, to mitigate any case sensitivity issues on FAT32
 */
__attribute__((sentinel(0))) char *build_case_correct_path(const char *c, ...);

char *build_case_correct_path_va(const char *c, va_list ap);

/*
 * Editor modelines  -  https://www.wireshark.org/tools/modelines.html
 *
 * Local variables:
 * c-basic-offset: 8
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 *
 * vi: set shiftwidth=8 tabstop=8 expandtab:
 * :indentSize=8:tabSize=8:noTabs=true:
 */
