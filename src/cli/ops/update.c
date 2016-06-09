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
        NcArray *removals = NULL;

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
                Kernel *running = NULL;
                autofree(NcHashmap) *mapped_kernels = NULL;
                NcHashmapIter map_iter = { 0 };
                const char *kernel_type = NULL;
                KernelArray *typed_kernels = NULL;
                Kernel *new_default = NULL;

                nc_array_qsort(avail_kernels, kernel_compare_reverse);

                /** Map kernels to type */
                mapped_kernels = boot_manager_map_kernels(manager, avail_kernels);
                if (!mapped_kernels || nc_hashmap_size(mapped_kernels) == 0) {
                        goto cleanup;
                }

                /* Determine the running kernel */
                running = boot_manager_get_running_kernel(manager, avail_kernels);
                if (!running) {
                        FATAL("Cannot dermine the currently running kernel");
                        goto cleanup;
                }

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

                /* This is mostly to allow a repair-situation */
                if (running && !boot_manager_is_kernel_installed(manager, running)) {
                        /* Not necessarily fatal. */
                        if (!boot_manager_install_kernel(manager, running)) {
                                fprintf(stderr, "Failed to repair running kernel\n");
                        }
                }

                nc_hashmap_iter_init(mapped_kernels, &map_iter);
                while (nc_hashmap_iter_next(&map_iter,
                                            (void **)&kernel_type,
                                            (void **)&typed_kernels)) {
                        Kernel *tip = NULL;
                        Kernel *last_good = NULL;

                        /* Sort this kernel set highest to lowest */
                        nc_array_qsort(typed_kernels, kernel_compare_reverse);

                        /* Get the default kernel selection */
                        tip =
                            boot_manager_get_default_for_type(manager, typed_kernels, kernel_type);
                        if (!tip) {
                                /* Fallback to highest release number */
                                tip = nc_array_get(avail_kernels, 0);
                        }

                        /* Ensure this tip kernel is installed */
                        if (!boot_manager_is_kernel_installed(manager, tip)) {
                                if (!boot_manager_install_kernel(manager, tip)) {
                                        fprintf(stderr,
                                                "Failed to install kernel: %s\n",
                                                tip->path);
                                        goto cleanup;
                                }
                        }

                        /* Last known booting kernel, might be null. */
                        last_good = boot_manager_get_last_booted(manager, typed_kernels);

                        /* Ensure this guy is still installed/repaired */
                        if (last_good && !boot_manager_is_kernel_installed(manager, last_good)) {
                                if (!boot_manager_install_kernel(manager, tip)) {
                                        fprintf(stderr,
                                                "Failed to install kernel: %s\n",
                                                tip->path);
                                        goto cleanup;
                                }
                        }

                        for (int i = 0; i < typed_kernels->len; i++) {
                                Kernel *tk = nc_array_get(typed_kernels, i);
                                /* Preserve running kernel */
                                if (running && tk == running) {
                                        continue;
                                }
                                /* Preserve tip */
                                if (tk && tk == tip) {
                                        continue;
                                }
                                /* Preserve last running */
                                if (last_good && tk == last_good) {
                                        continue;
                                }
                                if (!removals) {
                                        removals = nc_array_new();
                                }
                                /* Don't try to remove a non installed kernel */
                                if (!boot_manager_is_kernel_installed(manager, tk)) {
                                        continue;
                                }
                                /* Schedule removal of kernel */
                                if (!nc_array_add(removals, tk)) {
                                        DECLARE_OOM();
                                        goto cleanup;
                                }
                        }
                }

                /* Might return NULL */
                new_default =
                    boot_manager_get_default_for_type(manager, avail_kernels, running->ktype);
                if (!boot_manager_set_default_kernel(manager, new_default)) {
                        fprintf(stderr,
                                "Failed to set the default kernel to: %s\n",
                                new_default ? new_default->path : "<timeout mode>");
                        goto cleanup;
                }

                ret = true;

                if (!removals) {
                        /* We're done. */
                        goto cleanup;
                }

                /* Now remove the older kernels */
                for (int i = 0; i < removals->len; i++) {
                        Kernel *k = nc_array_get(removals, i);
                        if (!boot_manager_remove_kernel(manager, k)) {
                                fprintf(stderr, "Failed to remove kernel: %s\n", k->path);
                                ret = false;
                                goto cleanup;
                        }
                }
        }
cleanup:
        cbm_sync();
        if (removals) {
                nc_array_free(&removals, NULL);
        }

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
