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
        /* $moduledir/$i */
        const char *module_dirs[] = { "build", "source", "extra",   "kernel", "updates",
                                      "arch",  "crypto", "drivers", "fs",     "lib",
                                      "mm",    "net",    "sound" };
        /* $moduledir/kernel/$i */
        const char *module_modules[] = { "arch/dummy.ko", "crypto/dummy.ko", "drivers/dummy.ko",
                                         "fs/dummy.ko",   "lib/dummy.ko",    "mm/dummy.ko",
                                         "net/dummy.ko",  "sound/dummy.ko" };

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

        /* Construct the root kernels directory */
        if (!nc_mkdir_p(PLAYGROUND_ROOT "/" KERNEL_DIRECTORY, 00755)) {
                goto fail;
        }
        /* Construct the root kernel modules directory */
        if (!nc_mkdir_p(PLAYGROUND_ROOT "/" KERNEL_MODULES_DIRECTORY, 00755)) {
                goto fail;
        }

        if (!nc_mkdir_p(PLAYGROUND_ROOT "/" BOOT_DIRECTORY, 00755)) {
                goto fail;
        }

        /* TODO: Insert all the kernels into PLAYGROUND_ROOT */
        for (size_t i = 0; i < config->n_kernels; i++) {
                PlaygroundKernel *k = &(config->initial_kernels[i]);
                autofree(char) *kfile = NULL;
                autofree(char) *cmdfile = NULL;
                autofree(char) *conffile = NULL;
                autofree(char) *link_source = NULL;
                autofree(char) *link_target = NULL;

                /* $root/$kerneldir/$prefix.native.4.2.1-137 */
                if (!asprintf(&kfile,
                              "%s/%s/%s.%s.%s-%d",
                              PLAYGROUND_ROOT,
                              KERNEL_DIRECTORY,
                              KERNEL_NAMESPACE,
                              k->ktype,
                              k->version,
                              k->release)) {
                        goto fail;
                }

                /* $root/$kerneldir/cmdline-$version-$release.$type */
                if (!asprintf(&cmdfile,
                              "%s/%s/cmdline-%s-%d.%s",
                              PLAYGROUND_ROOT,
                              KERNEL_DIRECTORY,
                              k->version,
                              k->release,
                              k->ktype)) {
                        goto fail;
                }
                /* $root/$kerneldir/config-$version-$release.$type */
                if (!asprintf(&conffile,
                              "%s/%s/config-%s-%d.%s",
                              PLAYGROUND_ROOT,
                              KERNEL_DIRECTORY,
                              k->version,
                              k->release,
                              k->ktype)) {
                        goto fail;
                }

                /* Write the "kernel blob" */
                if (!file_set_text((const char *)kfile, (char *)k->version)) {
                        goto fail;
                }
                /* Write the "cmdline file" */
                if (!file_set_text((const char *)cmdfile, (char *)k->version)) {
                        goto fail;
                }
                /* Write the "config file" */
                if (!file_set_text((const char *)conffile, (char *)k->version)) {
                        goto fail;
                }

                /* Create all the dirs .. */
                for (size_t i = 0; i < ARRAY_SIZE(module_dirs); i++) {
                        const char *p = module_dirs[i];
                        autofree(char) *t = NULL;

                        /* $root/$moduledir/$version-$rel/$p */
                        if (!asprintf(&t,
                                      "%s/%s/%s-%d/%s",
                                      PLAYGROUND_ROOT,
                                      KERNEL_MODULES_DIRECTORY,
                                      k->version,
                                      k->release,
                                      p)) {
                                goto fail;
                        }
                        if (!nc_mkdir_p(t, 00755)) {
                                fprintf(stderr, "Failed to mkdir: %s %s\n", p, strerror(errno));
                                goto fail;
                        }
                }
                /* Create all the .ko's .. */
                for (size_t i = 0; i < ARRAY_SIZE(module_modules); i++) {
                        const char *p = module_modules[i];
                        autofree(char) *t = NULL;

                        /* $root/$moduledir/$version-$rel/$p */
                        if (!asprintf(&t,
                                      "%s/%s/%s-%d/%s",
                                      PLAYGROUND_ROOT,
                                      KERNEL_MODULES_DIRECTORY,
                                      k->version,
                                      k->release,
                                      p)) {
                                goto fail;
                        }
                        if (!file_set_text((const char *)t, (char *)k->version)) {
                                fprintf(stderr, "Failed to touch: %s %s\n", t, strerror(errno));
                                goto fail;
                        }
                }

                /* Not default so skip */
                if (!k->default_for_type) {
                        continue;
                }

                if (!asprintf(&link_source,
                              "%s.%s.%s-%d",
                              KERNEL_NAMESPACE,
                              k->ktype,
                              k->version,
                              k->release)) {
                        goto fail;
                }

                /* i.e. default-kvm */
                if (!asprintf(&link_target,
                              "%s/%s/default-%s",
                              PLAYGROUND_ROOT,
                              KERNEL_DIRECTORY,
                              k->ktype)) {
                        goto fail;
                }

                if (symlink(link_source, link_target) < 0) {
                        fprintf(stderr, "Failed to create default-%s symlink\n", k->ktype);
                        goto fail;
                }
        }

        boot_manager_set_can_mount(m, false);
        boot_manager_set_image_mode(m, false);
        if (config->uts_name && !boot_manager_set_uname(m, config->uts_name)) {
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
        PlaygroundConfig start_conf = { NULL, init_kernels, ARRAY_SIZE(init_kernels) };

        m = prepare_playground(&start_conf);
        fail_if(!m, "Fatal: Cannot initialise playground");
        boot_manager_set_image_mode(m, true);

        fail_if(!boot_manager_update(m), "Failed to update in image mode");
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
