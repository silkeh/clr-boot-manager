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
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nica/hashmap.h"
#include "nica/list.h"
#include "template.h"
#include "util.h"

/**
 * Represents the current node type in the parse context, when a full
 * node has been found.
 */
typedef enum {
        MST_TMPL_NODE_ROOT = 0,        /**<Root node of all contexts */
        MST_TMPL_NODE_SECTION,         /**<Section/conditionals, # start */
        MST_TMPL_NODE_SECTION_END,     /**<End of a section, "/" start */
        MST_TMPL_NODE_INVERSE_SECTION, /**<Begin inverse section, "^" start */
        MST_TMPL_NODE_COMMENT,         /**<Begin comment, "!" start */
        MST_TMPL_NODE_VARIABLE,        /**<Default node type, substitution var */
} MstTemplateNodeType;

typedef struct MstTemplateNode {
        char *tag;       /**<Name of this token */
        size_t tag_len;  /**<Length of this tag */
        char *lead;      /**<Any text between this token and the end/next token */
        size_t lead_len; /**<Length of the lead text */
        char *tail;
        size_t tail_len;
        MstTemplateNodeType ntype;      /**<Type of this particular node */
        struct MstTemplateNode *parent; /**<Parent of this node for context resolution */
        struct MstTemplateNode *next;   /**<Sibling in current node heirarchy */
        struct MstTemplateNode *child;  /**<Child (subcontext) of this current context */
} MstTemplateNode;

/**
 * Instance tracking
 */
struct MstTemplateParser {
        MstTemplateNode *root_node;
};

/**
 * Context data
 */
struct MstTemplateContext {
        NcHashmap *store;
        struct MstTemplateContext *parent;
};

/**
 * Possible types for an MstTemplateValue
 */
typedef enum {
        MST_TMPL_VALUE_MIN = 0,
        MST_TMPL_VALUE_STRING,
        MST_TMPL_VALUE_BOOL,
        MST_TMPL_VALUE_CHILD,
        MST_TMPL_VALUE_LIST,
        MST_TMPL_VALUE_MAX
} MstTemplateValueType;

/**
 * MstTemplateValue wraps various value types for a template context
 */
typedef struct MstTemplateValue {
        MstTemplateValueType vtype; /**<Type for this context variable */
        void *value;                /**<Value for this context variable */
} MstTemplateValue;

/**
 * Construct a new MstTemplateValue
 */
static inline MstTemplateValue *mst_template_value_new(MstTemplateValueType vtype, void *value)
{
        MstTemplateValue *ret = NULL;

        if (vtype < MST_TMPL_VALUE_MIN || vtype >= MST_TMPL_VALUE_MAX) {
                return NULL;
        }

        ret = calloc(1, sizeof(struct MstTemplateValue));
        if (!ret) {
                return NULL;
        }
        ret->vtype = vtype;
        ret->value = value;
        return ret;
}

/**
 * Free a previously allocated MstTemplateValue
 */
static inline void mst_template_value_free(MstTemplateValue *self)
{
        NcList *list, *elem = NULL;

        if (!self) {
                return;
        }
        switch (self->vtype) {
        case MST_TMPL_VALUE_STRING:
                free(self->value);
                break;
        case MST_TMPL_VALUE_CHILD:
                mst_template_context_free((MstTemplateContext *)self->value);
                break;
        case MST_TMPL_VALUE_LIST:
                list = self->value;
                NC_LIST_FOREACH (list, elem) {
                        mst_template_context_free((MstTemplateContext *)elem->data);
                }
                nc_list_free(&list);
                break;
        default:
                break;
        }
        free(self);
}

/**
 * Template context functions
 */
MstTemplateContext *mst_template_context_new(void)
{
        MstTemplateContext *ret = NULL;

        ret = calloc(1, sizeof(struct MstTemplateContext));

        if (!ret) {
                return NULL;
        }
        ret->store = nc_hashmap_new_full(nc_string_hash,
                                         nc_string_compare,
                                         free,
                                         (nc_hash_free_func)mst_template_value_free);
        if (!ret->store) {
                mst_template_context_free(ret);
                return NULL;
        }
        return ret;
}

void mst_template_context_free(MstTemplateContext *self)
{
        if (!self) {
                return;
        }
        nc_hashmap_free(self->store);
        free(self);
}

bool mst_template_context_add_value(MstTemplateContext *self, const char *key,
                                    MstTemplateValueType vtype, void *value)
{
        MstTemplateValue *val = NULL;
        char *key_dup = NULL;

        assert(self != NULL);

        key_dup = strdup(key);

        if (!key_dup) {
                return false;
        }

        val = mst_template_value_new(vtype, value);
        if (!val) {
                return false;
        }
        /* Assign parent to child nodes */
        if (value && vtype == MST_TMPL_VALUE_CHILD) {
                ((MstTemplateContext *)value)->parent = self;
        }
        return nc_hashmap_put(self->store, key_dup, val);
}

bool mst_template_context_add_string(MstTemplateContext *self, const char *key, char *value)
{
        char *dup = strdup(value);

        if (!dup) {
                return false;
        }

        if (!mst_template_context_add_value(self, key, MST_TMPL_VALUE_STRING, dup)) {
                free(dup);
                return false;
        }
        return true;
}

bool mst_template_context_add_bool(MstTemplateContext *self, const char *key, bool value)
{
        return mst_template_context_add_value(self, key, MST_TMPL_VALUE_BOOL, NC_HASH_VALUE(value));
}

bool mst_template_context_add_child(MstTemplateContext *self, const char *key,
                                    MstTemplateContext *child)
{
        if (!child) {
                return false;
        }
        return mst_template_context_add_value(self, key, MST_TMPL_VALUE_CHILD, child);
}

bool mst_template_context_add_list(MstTemplateContext *self, const char *key,
                                   MstTemplateContext *node)
{
        NcList *list = NULL;
        MstTemplateValue *value = NULL;

        assert(self != NULL);

        value = nc_hashmap_get(self->store, key);
        if (value) {
                if (value->vtype != MST_TMPL_VALUE_LIST) {
                        fprintf(stderr, "Attempted to add list to type that isn't a list.\n");
                        return false;
                }
                list = value->value;
                if (!nc_list_append(&list, node)) {
                        return false;
                }
        } else {
                if (!nc_list_append(&list, node)) {
                        return false;
                }
                node->parent = self;
                return mst_template_context_add_value(self, key, MST_TMPL_VALUE_LIST, list);
        }
        return true;
}

/**
 * Search for a value in the current context first, and if it isn't found,
 * reverse the tree until we do find it, or return NULL
 */
MstTemplateValue *mst_template_context_search_value(MstTemplateContext *self, const char *key)
{
        assert(self != NULL);

        MstTemplateContext *ctx = self;
        MstTemplateValue *val = NULL;

        while (ctx) {
                val = nc_hashmap_get(ctx->store, key);
                if (val) {
                        return val;
                }
                ctx = ctx->parent;
        }
        return NULL;
}

/**
 * Internal functions
 */
bool mst_template_render(MstTemplateNode *root_node, MstTemplateContext *context, FILE *stream);
void mst_template_node_free(MstTemplateNode *root);

MstTemplateParser *mst_template_parser_new()
{
        MstTemplateParser *p = NULL;

        p = calloc(1, sizeof(struct MstTemplateParser));
        return p;
}

static inline void mst_template_parser_reset(MstTemplateParser *parser)
{
        mst_template_node_free(parser->root_node);
        parser->root_node = NULL;
}

void mst_template_parser_free(MstTemplateParser *parser)
{
        if (!parser) {
                return;
        }
        mst_template_parser_reset(parser);
        free(parser);
}

/**
 * Construct a new MstTemplateNode with the given tag and type
 */
MstTemplateNode *mst_template_node_new(char *tag, MstTemplateNodeType ntype)
{
        MstTemplateNode *ret = NULL;

        ret = calloc(1, sizeof(MstTemplateNode));
        if (!ret) {
                /* FIXME */
                abort();
        }
        ret->tag = tag;
        ret->ntype = ntype;
        return ret;
}

/**
 * Free a previously allocated MstTemplateNode
 */
void mst_template_node_free(MstTemplateNode *root)
{
        if (!root) {
                return;
        }
        mst_template_node_free(root->child);
        mst_template_node_free(root->next);

        free(root->tag);
        free(root);
}

/**
 * Traverse the node to find the last element
 */
__attribute__((always_inline)) static inline MstTemplateNode *mst_template_node_last(
    MstTemplateNode *node)
{
        MstTemplateNode *n = node;
        MstTemplateNode *p = NULL;

        while (n) {
                p = n->next;
                if (!p) {
                        return n;
                }
                n = p;
        }
        return n;
}

/**
 * Factory function which will disappear in future.
 */
__cbm_inline__ static inline MstTemplateNode *mst_template_node_from_tag(char *tag, size_t tag_len)
{
        switch (tag[0]) {
        case '#':
                return mst_template_node_new(strndup(tag + 1, --tag_len), MST_TMPL_NODE_SECTION);
        case '^':
                return mst_template_node_new(strndup(tag + 1, --tag_len),
                                             MST_TMPL_NODE_INVERSE_SECTION);
        case '/':
                return mst_template_node_new(strndup(tag + 1, --tag_len),
                                             MST_TMPL_NODE_SECTION_END);
        case '!':
                /* This is a comment, skip it. */
                return NULL;
        default:
                return mst_template_node_new(strndup(tag, tag_len), MST_TMPL_NODE_VARIABLE);
        }
}

/**
 * Main parse loop
 */
bool mst_template_parser_load(MstTemplateParser *parser, const char *buffer, ssize_t length)
{
        assert(parser != NULL);

        size_t line_no = 1;
        size_t column = 0;
        size_t start_line_no = 0;
        size_t start_column = 0;
        char start_tags[] = { '{', '{' };
        char end_tags[] = { '}', '}' };

        char *sp_buffer = start_tags;
        char *ep_buffer = end_tags;
        char expected = '\0';
        bool in_tag = false;

        /* So we can dynamically update this later.. */
        size_t sp_buffer_len = 2;
        size_t ep_buffer_len = 2;

        /* Track offsets to buffer, minimize copies. */
        size_t tag_start_offset = -1;
        size_t tag_end_offset = -1;
        ssize_t last_offset = 0;

        MstTemplateNode *root = NULL;
        /* For tracking subcontexts */
        MstTemplateNode *parent = NULL;
        MstTemplateNode *last_sect = NULL;

        mst_template_parser_reset(parser);

        /* Need to do something useful with all this. */
        root = mst_template_node_new(NULL, MST_TMPL_NODE_ROOT);
        if (!root) {
                return -1;
        }

        parent = root;

        for (ssize_t i = 0; i < length; i++) {
                char c = buffer[i];

                /* Handle newline reset */
                if (c == '\n') {
                        ++line_no;
                        column = 0;
                        continue;
                }
                ++column;

                /* If we don't get an expected char, reset the state
                 * Avoids situations like: {}{}all text here
                 */
                if (expected && c != expected) {
                        expected = '\0';
                        in_tag = false;
                        tag_start_offset = tag_end_offset = 0;
                        sp_buffer = start_tags;
                        ep_buffer = end_tags;
                        continue;
                }

                if (c == *sp_buffer) {
                        /* Handle start tags */
                        ++sp_buffer;
                        size_t len = sp_buffer - start_tags;
                        if (len != sp_buffer_len) {
                                expected = *sp_buffer;
                                continue;
                        }
                        sp_buffer = start_tags;
                        in_tag = true;
                        tag_start_offset = i;
                        expected = '\0';
                        start_line_no = line_no;
                        start_column = column;
                } else if (c == *ep_buffer) {
                        size_t tag_len;
                        size_t tag_offset;
                        MstTemplateNode *text_node = NULL;
                        bool text_tail = false;
                        bool root_text = root->child == NULL && parent == root;

                        /* Handle end tags */
                        ++ep_buffer;
                        size_t len = ep_buffer - end_tags;
                        if (len != ep_buffer_len) {
                                expected = *ep_buffer;
                                continue;
                        }
                        expected = '\0';
                        ep_buffer = end_tags;

                        /* Could be a stray end tag in the template, just skip it */
                        if (!in_tag) {
                                continue;
                        }

                        /* Cleanup */
                        in_tag = false;
                        tag_end_offset = i;

                        /* Now have a complete tag. */
                        tag_len = (tag_end_offset - tag_start_offset) - (ep_buffer_len);
                        if (tag_len < 1) {
                                fprintf(stderr,
                                        "[L%ld@%ld] Skipping empty tag\n",
                                        start_line_no,
                                        start_column);
                                continue;
                        }

                        tag_offset = tag_start_offset + 1 /* Skip last */;

                        MstTemplateNode *node =
                            mst_template_node_from_tag((char *)buffer + tag_offset, tag_len);
                        if (!node) {
                                goto next;
                        }

                        switch (node->ntype) {
                        case MST_TMPL_NODE_SECTION:
                        case MST_TMPL_NODE_INVERSE_SECTION:
                                if (!parent->child) {
                                        parent->child = node;
                                } else {
                                        text_node = mst_template_node_last(parent->child);
                                        text_node->next = node;
                                }
                                node->parent = parent;
                                last_sect = NULL;
                                parent = node;
                                break;
                        case MST_TMPL_NODE_SECTION_END:
                                if (!parent->tag || !streq(parent->tag, node->tag)) {
                                        if (parent == root) {
                                                fprintf(stderr,
                                                        "[L%ld@%ld] Closed section '%s' without "
                                                        "any opened section\n",
                                                        start_line_no,
                                                        start_column,
                                                        node->tag);
                                        } else {
                                                fprintf(stderr,
                                                        "[L%ld@%ld] Closed section '%s', expected "
                                                        "'%s'\n",
                                                        start_line_no,
                                                        start_column,
                                                        node->tag,
                                                        parent->tag);
                                        }
                                        mst_template_node_free(node);
                                        goto fail;
                                }
                                last_sect = parent;
                                parent = parent->parent;
                                mst_template_node_free(node);
                                node = NULL;
                                break;
                        case MST_TMPL_NODE_VARIABLE:
                                if (!parent->child) {
                                        text_node = parent;
                                        text_tail = true;
                                        parent->child = node;
                                } else {
                                        text_node = mst_template_node_last(parent->child);
                                        text_node->next = node;
                                }
                                node->parent = parent;
                                break;
                        default:
                                assert(node == NULL);
                                break;
                        }

                        /* Closed a section, set the lead */
                        if (!node) {
                                text_node = mst_template_node_last(last_sect->child);
                                if (text_node) {
                                        node = text_node;
                                } else {
                                        node = last_sect;
                                }
                        }

                        if (root_text) {
                                text_node = root;
                                text_tail = true;
                        }
                        if (!text_node) {
                                text_node = node;
                        }

                        size_t lead_start = last_offset;
                        size_t lead_len = (tag_start_offset - lead_start) - 1;

                        if (lead_len > 0) {
                                if (text_tail) {
                                        text_node->lead = (char *)buffer + lead_start;
                                        text_node->lead_len = lead_len;
                                } else {
                                        text_node->tail = (char *)buffer + lead_start;
                                        text_node->tail_len = lead_len;
                                }
                        }
                next:
                        last_offset = tag_end_offset + 1;
                        tag_end_offset = tag_start_offset = -1;
                } else {
                        sp_buffer = start_tags;
                        ep_buffer = end_tags;
                }
        }
        if (parent != root) {
                fprintf(stderr, "Section '%s' was not closed\n", parent->tag);
                goto fail;
        }
        /* Ensure all text is compiled into the template */
        if (last_offset < length) {
                ssize_t diff = length - last_offset;
                root->tail_len = diff;
                root->tail = (char *)buffer + last_offset;
        }

        parser->root_node = root;
        return true;
fail:
        mst_template_node_free(root);

        return false;
}

bool mst_template_render(MstTemplateNode *root, MstTemplateContext *context, FILE *stream)
{
        if (!root) {
                return false;
        }
        bool should_render = false;
        bool render_lead = false;
        MstTemplateValue *svalue = NULL;
        MstTemplateContext *child_context = context;
        NcList *list = NULL;

        if (root->tag && context) {
                svalue = mst_template_context_search_value(context, root->tag);
        }

        switch (root->ntype) {
        /* Special nodes, always traverse */
        case MST_TMPL_NODE_ROOT:
                if (root->lead) {
                        if (fprintf(stream, "%.*s", (int)root->lead_len, root->lead) < 0) {
                                return false;
                        }
                }
                should_render = true;
                break;
        case MST_TMPL_NODE_VARIABLE:
                if (root->lead) {
                        if (fprintf(stream, "%.*s", (int)root->lead_len, root->lead) < 0) {
                                return false;
                        }
                }
                if (svalue) {
                        if (svalue->vtype == MST_TMPL_VALUE_STRING) {
                                if (fputs(svalue->value, stream) < 0) {
                                        return false;
                                }
                        } else if (svalue->vtype == MST_TMPL_VALUE_BOOL) {
                                if (fprintf(stream, svalue->value ? "true" : "false") < 0) {
                                        return false;
                                }
                        } else if (svalue->vtype == MST_TMPL_VALUE_LIST) {
                                if (fprintf(stream, svalue->value ? "true" : "false") < 0) {
                                        return false;
                                }
                                fprintf(stderr,
                                        "Warning: Variable %s is a list, use in a section\n",
                                        root->tag);
                        } else if (svalue->vtype == MST_TMPL_VALUE_CHILD) {
                                if (fprintf(stream, svalue->value ? "true" : "false") < 0) {
                                        return false;
                                }
                                fprintf(
                                    stderr,
                                    "Warning: Variable %s is a child context, use as a section\n",
                                    root->tag);
                        }
                } /* skip null variable */
                should_render = true;
                break;
        case MST_TMPL_NODE_SECTION:
        case MST_TMPL_NODE_INVERSE_SECTION:
                /* Set up child context if necessary */
                render_lead = true;
                if (svalue) {
                        should_render = true;
                        if (svalue->vtype == MST_TMPL_VALUE_CHILD) {
                                child_context = (MstTemplateContext *)svalue->value;
                        } else if (svalue->vtype == MST_TMPL_VALUE_BOOL) {
                                /* Boolean is basically not-null */
                                if (svalue->value) {
                                        should_render = true;
                                } else {
                                        should_render = false;
                                }
                        } else if (svalue->vtype == MST_TMPL_VALUE_STRING) {
                                /* No key? No section. */
                                if (!svalue->value) {
                                        should_render = false;
                                }
                        } else if (svalue->vtype == MST_TMPL_VALUE_LIST) {
                                list = svalue->value;
                                if (!list) {
                                        should_render = false;
                                } else {
                                        render_lead = false;
                                }
                        }
                }
                if (render_lead) {
                        if (root->lead) {
                                if (fprintf(stream, "%.*s", (int)root->lead_len, root->lead) < 0) {
                                        return false;
                                }
                        }
                }
                /* Invert it again */
                if (root->ntype == MST_TMPL_NODE_INVERSE_SECTION) {
                        should_render = !should_render;
                }
                break;
        default:
                if (root->lead) {
                        if (fprintf(stream, "%.*s", (int)root->lead_len, root->lead) < 0) {
                                return false;
                        }
                }
                should_render = false;
        }

        /* Render with the child context */
        if (should_render) {
                NcList *elem = NULL;
                if (!list) {
                        if (root->child &&
                            !mst_template_render(root->child, child_context, stream)) {
                                return false;
                        }
                } else {
                        /* Re-render the root child node with the given context */
                        NC_LIST_FOREACH (list, elem) {
                                child_context = elem->data;
                                child_context->parent = context;
                                if (root->lead) {
                                        if (fprintf(stream,
                                                    "%.*s",
                                                    (int)root->lead_len,
                                                    root->lead) < 0) {
                                                return false;
                                        }
                                }
                                if (root->child) {
                                        if (!mst_template_render(root->child,
                                                                 child_context,
                                                                 stream)) {
                                                return false;
                                        }
                                } else {
                                        /* Text-only node with no children */
                                        if (root->tail) {
                                                int r = fprintf(stream,
                                                                "%.*s\n",
                                                                (int)root->tail_len,
                                                                root->tail);
                                                if (r < 0) {
                                                        return false;
                                                }
                                        }
                                        should_render = false;
                                }
                        }
                }
        }

        if (should_render && root->tail) {
                if (fprintf(stream, "%.*s", (int)root->tail_len, root->tail) < 0) {
                        return false;
                }
        }

        /* Render the next guy */
        if (root->next && !mst_template_render(root->next, context, stream)) {
                return false;
        }
        return true;
}

bool mst_template_parser_render(MstTemplateParser *parser, MstTemplateContext *context,
                                FILE *stream)
{
        if (!parser || !parser->root_node) {
                return false;
        }
        return mst_template_render(parser->root_node, context, stream);
}

char *mst_template_parser_render_string(MstTemplateParser *parser, MstTemplateContext *context)
{
        assert(parser != NULL);

        FILE *fst = NULL;
        char *buf = NULL;
        size_t siz;
        bool ret = false;

        fst = open_memstream(&buf, &siz);
        if (!fst) {
                fprintf(stderr, "Failed to allocate memstream: %s\n", strerror(errno));
                return NULL;
        }

        ret = mst_template_parser_render(parser, context, fst);
        fflush(fst);
        if (!ret) {
                free(buf);
                fclose(fst);
                return NULL;
        }
        fclose(fst);
        return buf;
}
