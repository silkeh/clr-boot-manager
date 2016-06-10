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

#pragma once

#include <stdbool.h>
#include <stdlib.h>

#include "bootman.h"

/**
 * Needed for intiialisation
 */
typedef struct PlaygroundKernel {
        const char *version;
        const char *ktype;
        int release;
        bool default_for_type;
} PlaygroundKernel;

/**
 * Playground initialisation
 */
typedef struct PlaygroundConfig {
        const char *uts_name;
        PlaygroundKernel *initial_kernels;
        size_t n_kernels;
} PlaygroundConfig;

/**
 * Return a new BootManager for the newly prepared playground.
 * Will return null if initialisation failed
 */
BootManager *prepare_playground(PlaygroundConfig *config);

/**
 * Push a new kernel into the root
 */
bool push_kernel_update(PlaygroundKernel *kernel);

/**
 * Set the current kernel as the default for it's type, overriding any
 * previous configuration.
 */
bool set_kernel_default(PlaygroundKernel *kernel);

/**
 * Mark the kernel as having booted
 */
bool set_kernel_booted(PlaygroundKernel *kernel, bool did_boot);

/**
 * Util - confirm the bootloader is installed in the current test
 */
void confirm_bootloader(void);

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
