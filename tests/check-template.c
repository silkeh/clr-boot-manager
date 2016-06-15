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
#include <check.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "util.h"

#include "template.h"

START_TEST(bootman_template_test_simple_new)
{
        MstTemplateParser *parser = NULL;

        parser = mst_template_parser_new();
        fail_if(!parser, "Failed to construct new MstTemplateParser");
        mst_template_parser_free(parser);
}
END_TEST

START_TEST(bootman_template_test_simple_static)
{
        autofree(MstTemplateParser) *parser = NULL;
        const char *buf = "I am a string\n";
        const char *buf2 = "I am{{non-var}} a string\n";

        parser = mst_template_parser_new();

        fail_if(mst_template_parser_render(parser, NULL, stderr), "Rendered unloaded MstTemplate");

        fail_if(!mst_template_parser_load(parser, buf, strlen(buf)), "Failed to load static text");

        fail_if(!mst_template_parser_render(parser, NULL, stderr),
                "Failed to render with a null context");

        fail_if(!mst_template_parser_load(parser, buf2, strlen(buf2)),
                "Failed to load static text2");

        fail_if(!mst_template_parser_render(parser, NULL, stderr),
                "Failed to render2 with a null context");
}
END_TEST

START_TEST(bootman_template_test_simple_string)
{
        autofree(MstTemplateParser) *parser = NULL;
        const char *inp_buf = "I am a {{no-key}}string";
        const char *exp = "I am a string";
        autofree(char) *ret = NULL;

        parser = mst_template_parser_new();
        fail_if(!mst_template_parser_load(parser, inp_buf, strlen(inp_buf)),
                "Failed to load static text");

        ret = mst_template_parser_render_string(parser, NULL);
        fail_if(!ret, "Failed to render string");
        fail_if(!streq(ret, exp), "Returned text does not match expectation");

        /* Temp debug */
        fprintf(stderr, "Ret was: %s\n", ret);
}
END_TEST

/**
 * One key test
 */
START_TEST(bootman_template_test_simple_key)
{
        autofree(MstTemplateParser) *parser = NULL;
        autofree(MstTemplateContext) *context = NULL;
        const char *inp1 = "My name is {{name}}";
        const char *exp1 = "My name is not important";
        const char *exp2 = "My name is Ikey";
        autofree(char) *ret1 = NULL;
        autofree(char) *ret2 = NULL;

        parser = mst_template_parser_new();
        context = mst_template_context_new();
        fail_if(!context, "Failed to create a context!");

        fail_if(!mst_template_context_add_string(context, "name", "not important"),
                "Failed to add named key to context!");

        fail_if(!mst_template_parser_load(parser, inp1, strlen(inp1)), "Failed to prepare parser");

        ret1 = mst_template_parser_render_string(parser, context);
        fail_if(!ret1, "Failed to render template!");
        fail_if(!streq(ret1, exp1), "Returned string does not match expectation");

        /* Replace value in context */
        fail_if(!mst_template_context_add_string(context, "name", "Ikey"));
        ret2 = mst_template_parser_render_string(parser, context);
        fail_if(!ret2, "Failed to render template twice");
        fail_if(!streq(ret2, exp2), "Second returned string does not match expectation");
}
END_TEST

static Suite *core_suite(void)
{
        Suite *s = NULL;
        TCase *tc = NULL;

        s = suite_create("bootman_template");
        tc = tcase_create("bootman_template_simple");
        tcase_add_test(tc, bootman_template_test_simple_new);
        tcase_add_test(tc, bootman_template_test_simple_static);
        tcase_add_test(tc, bootman_template_test_simple_string);
        tcase_add_test(tc, bootman_template_test_simple_key);
        suite_add_tcase(s, tc);

        return s;
}

int main(void)
{
        Suite *s;
        SRunner *sr;
        int fail;

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
