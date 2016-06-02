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

#include <errno.h>
#include <getopt.h>
#include <string.h>
#include <sys/mount.h>
#include <unistd.h>

#include "bootman.h"
#include "cli.h"
#include "files.h"
#include "nica/files.h"
#include "util.h"

static int kernel_compare_reverse(const void *a, const void *b)
{
        const Kernel *ka = *(const Kernel **)a;
        const Kernel *kb = *(const Kernel **)b;

        if (ka->release > kb->release) {
                return -1;
        }
        return 1;
}

bool cbm_command_update(int argc, char **argv)
{
        autofree(char) *root = NULL;
        autofree(BootManager) *manager = NULL;
        autofree(KernelArray) *avail_kernels = NULL;
        autofree(char) *boot_dir = NULL;
        autofree(char) *root_device = NULL;
        autofree(char) *root_base = NULL;
        bool did_mount = false;
        Kernel *candidate = NULL;
        bool ret = false;
        autofree(char) *abs_bootdir = NULL;

        cli_default_args_init(&argc, &argv, &root);

        manager = boot_manager_new();
        if (!manager) {
                DECLARE_OOM();
                return false;
        }

        /* Use specified root if required */
        if (root && !boot_manager_set_prefix(manager, root)) {
                return false;
        }

        boot_dir = boot_manager_get_boot_dir(manager);
        if (!boot_dir) {
                DECLARE_OOM();
                return false;
        }

        avail_kernels = boot_manager_get_kernels(manager);
        if (!avail_kernels || avail_kernels->len < 1) {
                fprintf(stderr, "No available kernels, not continuing\n");
                return false;
        }

        /* Image mode */
        if (boot_manager_is_image_mode(manager)) {
                /* Highest release number wins */
                for (int i = 0; i < avail_kernels->len; i++) {
                        Kernel *cur = nc_array_get((NcArray *)avail_kernels, i);
                        if (!candidate) {
                                candidate = cur;
                                continue;
                        }
                        if (cur->release > candidate->release) {
                                candidate = cur;
                        }
                }

                if (!nc_file_exists(boot_dir)) {
                        nc_mkdir_p(boot_dir, 0755);
                }

                /* Get the bootloader in order (NOCHECK) to prevent second hash checks */
                if (boot_manager_needs_install(manager)) {
                        if (!boot_manager_modify_bootloader(manager,
                                                            BOOTLOADER_OPERATION_INSTALL |
                                                                BOOTLOADER_OPERATION_NO_CHECK)) {
                                fprintf(stderr, "Failed to install bootloader\n");
                                goto cleanup;
                        }
                } else if (boot_manager_needs_update(manager)) {
                        if (!boot_manager_modify_bootloader(manager,
                                                            BOOTLOADER_OPERATION_UPDATE |
                                                                BOOTLOADER_OPERATION_NO_CHECK)) {
                                fprintf(stderr, "Failed to update bootloader\n");
                                goto cleanup;
                        }
                }

                /* Install all the kernels */
                for (int i = 0; i < avail_kernels->len; i++) {
                        Kernel *k = nc_array_get((NcArray *)avail_kernels, i);
                        if (!boot_manager_install_kernel(manager, k)) {
                                fprintf(stderr, "Failed to install kernel %s\n", k->path);
                                goto cleanup;
                        }
                }

                /* Now set the default, and we're all done */
                if (!boot_manager_set_default_kernel(manager, candidate)) {
                        fprintf(stderr, "Failed to set default kernel\n");
                        goto cleanup;
                }
                ret = true;
        } else {
                Kernel *tip = NULL;
                Kernel *running = NULL;
                Kernel *last_good = NULL;

                nc_array_qsort(avail_kernels, kernel_compare_reverse);

                /* Native install mode */
                if (!cbm_is_mounted(boot_dir, NULL)) {
                        root_device = get_boot_device();
                        if (!root_device) {
                                fprintf(stderr, "FATAL: Cannot determine boot device\n");
                                return false;
                        }
                        root_base = realpath(root_device, NULL);
                        if (!root_base) {
                                DECLARE_OOM();
                                return false;
                        }

                        abs_bootdir = cbm_get_mountpoint_for_device(root_base);
                        if (abs_bootdir) {
                                /* User has already mounted the ESP somewhere else, use that */
                                if (!boot_manager_set_boot_dir(manager, abs_bootdir)) {
                                        fprintf(stderr,
                                                "FATAL: Cannot initialise with premounted ESP\n");
                                        return false;
                                }
                        } else {
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
                }

                /* Get the bootloader in order (NOCHECK) to prevent second hash checks */
                if (boot_manager_needs_install(manager)) {
                        if (!boot_manager_modify_bootloader(manager,
                                                            BOOTLOADER_OPERATION_INSTALL |
                                                                BOOTLOADER_OPERATION_NO_CHECK)) {
                                fprintf(stderr, "Failed to install bootloader\n");
                                goto cleanup;
                        }
                } else if (boot_manager_needs_update(manager)) {
                        if (!boot_manager_modify_bootloader(manager,
                                                            BOOTLOADER_OPERATION_UPDATE |
                                                                BOOTLOADER_OPERATION_NO_CHECK)) {
                                fprintf(stderr, "Failed to update bootloader\n");
                                goto cleanup;
                        }
                }

                /* Yes, it's ugly, but we need to determine the running one first. */
                for (int i = 0; i < avail_kernels->len; i++) {
                        Kernel *k = nc_array_get((NcArray *)avail_kernels, i);
                        /* Determine the running kernel. */
                        if (k->is_running) {
                                running = k;
                                break;
                        }
                }

                /* Only ever install the *newest* kernel */
                tip = nc_array_get(avail_kernels, 0);

                /* This is mostly to allow a repair-situation */
                if (running && !boot_manager_is_kernel_installed(manager, running)) {
                        /* Not necessarily fatal. */
                        if (!boot_manager_install_kernel(manager, running)) {
                                fprintf(stderr, "Failed to repair running kernel\n");
                        }
                }

                if (boot_manager_is_kernel_installed(manager, tip)) {
                        ret = true;
                        goto cleanup;
                }

                if (!running) {
                        FATAL("Cannot dermine the currently running kernel");
                        goto cleanup;
                }
                /* Attempt to keep the last booted kernel of the same type */
                for (int i = 0; i < avail_kernels->len; i++) {
                        Kernel *k = nc_array_get(avail_kernels, i);
                        if (k != running && k->release < running->release &&
                            k->type == running->type && k->boots) {
                                last_good = k;
                                break;
                        }
                }
                if (!last_good) {
                        /* Fallback to the last good kernel even though type didn't match*/
                        for (int i = 0; i < avail_kernels->len; i++) {
                                Kernel *k = nc_array_get(avail_kernels, i);
                                if (k != running && k->release < running->release && k->boots) {
                                        last_good = k;
                                        break;
                                }
                        }
                }

                if (!boot_manager_install_kernel(manager, tip)) {
                        fprintf(stderr, "Failed to install kernel: %s\n", tip->path);
                        goto cleanup;
                }
                if (!boot_manager_set_default_kernel(manager, tip)) {
                        fprintf(stderr, "Failed to set the default kernel to: %s\n", tip->path);
                        goto cleanup;
                }

                if (avail_kernels->len < 3) {
                        /* We're done. */
                        goto cleanup;
                }

                /* Now remove the older kernels */
                for (int i = 0; i < avail_kernels->len; i++) {
                        Kernel *k = nc_array_get(avail_kernels, i);
                        if (k != tip && k != running && k != last_good) {
                                if (!boot_manager_remove_kernel(manager, k)) {
                                        fprintf(stderr, "Failed to remove kernel: %s\n", k->path);
                                        goto cleanup;
                                }
                        }
                }
                ret = true;
        }
cleanup:
        cbm_sync();

        if (did_mount) {
                if (umount(boot_dir) < 0) {
                        fprintf(stderr, "WARNING: Could not unmount boot directory\n");
                }
        }

        return ret;
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
