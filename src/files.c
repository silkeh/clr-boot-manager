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

#define _GNU_SOURCE

#include <blkid.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <glob.h>
#include <libgen.h>
#include <limits.h>
#include <openssl/sha.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <unistd.h>

#include "files.h"
#include "nica/files.h"
#include "util.h"

#define COPY_BUFFER_SIZE 8192

DEF_AUTOFREE(DIR, closedir)

/**
 * By default we call sync() - for testing however we disable this due to timeout
 * issues.
 */
static bool cbm_should_sync = true;

__cbm_inline__ static inline void cbm_sync(void)
{
        if (cbm_should_sync) {
                sync();
        }
}

char *get_sha1sum(const char *p)
{
        unsigned char hash[SHA_DIGEST_LENGTH] = { 0 };
        int fd = -1;
        struct stat st = { 0 };
        char *buffer = NULL;
        char *ret = NULL;
        int max_len = SHA_DIGEST_LENGTH * 2;

        if ((fd = open(p, O_RDONLY)) < 0) {
                goto finish;
        }
        if (fstat(fd, &st) < 0) {
                goto finish;
        }

        buffer = mmap(0, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (!buffer) {
                goto finish;
        }

        if (!SHA1((unsigned char *)buffer, st.st_size, hash)) {
                goto finish;
        }

        ret = calloc((max_len) + 1, sizeof(char));
        if (!ret) {
                goto finish;
        }

        for (int i = 0; i < SHA_DIGEST_LENGTH; i++) {
                snprintf(ret + (i * 2), max_len, "%02x", hash[i]);
        }
        ret[max_len] = '\0';

finish:
        if (fd >= 0) {
                close(fd);
        }
        if (buffer) {
                munmap(buffer, st.st_size);
        }
        return ret;
}

bool cbm_files_match(const char *p1, const char *p2)
{
        autofree(char) *h1 = NULL;
        autofree(char) *h2 = NULL;

        h1 = get_sha1sum(p1);
        if (!h1) {
                return false;
        }
        h2 = get_sha1sum(p2);
        if (!h2) {
                return false;
        }

        return streq(h1, h2);
}

char *get_part_uuid(const char *path)
{
        struct stat st = { 0 };
        autofree(char) *node = NULL;
        blkid_probe probe = NULL;
        const char *value = NULL;
        char *ret = NULL;

        if (stat(path, &st) != 0) {
                LOG("Path does not exist: %s\n", path);
                return NULL;
        }

        if (major(st.st_dev) == 0) {
                LOG("Invalid block device: %s\n", path);
                return NULL;
        }

        if (!asprintf(&node, "/dev/block/%u:%u", major(st.st_dev), minor(st.st_dev))) {
                DECLARE_OOM();
                return NULL;
        }

        probe = blkid_new_probe_from_filename(node);
        if (!probe) {
                LOG("Unable to probe %u:%u\n", major(st.st_dev), minor(st.st_dev));
                return NULL;
        }

        blkid_probe_enable_superblocks(probe, 1);
        blkid_probe_set_superblocks_flags(probe, BLKID_SUBLKS_TYPE);
        blkid_probe_enable_partitions(probe, 1);
        blkid_probe_set_partitions_flags(probe, BLKID_PARTS_ENTRY_DETAILS);

        if (blkid_do_safeprobe(probe) != 0) {
                LOG("Error probing filesystem: %s\n", strerror(errno));
                goto clean;
        }

        if (blkid_probe_lookup_value(probe, "PART_ENTRY_UUID", &value, NULL) != 0) {
                LOG("Unable to find UUID %s\n", strerror(errno));
                value = NULL;
                goto clean;
        }

        ret = strdup(value);
clean:
        blkid_free_probe(probe);
        errno = 0;
        return ret;
}

char *get_boot_device()
{
        glob_t glo = { 0 };
        char read_buf[4096];
        glo.gl_offs = 1;
        int fd = -1;
        ;
        ssize_t size = 0;
        autofree(char) *uuid = NULL;
        autofree(char) *p = NULL;

        glob("/sys/firmware/efi/efivars/LoaderDevicePartUUID*", GLOB_DOOFFS, NULL, &glo);

        if (glo.gl_pathc < 1) {
                goto next;
        }

        /* Read the uuid */
        if ((fd = open(glo.gl_pathv[1], O_RDONLY | O_NOCTTY | O_CLOEXEC)) < 0) {
                LOG("Unable to read LoaderDevicePartUUID\n");
                return NULL;
        }
        globfree(&glo);

        size = read(fd, read_buf, sizeof(read_buf));
        close(fd);
        if (size < 1) {
                goto next;
        }

        uuid = calloc(size + 1, sizeof(char));
        int j = 0;
        for (ssize_t i = 0; i < size; i++) {
                char c = read_buf[i];
                if (!isalnum(c) && c != '-' && c != '_') {
                        continue;
                }
                if (c == '_' || c == '-') {
                        uuid[j] = '-';
                } else {
                        uuid[j] = tolower(read_buf[i]);
                }
                ++j;
        }
        read_buf[j] = '\0';

        if (!asprintf(&p, "/dev/disk/by-partuuid/%s", uuid)) {
                DECLARE_OOM();
                return NULL;
        }

        if (nc_file_exists(p)) {
                return strdup(p);
        }
next:

        if (nc_file_exists("/dev/disk/by-partlabel/ESP")) {
                return strdup("/dev/disk/by-partlabel/ESP");
        }
        return NULL;
}

char *cbm_get_file_parent(const char *p)
{
        char *r = realpath(p, NULL);
        if (!r) {
                return NULL;
        }
        return dirname(r);
}

bool file_set_text(const char *path, char *text)
{
        FILE *fp = NULL;
        bool ret = false;

        if (nc_file_exists(path) && unlink(path) < 0) {
                return false;
        }
        cbm_sync();

        fp = fopen(path, "w");

        if (!fp) {
                goto end;
        }

        if (fprintf(fp, "%s", text) < 0) {
                goto end;
        }
        ret = true;
end:
        if (fp) {
                fclose(fp);
        }
        cbm_sync();

        return ret;
}

bool file_get_text(const char *path, char **out_buf)
{
        FILE *fp = NULL;
        char buffer[CHAR_MAX] = { 0 };
        bool ret = false;
        __attribute__((unused)) int r;

        if (!out_buf) {
                return false;
        }

        fp = fopen(path, "r");
        if (!fp) {
                return false;
        }
        r = fread(buffer, sizeof(buffer), 1, fp);
        if (!ferror(fp)) {
                ret = true;
                *out_buf = strdup(buffer);
        }
        fclose(fp);

        return ret;
}

bool copy_file(const char *src, const char *target, mode_t mode)
{
        struct stat sst = { 0 };
        size_t sz;
        int sfd = -1;
        int dfd = -1;
        bool ret = false;
        ssize_t written;

        sfd = open(src, O_RDONLY);
        if (sfd < 0) {
                return false;
        }
        dfd = open(target, O_WRONLY | O_TRUNC | O_CREAT, mode);
        if (dfd < 0) {
                goto end;
        }
        if (fstat(sfd, &sst) != 0) {
                goto end;
        }

        sz = sst.st_size;
        for (;;) {
                written = sendfile(dfd, sfd, NULL, sz);
                if ((size_t)written == sz) {
                        break;
                } else if (written < 0) {
                        goto end;
                }
                sz -= written;
        }
        ret = true;

end:
        if (sfd > 0) {
                close(sfd);
        }
        if (dfd > 0) {
                close(dfd);
        }
        return ret;
}

bool copy_file_atomic(const char *src, const char *target, mode_t mode)
{
        autofree(char) *new_name = NULL;
        struct stat st = { 0 };

        if (!asprintf(&new_name, "%s.TmpWrite", target)) {
                return false;
        }

        if (!copy_file(src, new_name, mode)) {
                (void)unlink(new_name);
                return false;
        }
        cbm_sync();

        /* Delete target if needed  */
        if (stat(target, &st) == 0) {
                if (!S_ISDIR(st.st_mode) && unlink(target) != 0) {
                        return false;
                }
                cbm_sync();
        } else {
                errno = 0;
        }

        if (rename(new_name, target) != 0) {
                return false;
        }
        /* vfat protect */
        cbm_sync();

        return true;
}

bool cbm_is_mounted(const char *path, bool *error)
{
        autofree(FILE_MNT) *tab = NULL;
        struct mntent *ent = NULL;
        struct mntent mnt = { 0 };
        char buf[8192];

        if (error) {
                *error = false;
        }

        tab = setmntent("/proc/self/mounts", "r");
        if (!tab) {
                if (error) {
                        *error = true;
                }
                return false;
        }

        while ((ent = getmntent_r(tab, &mnt, buf, sizeof(buf)))) {
                if (mnt.mnt_dir && streq(path, mnt.mnt_dir)) {
                        return true;
                }
        }

        return false;
}

char *cbm_get_mountpoint_for_device(const char *device)
{
        autofree(FILE_MNT) *tab = NULL;
        struct mntent *ent = NULL;
        struct mntent mnt = { 0 };
        char buf[8192];
        autofree(char) *abs_path = NULL;

        abs_path = realpath(device, NULL);
        if (!abs_path) {
                return NULL;
        }

        tab = setmntent("/proc/self/mounts", "r");
        if (!tab) {
                return NULL;
        }

        while ((ent = getmntent_r(tab, &mnt, buf, sizeof(buf)))) {
                if (!mnt.mnt_fsname) {
                        continue;
                }
                autofree(char) *mnt_device = realpath(mnt.mnt_fsname, NULL);
                if (!mnt_device) {
                        continue;
                }
                if (streq(abs_path, mnt_device)) {
                        return strdup(mnt.mnt_dir);
                }
        }
        return NULL;
}

bool cbm_system_has_uefi()
{
        return nc_file_exists("/sys/firmware/efi");
}

void cbm_set_sync_filesystems(bool should_sync)
{
        cbm_should_sync = should_sync;
}

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
