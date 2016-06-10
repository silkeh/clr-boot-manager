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

#define PLAYGROUND_ROOT TOP_BUILD_DIR "/tests/update_playground"

/**
 * 64-bit vs 32-bit test
 */
#if UINTPTR_MAX == 0xffffffffffffffff
#define EFI_STUB_SUFFIX "X64.EFI"
#define EFI_STUB_SUFFIX_L "x64.efi"
#else
#define EFI_STUB_SUFFIX "IA32.EFI"
#define EFI_STUB_SUFFIX_L "ia32.efi"
#endif

/**
 * i.e. $root/boot
 */
#define BOOT_FULL PLAYGROUND_ROOT "/" BOOT_DIRECTORY

#define EFI_START BOOT_FULL "/EFI"
/**
 * i.e. $dir/EFI/Boot/BOOTX64.EFI
 */
#define EFI_STUB_MAIN BOOT_FULL "/EFI/Boot/BOOT" EFI_STUB_SUFFIX

/**
 * Places that need to exist..
 */

/**
 * Systemd support
 */
#if defined(HAVE_SYSTEMD_BOOT)
#define ESP_BOOT_DIR EFI_START "/systemd"
#define ESP_BOOT_STUB ESP_BOOT_DIR "/systemd-boot" EFI_STUB_SUFFIX_L

#define BOOT_COPY_SOURCE "/usr/lib/systemd/boot/efi/systemd-boot" EFI_STUB_SUFFIX_L
#define BOOT_COPY_TARGET PLAYGROUND_ROOT "/usr/lib/systemd/boot/efi/systemd-boot" EFI_STUB_SUFFIX_L
#define BOOT_COPY_DIR PLAYGROUND_ROOT "/usr/lib/systemd/boot/efi"

/**
 * gummiboot support
 */
#elif defined(HAVE_GUMMIBOOT)
#define ESP_BOOT_DIR EFI_START "/gummiboot"
#define ESP_BOOT_STUB ESP_BOOT_DIR "/gummiboot" EFI_STUB_SUFFIX_L

#define BOOT_COPY_SOURCE "/usr/lib/gummiboot/gummiboot" EFI_STUB_SUFFIX_L
#define BOOT_COPY_TARGET PLAYGROUND_ROOT "/usr/lib/gummiboot/gummiboot" EFI_STUB_SUFFIX_L
#define BOOT_COPY_DIR PLAYGROUND_ROOT "/usr/lib/gummiboot"

/**
 * goofiboot support
 */
#elif defined(HAVE_GOOFIBOOT)
#define ESP_BOOT_DIR EFI_START "/goofiboot"
#define ESP_BOOT_STUB ESP_BOOT_DIR "/goofiboot" EFI_STUB_SUFFIX_L
#define BOOT_COPY_SOURCE "/usr/lib/goofiboot/goofiboot" EFI_STUB_SUFFIX_L
#define BOOT_COPY_TARGET PLAYGROUND_ROOT "/usr/lib/goofiboot/goofiboot" EFI_STUB_SUFFIX_L
#define BOOT_COPY_DIR PLAYGROUND_ROOT "/usr/lib/goofiboot"
#else
#error No known ESP loader
#endif

/**
 * Wrap nc_file_exists and spam to stderr
 */
__cbm_inline__ static inline bool noisy_file_exists(const char *p)
{
        bool b = nc_file_exists(p);
        if (b) {
                return b;
        }
        fprintf(stderr, "missing-file: %s does not exist\n", p);
        return b;
}

void confirm_bootloader(void)
{
        fail_if(!noisy_file_exists(EFI_STUB_MAIN), "Main EFI stub missing");
        fail_if(!noisy_file_exists(ESP_BOOT_DIR), "ESP target directory missing");
        fail_if(!noisy_file_exists(ESP_BOOT_STUB), "ESP target stub missing");
}

/**
 * Initialise a playground area to assert update behaviour
 */
BootManager *prepare_playground(PlaygroundConfig *config)
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

        /* Copy the bootloader bits into the tree */
        if (!nc_mkdir_p(BOOT_COPY_DIR, 00755)) {
                goto fail;
        }
        if (!copy_file(BOOT_COPY_SOURCE, BOOT_COPY_TARGET, 00644)) {
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
