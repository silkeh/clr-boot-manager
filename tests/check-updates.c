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

#include "bootman.h"
#include "files.h"
#include "nica/files.h"

#define PLAYGROUND_ROOT TOP_BUILD_DIR "/tests/update_playground"

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
 * Initialise a playground area to assert update behaviour
 */
static BootManager *prepare_playground(PlaygroundConfig *config)
{
        assert(config != NULL);
        BootManager *m = NULL;

        m = boot_manager_new();
        if (!m) {
                return NULL;
        }

        /* Purge last runs */
        if (nc_file_exists(PLAYGROUND_ROOT)) {
                if (!nc_rm_rf(PLAYGROUND_ROOT)) {
                        fprintf(stderr, "Failed to rm_rf: %s\n", strerror(errno));
                        return false;
                }
        }
        /* Now create fresh tree */
        if (!nc_mkdir_p(PLAYGROUND_ROOT, 00755)) {
                fprintf(stderr, "Cannot create playground root: %s\n", strerror(errno));
                return false;
        }

        if (!boot_manager_set_prefix(m, PLAYGROUND_ROOT)) {
                goto fail;
        }

        /* TODO: Insert all the kernels into PLAYGROUND_ROOT */

        boot_manager_set_can_mount(m, false);
        boot_manager_set_image_mode(m, false);
        if (!boot_manager_set_uname(m, config->uts_name)) {
                fprintf(stderr, "Cannot set given uname of %s\n", config->uts_name);
                goto fail;
        }

        return m;
fail:
        boot_manager_free(m);
        return NULL;
}

START_TEST(bootman_image_test_simple)
{
        autofree(BootManager) *m = NULL;

        PlaygroundKernel init_kernels[] = {
                { "4.6.0", "native", 180, true },
                { "4.4.4", "native", 160, false },
                { "4.4.0", "native", 140, false },
        };
        PlaygroundConfig start_conf = { "4.4.4-160.native",
                                        init_kernels,
                                        ARRAY_SIZE(init_kernels) };

        m = prepare_playground(&start_conf);
        fail_if(!m, "Fatal: Cannot initialise playground");
}
END_TEST

static Suite *core_suite(void)
{
        Suite *s = NULL;
        TCase *tc = NULL;

        s = suite_create("bootman_update");
        tc = tcase_create("bootman_update_functions");
        tcase_add_test(tc, bootman_image_test_simple);
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
