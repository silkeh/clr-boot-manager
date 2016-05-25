/*
 * This file is part of clr-boot-manager.
 *
 * Copyright (C) 2016 Intel Corporation
 *
 * clr-boot-manager is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 */

#pragma once

#define _GNU_SOURCE

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Implemented in a *similar* fashion to how g_autoptr is intended to
 * work in future, but without the concerns of MSVC, etc..
 */
#define DEF_AUTOFREE(N, C)                                                                         \
        static inline void _autofree_func_##N(void *p)                                             \
        {                                                                                          \
                if (p && *(N **)p) {                                                               \
                        C(*(N **)p);                                                               \
                        (*(void **)p) = NULL;                                                      \
                }                                                                                  \
        }

#define autofree(N) __attribute__((cleanup(_autofree_func_##N))) N

#define streq(x, y) strcmp(x, y) == 0 ? true : false

/** Revisit in future */
#define LOG(...) fprintf(stderr, __VA_ARGS__)

#define FATAL(...)                                                                                 \
        do {                                                                                       \
                fprintf(stderr, "%s()[%d]: %s\n", __func__, __LINE__, __VA_ARGS__);                \
        } while (0);

#define DECLARE_OOM() FATAL("Out Of Memory")

#define OOM_CHECK(x)                                                                               \
        {                                                                                          \
                if (!x) {                                                                          \
                        DECLARE_OOM();                                                             \
                        abort();                                                                   \
                }                                                                                  \
        }
#define OOM_CHECK_RET(x, y)                                                                        \
        {                                                                                          \
                if (!x) {                                                                          \
                        DECLARE_OOM();                                                             \
                        return y;                                                                  \
                }                                                                                  \
        }

DEF_AUTOFREE(char, free)
DEF_AUTOFREE(FILE, fclose)

/**
 * Dump any leaked file descriptors
 */
void dump_file_descriptor_leaks(void);

void *greedy_realloc(void **p, size_t *allocated, size_t need);

/**
 * Determine whether the system is booted using UEFI
 */
bool cbm_system_has_uefi(void);

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
