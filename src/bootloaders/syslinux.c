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
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "bootloader.h"
#include "files.h"
#include "nica/files.h"
#include "util.h"

#define CBM_MBR_SYSLINUX_SIZE 440

static char *extlinux_cmd = NULL;
static char *base_path = NULL;

static bool syslinux_init(const BootManager *manager)
{
        autofree(char) *ldlinux = NULL;
        int ret = 0;

        if (base_path) {
                free(base_path);
                base_path = NULL;
        }
        base_path = boot_manager_get_boot_dir((BootManager *)manager);
        OOM_CHECK_RET(base_path, false);

        if (extlinux_cmd) {
                free(extlinux_cmd);
                extlinux_cmd = NULL;
        }
        if (asprintf(&ldlinux, "%s/ldlinux.sys", base_path) < 0) {
                DECLARE_OOM();
                abort();
        }
        if (nc_file_exists(ldlinux)) {
                ret = asprintf(&extlinux_cmd, "extlinux -U %s &> /dev/null", base_path);
        } else {
                ret = asprintf(&extlinux_cmd, "extlinux -i %s &> /dev/null", base_path);
        }
        if (ret < 0) {
                DECLARE_OOM();
                abort();
        }

        return true;
}

static bool syslinux_install_kernel(__cbm_unused__ const BootManager *manager,
                                    __cbm_unused__ const Kernel *kernel)
{
        return false;
}

static bool syslinux_is_kernel_installed(__cbm_unused__ const BootManager *manager,
                                         __cbm_unused__ const Kernel *kernel)
{
        return false;
}

static bool syslinux_remove_kernel(__cbm_unused__ const BootManager *manager,
                                   __cbm_unused__ const Kernel *kernel)
{
        return false;
}

static bool syslinux_set_default_kernel(__cbm_unused__ const BootManager *manager,
                                        __cbm_unused__ const Kernel *kernel)
{
        return false;
}

static bool syslinux_needs_update(__cbm_unused__ const BootManager *manager)
{
        return true;
}

static bool syslinux_needs_install(__cbm_unused__ const BootManager *manager)
{
        return true;
}

static bool syslinux_install(const BootManager *manager)
{
        autofree(char) *boot_device = NULL;
        autofree(char) *syslinux_path = NULL;
        const char *prefix = NULL;
        int mbr = -1;
        int syslinux_mbr = -1;
        ssize_t count = 0;

        prefix = boot_manager_get_prefix((BootManager *)manager);
        boot_device = get_parent_disk((char *)prefix);
        mbr = open(boot_device, O_WRONLY);
        if (mbr < 0) {
                return false;
        }

        if (asprintf(&syslinux_path, "%s/usr/share/syslinux/gptmbr.bin", prefix) < 0) {
                DECLARE_OOM();
                abort();
        }
        syslinux_mbr = open(syslinux_path, O_RDONLY);
        if (syslinux_mbr < 0) {
                close(mbr);
                return false;
        }

        count = sendfile(mbr, syslinux_mbr, NULL, CBM_MBR_SYSLINUX_SIZE);
        if (count != CBM_MBR_SYSLINUX_SIZE) {
                close(mbr);
                close(syslinux_mbr);
                return false;
        }
        close(mbr);
        close(syslinux_mbr);

        if (system(extlinux_cmd) != 0) {
                return false;
        }

        cbm_sync();
        return true;
}

static bool syslinux_update(const BootManager *manager)
{
        return syslinux_install(manager);
}

static bool syslinux_remove(__cbm_unused__ const BootManager *manager)
{
        /* Maybe should return false? Unsure */
        return true;
}

static void syslinux_destroy(__cbm_unused__ const BootManager *manager)
{
        if (extlinux_cmd) {
                free(extlinux_cmd);
                extlinux_cmd = NULL;
        }
        if (base_path) {
                free(base_path);
                base_path = NULL;
        }
}

__cbm_export__ const BootLoader syslinux_bootloader = {.name = "syslinux",
                                                       .init = syslinux_init,
                                                       .install_kernel = syslinux_install_kernel,
                                                       .is_kernel_installed =
                                                           syslinux_is_kernel_installed,
                                                       .remove_kernel = syslinux_remove_kernel,
                                                       .set_default_kernel =
                                                           syslinux_set_default_kernel,
                                                       .needs_install = syslinux_needs_install,
                                                       .needs_update = syslinux_needs_update,
                                                       .install = syslinux_install,
                                                       .update = syslinux_update,
                                                       .remove = syslinux_remove,
                                                       .destroy = syslinux_destroy };

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
