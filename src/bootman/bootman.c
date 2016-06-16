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

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "bootloader.h"
#include "bootman.h"
#include "bootman_private.h"
#include "files.h"
#include "nica/files.h"

#include "config.h"

/**
 * Currently only support systemd_bootloader
 */

extern const BootLoader systemd_bootloader;
extern const BootLoader gummiboot_bootloader;
extern const BootLoader goofiboot_bootloader;

BootManager *boot_manager_new()
{
        struct BootManager *r = NULL;
        struct utsname uts = { 0 };

        r = calloc(1, sizeof(struct BootManager));
        if (!r) {
                return NULL;
        }

        /* Try to parse the currently running kernel */
        if (uname(&uts) == 0) {
                if (!boot_manager_set_uname(r, uts.release)) {
                        fprintf(stderr, "Warning: Unable to parse kernel\n");
                }
        }

        /* Sane defaults. */
        if (!boot_manager_set_prefix(r, "/")) {
                boot_manager_free(r);
                return NULL;
        }

        /* Potentially consider a configure or os-release check */
        boot_manager_set_vendor_prefix(r, "Clear-linux");
        boot_manager_set_os_name(r, "Clear Linux Software for Intel Architecture");
        /* CLI should override these */
        boot_manager_set_can_mount(r, false);
        boot_manager_set_image_mode(r, false);

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

        if (self->bootloader) {
                self->bootloader->destroy(self);
        }

        free(self->prefix);
        free(self->kernel_dir);
        free(self->vendor_prefix);
        free(self->os_name);
        free(self->root_uuid);
        free(self->abs_bootdir);
        free(self);
}

bool boot_manager_set_prefix(BootManager *self, char *prefix)
{
        assert(self != NULL);

        char *kernel_dir = NULL;
        char *realp = NULL;

        if (!prefix) {
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
        assert(self != NULL);

        return (const char *)self->prefix;
}

const char *boot_manager_get_kernel_dir(BootManager *self)
{
        assert(self != NULL);

        return (const char *)self->kernel_dir;
}

void boot_manager_set_vendor_prefix(BootManager *self, char *vendor_prefix)
{
        assert(self != NULL);

        if (!vendor_prefix) {
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
        assert(self != NULL);

        return (const char *)self->vendor_prefix;
}

void boot_manager_set_os_name(BootManager *self, char *os_name)
{
        assert(self != NULL);

        if (!os_name) {
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
        assert(self != NULL);

        return (const char *)self->os_name;
}

const char *boot_manager_get_root_uuid(BootManager *self)
{
        assert(self != NULL);

        return (const char *)self->root_uuid;
}

bool boot_manager_install_kernel(BootManager *self, const Kernel *kernel)
{
        assert(self != NULL);

        if (!kernel || !self->bootloader) {
                return false;
        }

        /* Install the kernel blob first */
        if (!boot_manager_install_kernel_internal(self, kernel)) {
                return false;
        }
        /* Hand over to the bootloader to finish it up */
        return self->bootloader->install_kernel(self, kernel);
}

bool boot_manager_remove_kernel(BootManager *self, const Kernel *kernel)
{
        assert(self != NULL);

        if (!kernel || !self->bootloader) {
                return false;
        }

        /* Remove the kernel blob first */
        if (!boot_manager_remove_kernel_internal(self, kernel)) {
                return false;
        }
        /* Hand over to the bootloader to finish it up */
        return self->bootloader->remove_kernel(self, kernel);
}

bool boot_manager_set_default_kernel(BootManager *self, const Kernel *kernel)
{
        assert(self != NULL);

        if (!self->bootloader) {
                return false;
        }

        return self->bootloader->set_default_kernel(self, kernel);
}

char *boot_manager_get_boot_dir(BootManager *self)
{
        assert(self != NULL);

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
        assert(self != NULL);

        if (!bootdir) {
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
        assert(self != NULL);

        if (!self->bootloader) {
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

uint8_t boot_manager_get_architecture_size(__cbm_unused__ BootManager *manager)
{
        return _detect_platform_size();
}

/**
 * We'll add a check here later to allow for differences in subdir usage
 */
uint8_t boot_manager_get_platform_size(__cbm_unused__ BootManager *manager)
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
        assert(self != NULL);

        return self->image_mode;
}

void boot_manager_set_image_mode(BootManager *self, bool image_mode)
{
        assert(self != NULL);

        self->image_mode = image_mode;
}

bool boot_manager_needs_install(BootManager *self)
{
        assert(self != NULL);

        return self->bootloader->needs_install(self);
}

bool boot_manager_needs_update(BootManager *self)
{
        assert(self != NULL);

        return self->bootloader->needs_update(self);
}

bool boot_manager_is_kernel_installed(BootManager *self, const Kernel *kernel)
{
        assert(self != NULL);

        /* Ensure the blob is in place */
        if (!boot_manager_is_kernel_installed_internal(self, kernel)) {
                return false;
        }
        /* Last bits, like config files */
        return self->bootloader->is_kernel_installed(self, kernel);
}

bool boot_manager_set_uname(BootManager *self, const char *uname)
{
        assert(self != NULL);

        if (!uname) {
                return false;
        }
        SystemKernel k = { 0 };
        bool have_sys_kernel = cbm_parse_system_kernel(uname, &k);
        if (!have_sys_kernel) {
                self->have_sys_kernel = false;
                return false;
        }

        memcpy(&(self->sys_kernel), &k, sizeof(struct SystemKernel));
        self->have_sys_kernel = have_sys_kernel;
        return self->have_sys_kernel;
}

void boot_manager_set_can_mount(BootManager *self, bool can_mount)
{
        assert(self != NULL);
        self->can_mount = can_mount;
}

bool boot_manager_get_can_mount(BootManager *self)
{
        assert(self != NULL);
        return self->can_mount;
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
