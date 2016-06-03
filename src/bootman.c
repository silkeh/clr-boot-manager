/*
 * This file is part of clr-boot-manager.
 *
 * Copyright © 2016 Intel Corporation
 *
 * clr-boot-manager is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "bootloader.h"
#include "bootman.h"
#include "files.h"
#include "nica/array.h"
#include "nica/files.h"
#include "util.h"

#include "config.h"

#define BOOT_TIMEOUT_CONFIG SYSCONFDIR "/boot_timeout.conf"

/**
 * Currently only support systemd_bootloader
 */

extern const BootLoader systemd_bootloader;
extern const BootLoader gummiboot_bootloader;
extern const BootLoader goofiboot_bootloader;

struct BootManager {
        char *prefix;
        char *kernel_dir;
        const BootLoader *bootloader;
        char *vendor_prefix;
        char *os_name;
        char *root_uuid;
        char *abs_bootdir; /**<Absolute boot directory, i.e. already mounted */
};

BootManager *boot_manager_new()
{
        struct BootManager *r = NULL;

        r = calloc(1, sizeof(struct BootManager));
        if (!r) {
                return NULL;
        }

        /* Sane defaults. */
        if (!boot_manager_set_prefix(r, "/")) {
                boot_manager_free(r);
                return NULL;
        }

        /* Potentially consider a configure or os-release check */
        boot_manager_set_vendor_prefix(r, "Clear-linux");
        boot_manager_set_os_name(r, "Clear Linux Software for Intel Architecture");

/* Use the bootloader selected at compile time */
#if defined(HAVE_SYSTEMD_BOOT)
        r->bootloader = &systemd_bootloader;
#elif defined(HAVE_GUMMIBOOT)
        r->bootloader = &gummiboot_bootloader;
#else
        r->bootloader = &goofiboot_bootloader;
#endif
        if (!r->bootloader->init(r)) {
                r->bootloader->destroy(r);
                LOG("%s: Cannot initialise bootloader\n", __func__);
                boot_manager_free(r);
                return NULL;
        }

        return r;
}

void boot_manager_free(BootManager *self)
{
        if (!self) {
                return;
        }

        self->bootloader->destroy(self);

        if (self->prefix) {
                free(self->prefix);
        }
        if (self->kernel_dir) {
                free(self->kernel_dir);
        }
        if (self->vendor_prefix) {
                free(self->vendor_prefix);
        }
        if (self->os_name) {
                free(self->os_name);
        }
        if (self->root_uuid) {
                free(self->root_uuid);
        }
        if (self->abs_bootdir) {
                free(self->abs_bootdir);
        }
        free(self);
}

bool boot_manager_set_prefix(BootManager *self, char *prefix)
{
        char *kernel_dir = NULL;
        char *realp = NULL;

        if (!self || !prefix) {
                return false;
        }

        realp = realpath(prefix, NULL);
        if (!realp) {
                LOG("Path specified does not exist: %s\n", prefix);
                return false;
        }

        if (self->prefix) {
                free(self->prefix);
                self->prefix = NULL;
        }
        if (self->root_uuid) {
                free(self->root_uuid);
                self->root_uuid = NULL;
        }
        if (self->kernel_dir) {
                free(self->kernel_dir);
                self->kernel_dir = NULL;
        }

        self->prefix = realp;

        if (!asprintf(&kernel_dir, "%s/%s", self->prefix, KERNEL_DIRECTORY)) {
                DECLARE_OOM();
                abort();
        }

        if (self->kernel_dir) {
                free(self->kernel_dir);
        }
        self->kernel_dir = kernel_dir;

        if (geteuid() == 0) {
                /* Only try this if we have root perms, don't break the
                 * test suite. */
                self->root_uuid = get_part_uuid(self->prefix);
        }

        if (!self->bootloader) {
                return true;
        }
        self->bootloader->destroy(self);
        if (!self->bootloader->init(self)) {
                /* Ensure cleanup. */
                self->bootloader->destroy(self);
                FATAL("Re-initialisation of bootloader failed");
                return false;
        }
        return true;
}

const char *boot_manager_get_prefix(BootManager *self)
{
        if (!self) {
                return NULL;
        }
        return (const char *)self->prefix;
}

const char *boot_manager_get_kernel_dir(BootManager *self)
{
        if (!self) {
                return NULL;
        }
        return (const char *)self->kernel_dir;
}

void boot_manager_set_vendor_prefix(BootManager *self, char *vendor_prefix)
{
        if (!self || !vendor_prefix) {
                return;
        }
        if (self->vendor_prefix) {
                free(self->vendor_prefix);
                self->vendor_prefix = NULL;
        }
        self->vendor_prefix = strdup(vendor_prefix);
}

const char *boot_manager_get_vendor_prefix(BootManager *self)
{
        if (!self) {
                return NULL;
        }
        return (const char *)self->vendor_prefix;
}

void boot_manager_set_os_name(BootManager *self, char *os_name)
{
        if (!self || !os_name) {
                return;
        }
        if (self->os_name) {
                free(self->os_name);
                self->os_name = NULL;
        }
        self->os_name = strdup(os_name);
}

const char *boot_manager_get_os_name(BootManager *self)
{
        if (!self) {
                return NULL;
        }
        return (const char *)self->os_name;
}

Kernel *boot_manager_inspect_kernel(BootManager *self, char *path)
{
        Kernel *kern = NULL;
        autofree(char) *cmp = NULL;
        char type[15] = { 0 };
        char version[15] = { 0 };
        int release = 0;
        autofree(char) *parent = NULL;
        autofree(char) *cmdline = NULL;
        autofree(char) *module_dir = NULL;
        autofree(char) *kconfig_file = NULL;
        autofree(char) *default_file = NULL;
        autofree(char) *kboot_file = NULL;
        /* Consider making this a namespace option */
        autofree(FILE) *f = NULL;
        size_t sn;
        int r = 0;
        char *buf = NULL;
        char *bcp = NULL;
        struct utsname running = { 0 };
        autofree(char) *run_match = NULL;
        autofree(char) *run_match_legacy = NULL;

        if (!self || !path) {
                return NULL;
        }

        cmp = strdup(path);
        if (!cmp) {
                return NULL;
        }
        bcp = basename(cmp);

        /* org.clearlinux.kvm.4.2.1-121 */
        r = sscanf(bcp, KERNEL_NAMESPACE ".%15[^.].%15[^-]-%d", type, version, &release);
        if (r != 3) {
                return NULL;
        }

        parent = cbm_get_file_parent(path);
        if (!asprintf(&cmdline, "%s/cmdline-%s-%d.%s", parent, version, release, type)) {
                DECLARE_OOM();
                abort();
        }

        if (!asprintf(&kconfig_file, "%s/config-%s-%d.%s", parent, version, release, type)) {
                DECLARE_OOM();
                abort();
        }

        if (!asprintf(&kboot_file, "%s-%d-%s", version, release, type)) {
                DECLARE_OOM();
                abort();
        }

        /* TODO: We may actually be uninstalling a partially flopped kernel,
         * so validity of existing kernels may be questionable
         * Thus, flag it, and return kernel */
        if (!nc_file_exists(cmdline)) {
                LOG("Valid kernel found with no cmdline: %s (expected %s)\n", path, cmdline);
                return NULL;
        }

        /* Check local modules */
        if (!asprintf(&module_dir,
                      "%s/%s/%s-%d.%s",
                      self->prefix,
                      KERNEL_MODULES_DIRECTORY,
                      version,
                      release,
                      type)) {
                DECLARE_OOM();
                abort();
        }

        /* Fallback to an older namespace */
        if (!nc_file_exists(module_dir)) {
                free(module_dir);
                if (!asprintf(&module_dir,
                              "%s/%s/%s-%d",
                              self->prefix,
                              KERNEL_MODULES_DIRECTORY,
                              version,
                              release)) {
                        DECLARE_OOM();
                        abort();
                }
                if (!nc_file_exists(module_dir)) {
                        LOG("Valid kernel with no modules: %s %s\n", path, module_dir);
                        return NULL;
                }
        }

        /* Got this far, we have a valid clear kernel */
        kern = calloc(1, sizeof(struct Kernel));
        if (!kern) {
                abort();
        }

        kern->path = strdup(path);
        kern->bpath = strdup(bcp);
        kern->version = strdup(version);
        kern->module_dir = strdup(module_dir);
        kern->ktype = strdup(type);

        if (nc_file_exists(kconfig_file)) {
                kern->kconfig_file = strdup(kconfig_file);
        }
        if (nc_file_exists(kboot_file)) {
                kern->kboot_file = strdup(kboot_file);
                kern->boots = true;
        }

        kern->release = release;

        if (!asprintf(&run_match, "%s-%d.%s", version, release, type)) {
                DECLARE_OOM();
                abort();
        }
        if (!asprintf(&run_match_legacy, "%s-%d", version, release)) {
                DECLARE_OOM();
                abort();
        }

        if (uname(&running) == 0) {
                if (streq(run_match, running.release)) {
                        kern->is_running = true;
                } else if (streq(run_match_legacy, running.release)) {
                        kern->is_running = true;
                }
        }

        if (!(f = fopen(cmdline, "r"))) {
                LOG("Unable to open %s: %s\n", cmdline, strerror(errno));
                free_kernel(kern);
                return NULL;
        }

        /* Keep cmdline in a single "line" with no spaces */
        while ((r = getline(&buf, &sn, f)) > 0) {
                char *tmp = NULL;
                /* Strip newlines */
                if (r > 1 && buf[r - 1] == '\n') {
                        buf[r - 1] = '\0';
                }
                if (kern->cmdline) {
                        if (!asprintf(&tmp, "%s %s", kern->cmdline, buf)) {
                                DECLARE_OOM();
                                abort();
                        }
                        free(kern->cmdline);
                        kern->cmdline = tmp;
                        free(buf);
                } else {
                        kern->cmdline = buf;
                }
                buf = NULL;
        }

        kern->cmdline_file = strdup(cmdline);
        if (buf) {
                free(buf);
        }

        return kern;
}

KernelArray *boot_manager_get_kernels(BootManager *self)
{
        KernelArray *ret = NULL;
        DIR *dir = NULL;
        struct dirent *ent = NULL;
        struct stat st = { 0 };
        if (!self || !self->kernel_dir) {
                return NULL;
        }

        ret = nc_array_new();
        OOM_CHECK_RET(ret, NULL);

        dir = opendir(self->kernel_dir);
        if (!dir) {
                LOG("Error opening %s: %s\n", self->kernel_dir, strerror(errno));
                nc_array_free(&ret, NULL);
                return NULL;
        }

        while ((ent = readdir(dir)) != NULL) {
                autofree(char) *path = NULL;
                Kernel *kern = NULL;

                if (!asprintf(&path, "%s/%s", self->kernel_dir, ent->d_name)) {
                        DECLARE_OOM();
                        abort();
                }

                /* Some kind of broken link */
                if (lstat(path, &st) != 0) {
                        continue;
                }

                /* Regular only */
                if (!S_ISREG(st.st_mode)) {
                        continue;
                }

                /* empty files are skipped too */
                if (st.st_size == 0) {
                        continue;
                }

                /* Now see if its a kernel */
                kern = boot_manager_inspect_kernel(self, path);
                if (!kern) {
                        continue;
                }
                if (!nc_array_add(ret, kern)) {
                        DECLARE_OOM();
                        abort();
                }
        }
        closedir(dir);
        return ret;
}

void free_kernel(Kernel *t)
{
        if (!t) {
                return;
        }
        free(t->path);
        free(t->bpath);
        free(t->version);
        free(t->cmdline);
        free(t->module_dir);
        free(t->cmdline_file);
        free(t->kconfig_file);
        free(t->kboot_file);
        free(t->ktype);
        free(t);
}

const char *boot_manager_get_root_uuid(BootManager *self)
{
        if (!self) {
                return NULL;
        }
        return (const char *)self->root_uuid;
}

bool boot_manager_install_kernel(BootManager *self, const Kernel *kernel)
{
        if (!self || !kernel || !self->bootloader) {
                return false;
        }

        /* Don't allow reinstall, bail back
         * Ensure ESP is mounted/available
         * Copy kernel binary across
         * Write cmdline
         */
        return self->bootloader->install_kernel(self, kernel);
}

bool boot_manager_remove_kernel(BootManager *self, const Kernel *kernel)
{
        if (!self || !kernel || !self->bootloader) {
                return false;
        }

        /*
         * Ensure it is actually installed
         * Remove: kernel, module tree, ESP loader config, .config file, cmdline file, etc
         */
        return self->bootloader->remove_kernel(self, kernel);
}

bool boot_manager_set_default_kernel(BootManager *self, const Kernel *kernel)
{
        if (!self || !kernel || !self->bootloader) {
                return false;
        }

        return self->bootloader->set_default_kernel(self, kernel);
}

char *boot_manager_get_boot_dir(BootManager *self)
{
        char *ret = NULL;

        if (self->abs_bootdir) {
                return strdup(self->abs_bootdir);
        }

        if (!asprintf(&ret, "%s%s", self->prefix, BOOT_DIRECTORY)) {
                DECLARE_OOM();
                abort();
        }
        return ret;
}

bool boot_manager_set_boot_dir(BootManager *self, const char *bootdir)
{
        if (!self || !bootdir) {
                return false;
        }
        if (self->abs_bootdir) {
                free(self->abs_bootdir);
        }
        self->abs_bootdir = strdup(bootdir);
        if (!self->abs_bootdir) {
                return false;
        }

        if (!self->bootloader) {
                return true;
        }
        self->bootloader->destroy(self);
        if (!self->bootloader->init(self)) {
                /* Ensure cleanup. */
                self->bootloader->destroy(self);
                FATAL("Re-initialisation of bootloader failed");
                return false;
        }
        return true;
}

bool boot_manager_modify_bootloader(BootManager *self, BootLoaderOperation op)
{
        if (!self || !self->bootloader) {
                return false;
        }

        if (op & BOOTLOADER_OPERATION_INSTALL) {
                if (op & BOOTLOADER_OPERATION_NO_CHECK) {
                        return self->bootloader->install(self);
                }
                if (self->bootloader->needs_install(self)) {
                        return self->bootloader->install(self);
                }
                return true;
        } else if (op & BOOTLOADER_OPERATION_REMOVE) {
                return self->bootloader->remove(self);
        } else if (op & BOOTLOADER_OPERATION_UPDATE) {
                if (op & BOOTLOADER_OPERATION_NO_CHECK) {
                        return self->bootloader->update(self);
                }
                if (self->bootloader->needs_update(self)) {
                        return self->bootloader->update(self);
                }
                return true;
        } else {
                LOG("boot_manager_modify_bootloader: Unknown operation\n");
                return false;
        }
}

static inline uint8_t _detect_platform_size(void)
{
/* Only when we can't reliably detect the firmware architecture */
#if UINTPTR_MAX == 0xffffffffffffffff
        return 64;
#else
        return 32;
#endif
}

uint8_t boot_manager_get_architecture_size(__attribute__((unused)) BootManager *manager)
{
        return _detect_platform_size();
}

/**
 * We'll add a check here later to allow for differences in subdir usage
 */
uint8_t boot_manager_get_platform_size(__attribute__((unused)) BootManager *manager)
{
        int fd;
        char buffer[3];

        fd = open("/sys/firmware/efi/fw_platform_size", O_RDONLY);
        if (fd < 0) {
                return _detect_platform_size();
        }

        if (read(fd, buffer, sizeof(buffer)) != sizeof(buffer)) {
                LOG("boot_manager_get_platform_size: Problematic firmware interface\n");
                close(fd);
                return _detect_platform_size();
        }

        close(fd);

        if (strncmp(buffer, "32", 2) == 0) {
                return 32;
        } else if (strncmp(buffer, "64", 2) == 0) {
                return 64;
        } else {
                return _detect_platform_size();
        }
}

bool boot_manager_is_image_mode(BootManager *self)
{
        if (!self || !self->prefix) {
                return false;
        }
        return !streq(self->prefix, "/");
}

bool boot_manager_set_timeout_value(BootManager *self, int timeout)
{
        autofree(FILE) *fp = NULL;
        char *path = NULL;

        if (!self || !self->prefix) {
                return false;
        }

        if (!asprintf(&path, "%s%s", self->prefix, BOOT_TIMEOUT_CONFIG)) {
                DECLARE_OOM();
                return -1;
        }

        if (timeout <= 0) {
                /* Nothing to be done here. */
                if (!nc_file_exists(path)) {
                        return true;
                }
                if (unlink(path) < 0) {
                        fprintf(stderr, "Unable to remove %s: %s\n", path, strerror(errno));
                        return false;
                }
                return true;
        }

        fp = fopen(path, "w");
        if (!fp) {
                fprintf(stderr, "Unable to open %s for writing: %s\n", path, strerror(errno));
                return false;
        }

        if (fprintf(fp, "%d\n", timeout) < 0) {
                fprintf(stderr, "Unable to set new timeout: %s\n", strerror(errno));
                return false;
        }
        return true;
}

int boot_manager_get_timeout_value(BootManager *self)
{
        autofree(FILE) *fp = NULL;
        autofree(char) *path = NULL;
        int t_val;

        if (!self || !self->prefix) {
                return false;
        }

        if (!asprintf(&path, "%s%s", self->prefix, BOOT_TIMEOUT_CONFIG)) {
                DECLARE_OOM();
                return -1;
        }

        /* Default timeout being -1, i.e. don't use one */
        if (!nc_file_exists(path)) {
                return -1;
        }

        fp = fopen(path, "r");
        if (!fp) {
                fprintf(stderr, "Unable to open %s for reading: %s\n", path, strerror(errno));
                return -1;
        }

        if (fscanf(fp, "%d\n", &t_val) != 1) {
                fprintf(stderr, "Failed to parse config file, defaulting to no timeout\n");
                return -1;
        }

        return t_val;
}

bool boot_manager_needs_install(BootManager *self)
{
        if (!self) {
                return false;
        }
        return self->bootloader->needs_install(self);
}

bool boot_manager_needs_update(BootManager *self)
{
        if (!self) {
                return false;
        }
        return self->bootloader->needs_update(self);
}

bool boot_manager_is_kernel_installed(BootManager *self, const Kernel *kernel)
{
        return self->bootloader->is_kernel_installed(self, kernel);
}

Kernel *boot_manager_get_default_for_type(BootManager *self, KernelArray *kernels, char *type)
{
        autofree(char) *default_file = NULL;
        char linkbuf[PATH_MAX] = { 0 };

        if (!self || !kernels || !type) {
                return NULL;
        }

        if (asprintf(&default_file, "%s/default-%s", self->kernel_dir, type) < 0) {
                return NULL;
        }

        if (readlink(default_file, linkbuf, sizeof(linkbuf)) < 0) {
                return NULL;
        }

        for (int i = 0; i < kernels->len; i++) {
                Kernel *k = nc_array_get(kernels, i);
                if (streq(k->bpath, linkbuf)) {
                        return k;
                }
        }

        return NULL;
}

static inline void kern_dup_free(void *v)
{
        NcArray *array = v;
        nc_array_free(&array, NULL);
}

NcHashmap *boot_manager_map_kernels(BootManager *self, KernelArray *kernels)
{
        if (!self || !kernels) {
                return NULL;
        }

        NcHashmap *map = NULL;

        map = nc_hashmap_new_full(nc_string_hash, nc_string_compare, free, kern_dup_free);

        for (int i = 0; i < kernels->len; i++) {
                KernelArray *r = NULL;
                Kernel *cur = nc_array_get(kernels, i);

                r = nc_hashmap_get(map, cur->ktype);
                if (!r) {
                        r = nc_array_new();
                        if (!r) {
                                goto oom;
                        }
                        if (!nc_hashmap_put(map, strdup(cur->ktype), r)) {
                                kern_dup_free(r);
                                goto oom;
                        }
                }
                if (!nc_array_add(r, cur)) {
                        goto oom;
                }
        }

        return map;
oom:
        DECLARE_OOM();
        nc_hashmap_free(map);
        return NULL;
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
