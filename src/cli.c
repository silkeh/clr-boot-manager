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

#define _GNU_SOURCE

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli.h"

static struct option default_opts[] = { { "path", required_argument, 0, 'p' }, { 0, 0, 0, 0 } };

void cli_default_args_init(int *argc, char ***argv, char **root)
{
        int o_in = 0;
        char c;
        char *_root = NULL;

        /* We actually want to use getopt, so rewind one for getopt */;
        --(*argv);
        ++(*argc);

        if (!root) {
                return;
        }

        /* Allow setting the root */
        while (true) {
                c = getopt_long(*argc, *argv, "p:", default_opts, &o_in);
                if (c == -1) {
                        break;
                }
                switch (c) {
                case 0:
                case 'p':
                        if (optarg) {
                                if (_root) {
                                        free(_root);
                                        _root = NULL;
                                }
                                _root = strdup(optarg);
                        }
                        break;
                case '?':
                        break;
                default:
                        abort();
                }
        }
        *argc -= optind;

        if (_root) {
                *root = _root;
        }
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
