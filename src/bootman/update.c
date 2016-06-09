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

#include "bootman.h"
#include "bootman_private.h"

static bool boot_manager_update_image(BootManager *self);
static bool boot_manager_update_native(BootManager *self);

bool boot_manager_update(BootManager *self)
{
        assert(self != NULL);
        bool ret = false;

        /* TODO: Insert prep code here */

        /* Perform the main operation */
        if (boot_manager_is_image_mode(self)) {
                ret = boot_manager_update_image(self);
        } else {
                ret = boot_manager_update_native(self);
        }

        /* TODO: Insert cleanup code here */
        return ret;
}

/**
 * Update the target with logical view of an image creation
 */
static bool boot_manager_update_image(__attribute__((unused)) BootManager *self)
{
        return false;
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
