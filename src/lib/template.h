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
#include <stdio.h>

#include "nica/util.h"

/**
 * MstTemplateParser
 *
 * Fed a template, this parser will compile a series of contexts to be
 * used later in a render loop
 */
typedef struct MstTemplateParser MstTemplateParser;

/**
 * MstTemplateContext
 *
 * The context must be populated with key->value mappings to enable the logic
 * and content for the main render function.
 */
typedef struct MstTemplateContext MstTemplateContext;

/**
 * Construct a new MstTemplateParser
 */
MstTemplateParser *mst_template_parser_new(void);

/**
 * Free a previously allocated MstTemplateParser and any compiled resources
 */
void mst_template_parser_free(MstTemplateParser *parser);

/**
 * Compile this template parser from the given @buffer of size @length
 */
bool mst_template_parser_load(MstTemplateParser *parser, const char *buffer, ssize_t length);

/**
 * Render the current parser to the given output stream
 */
bool mst_template_parser_render(MstTemplateParser *parser, MstTemplateContext *context,
                                FILE *stream);

/**
 * Construct a new root template context
 */
MstTemplateContext *mst_template_context_new(void);

/**
 * Add a key->value mapping to the context with the string @value
 */
bool mst_template_context_add_string(MstTemplateContext *context, const char *key, char *value);

/**
 * Add a key->value mapping to the context with the boolean @value
 */
bool mst_template_context_add_bool(MstTemplateContext *context, const char *key, bool value);

/**
 * Add a key->value mapping to the context with a TemplateContext child @value
 */
bool mst_template_context_add_child(MstTemplateContext *context, const char *key,
                                    MstTemplateContext *child);

/**
 * Add an item to list with the name of @key.
 * If the list does not already exist, it will be created. Each node in the list must be
 * a full MstTemplateContext, which will be the root context in each iteration
 */
bool mst_template_context_add_list(MstTemplateContext *context, const char *key,
                                   MstTemplateContext *node);

/**
 * Free a previously allocated template context and any associated resources
 */
void mst_template_context_free(MstTemplateContext *context);

/**
 * Convenience macros
 */
DEF_AUTOFREE(MstTemplateParser, mst_template_parser_free)
DEF_AUTOFREE(MstTemplateContext, mst_template_context_free)
