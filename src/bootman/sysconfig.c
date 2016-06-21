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

#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include "bootman.h"
#include "bootman_private.h"
#include "files.h"

void cbm_free_sysconfig(SystemConfig *config)
{
        if (!config) {
                return;
        }
        free(config->prefix);
        free(config->boot_device);
        free(config->root_uuid);
        free(config);
}

SystemConfig *cbm_inspect_root(const char *path)
{
        if (!path) {
                return NULL;
        }
        SystemConfig *c = NULL;
        char *realp = NULL;
        char *boot = NULL;

        realp = realpath(path, NULL);
        if (!realp) {
                LOG("Path specified does not exist: %s\n", path);
                return NULL;
        }

        c = calloc(1, sizeof(struct SystemConfig));
        if (!c) {
                DECLARE_OOM();
                free(realp);
                return NULL;
        }
        c->prefix = realp;

        /* TODO: Disable logging */
        if (geteuid() == 0) {
                /* Find legacy relative to root */
                boot = get_legacy_boot_device(realp);
                if (boot) {
                        c->boot_device = boot;
                        c->legacy = true;
                } else {
                        c->boot_device = get_boot_device();
                }
                c->root_uuid = get_part_uuid(realp);
        }

        return c;
}

bool cbm_is_sysconfig_sane(SystemConfig *config)
{
        if (!config) {
                LOG("sysconfig insane: Missing config\n");
                return false;
        }
        if (!config->root_uuid) {
                LOG("sysconfig insane: Missing root_uuid\n");
                return false;
        }
        return true;
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
