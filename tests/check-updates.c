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
#include <check.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "bootman.h"
#include "files.h"
#include "nica/files.h"

#include "config.h"
#include "harness.h"

START_TEST(bootman_image_test_simple)
{
        autofree(BootManager) *m = NULL;

        PlaygroundKernel init_kernels[] = {
                { "4.6.0", "native", 180, true },
                { "4.4.4", "native", 160, false },
                { "4.4.0", "native", 140, false },
        };
        PlaygroundConfig start_conf = { NULL, init_kernels, ARRAY_SIZE(init_kernels) };

        m = prepare_playground(&start_conf);
        fail_if(!m, "Fatal: Cannot initialise playground");
        boot_manager_set_image_mode(m, true);

        fail_if(!boot_manager_update(m), "Failed to update in image mode");

        confirm_bootloader();
}
END_TEST

/**
 * Identical to bootman_image_test_simple, but in native mode
 */
START_TEST(bootman_native_test_simple)
{
        autofree(BootManager) *m = NULL;

        PlaygroundKernel init_kernels[] = {
                { "4.6.0", "native", 180, true },
                { "4.4.4", "native", 160, false },
                { "4.4.0", "native", 140, false },
        };
        PlaygroundConfig start_conf = { "4.6.0-160.native",
                                        init_kernels,
                                        ARRAY_SIZE(init_kernels) };

        m = prepare_playground(&start_conf);
        fail_if(!m, "Fatal: Cannot initialise playground");
        boot_manager_set_image_mode(m, false);

        fail_if(!boot_manager_update(m), "Failed to update in native mode");

        confirm_bootloader();
}
END_TEST

static Suite *core_suite(void)
{
        Suite *s = NULL;
        TCase *tc = NULL;

        s = suite_create("bootman_update");
        tc = tcase_create("bootman_update_functions");
        tcase_add_test(tc, bootman_image_test_simple);
        tcase_add_test(tc, bootman_native_test_simple);
        suite_add_tcase(s, tc);

        return s;
}

int main(void)
{
        Suite *s;
        SRunner *sr;
        int fail;

        /* syncing can be problematic during test suite runs */
        cbm_set_sync_filesystems(false);

        s = core_suite();
        sr = srunner_create(s);
        srunner_run_all(sr, CK_VERBOSE);
        fail = srunner_ntests_failed(sr);
        srunner_free(sr);

        if (fail > 0) {
                return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
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
