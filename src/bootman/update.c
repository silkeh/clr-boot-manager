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
#include <string.h>
#include <sys/mount.h>
#include <unistd.h>

#include "bootman.h"
#include "bootman_private.h"
#include "files.h"
#include "nica/files.h"

static bool boot_manager_update_image(BootManager *self);
static bool boot_manager_update_native(BootManager *self);

/**
 * Sort by release number, putting highest first
 */
static int kernel_compare_reverse(const void *a, const void *b)
{
        const Kernel *ka = *(const Kernel **)a;
        const Kernel *kb = *(const Kernel **)b;

        if (ka->release > kb->release) {
                return -1;
        }
        return 1;
}

bool boot_manager_update(BootManager *self)
{
        assert(self != NULL);
        bool ret = false;
        autofree(char) *boot_dir = NULL;
        bool did_mount = false;

        /* Image mode is very simple, no prep/cleanup */
        if (boot_manager_is_image_mode(self)) {
                return boot_manager_update_image(self);
        }

        /* Get our boot directory */
        boot_dir = boot_manager_get_boot_dir(self);
        if (!boot_dir) {
                DECLARE_OOM();
                return false;
        }

        /* Prepare mounts */
        if (boot_manager_get_can_mount(self)) {
                /* Already mounted at the default boot dir, nothing for us to do */
                if (cbm_is_mounted(boot_dir, NULL)) {
                        goto perform;
                }
                autofree(char) *root_device = NULL;
                autofree(char) *root_base = NULL;
                autofree(char) *abs_bootdir = NULL;

                /* Determine root device */
                root_device = get_boot_device();
                if (!root_device) {
                        fprintf(stderr, "FATAL: Cannot determine boot device\n");
                        return false;
                }

                /* Resolve the actual device */
                root_base = realpath(root_device, NULL);
                if (!root_base) {
                        DECLARE_OOM();
                        return false;
                }

                abs_bootdir = cbm_get_mountpoint_for_device(root_base);

                if (abs_bootdir) {
                        /* User has already mounted the ESP somewhere else, use that */
                        if (!boot_manager_set_boot_dir(self, abs_bootdir)) {
                                fprintf(stderr, "FATAL: Cannot initialise with premounted ESP\n");
                                return false;
                        }
                        /* Successfully using their premounted ESP, go use it */
                        goto perform;
                }

                /* The boot directory isn't mounted, so we'll mount it now */
                if (!nc_file_exists(boot_dir)) {
                        nc_mkdir_p(boot_dir, 0755);
                }
                if (mount(root_base, boot_dir, "vfat", MS_MGC_VAL, "") < 0) {
                        fprintf(stderr,
                                "FATAL: Cannot mount boot device %s on %s: %s\n",
                                root_base,
                                boot_dir,
                                strerror(errno));
                        return false;
                }
                did_mount = true;
        }

perform:
        /* Do a native update */
        ret = boot_manager_update_native(self);

        /* Cleanup and umount */
        if (did_mount) {
                if (umount(boot_dir) < 0) {
                        fprintf(stderr, "WARNING: Could not unmount boot directory\n");
                }
        }

        /* Done */
        return ret;
}

/**
 * Update the target with logical view of an image creation
 *
 * Quite simply, we install all potential kernels to the specified boot
 * directory, and ensure it's equiped with a boot loader. The kernel with
 * the highest release number is selected as the default kernel.
 *
 * This method assumes the boot partition is *already mounted* at the target,
 * therefore it is an _error_ for the target to not exist. No attempt is
 * made to determine the running kernel or to mount a boot partition.
 */
static bool boot_manager_update_image(BootManager *self)
{
        assert(self != NULL);
        autofree(KernelArray) *kernels = NULL;
        autofree(char) *boot_dir = NULL;
        const Kernel *default_kernel = NULL;

        /* Grab the available kernels */
        kernels = boot_manager_get_kernels(self);
        if (!kernels || kernels->len == 0) {
                fprintf(stderr, "No kernels discovered in %s, bailing\n", self->kernel_dir);
                return false;
        }

        /* Grab boot dir */
        boot_dir = boot_manager_get_boot_dir(self);
        if (!boot_dir) {
                DECLARE_OOM();
                return false;
        }

        /* If it doesn't exist this is a user error */
        if (!nc_file_exists(boot_dir)) {
                fprintf(stderr, "Cannot find boot directory, ensure it is mounted: %s\n", boot_dir);
                return false;
        }

        /* Sort them to find the newest kernel */
        nc_array_qsort(kernels, kernel_compare_reverse);

        if (boot_manager_needs_install(self)) {
                /* Attempt install of the bootloader */
                int flags = BOOTLOADER_OPERATION_INSTALL | BOOTLOADER_OPERATION_NO_CHECK;
                if (!boot_manager_modify_bootloader(self, flags)) {
                        fprintf(stderr, "Failed to install bootloader\n");
                        return false;
                }
        } else if (boot_manager_needs_update(self)) {
                /* Attempt update of the bootloader */
                int flags = BOOTLOADER_OPERATION_UPDATE | BOOTLOADER_OPERATION_NO_CHECK;
                if (!boot_manager_modify_bootloader(self, flags)) {
                        fprintf(stderr, "Failed to update bootloader\n");
                        return false;
                }
        }

        /* Go ahead and install the kernels */
        for (int i = 0; i < kernels->len; i++) {
                const Kernel *k = nc_array_get(kernels, i);
                /* Already installed, skip it */
                if (boot_manager_is_kernel_installed(self, k)) {
                        continue;
                }
                if (!boot_manager_install_kernel(self, k)) {
                        fprintf(stderr, "Cannot install kernel %s\n", k->path);
                        return false;
                }
        }

        /* Set the default to the highest release kernel */
        default_kernel = nc_array_get(kernels, 0);
        if (!boot_manager_set_default_kernel(self, default_kernel)) {
                fprintf(stderr, "Failed to set the default kernel to: %s\n", default_kernel->path);
                return false;
        }

        /* Everything succeeded */
        return true;
}

/**
 * Update the target with logical view of a native installation
 */
static bool boot_manager_update_native(__attribute__((unused)) BootManager *self)
{
        return false;
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
