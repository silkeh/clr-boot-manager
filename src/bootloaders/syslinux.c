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

#include "bootloader.h"
#include "util.h"

static bool syslinux_init(__cbm_unused__ const BootManager *manager)
{
        return false;
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

static bool syslinux_install(__cbm_unused__ const BootManager *manager)
{
        return false;
}

static bool syslinux_update(__cbm_unused__ const BootManager *manager)
{
        return false;
}

static bool syslinux_remove(__cbm_unused__ const BootManager *manager)
{
        return false;
}

static void syslinux_destroy(__cbm_unused__ const BootManager *manager)
{
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
