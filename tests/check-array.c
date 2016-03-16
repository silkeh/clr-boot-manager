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
#include <check.h>
#include <malloc.h>
#include <stdlib.h>

#include "array.c"
#include "util.c"


START_TEST(cbm_array_new_check)
{
        CBMArray *array = NULL;

        array = cbm_array_new();
        fail_if(!array, "Failed to allocate new array");
        fail_if(array->data, "array->data is not NULL after new");
        fail_if(array->len != 0, "array->len is not 0 after new");
        cbm_array_free(&array, NULL);
}
END_TEST

START_TEST(cbm_array_free_check)
{
        CBMArray *array = NULL;

        cbm_array_free(&array, NULL);
        fail_if(array, "Free changed NULL array to non NULL");
        /* Check to see if array free on NULL segfaults */
        cbm_array_free(NULL, NULL);
        array = cbm_array_new();
        fail_if(!array, "Failed to allocate new array");
        cbm_array_free(&array, NULL);
        fail_if(array, "Failed to set array to NULL");
        }
END_TEST

static inline void cbm_array_free_fun(void *p)
{
	free(p);
}

START_TEST(cbm_array_add_check)
{
        CBMArray *array = NULL;
        int data1 = 1;
        int data2 = 2;
        int *data3 = NULL;

        fail_if(cbm_array_add(NULL, &data1),
                "Added data to NULL array");
        array = cbm_array_new();
        fail_if(!array, "Failed to allocate new array");
        fail_if(cbm_array_add(array, NULL),
                "Added NULL data to array");
        fail_if(!cbm_array_add(array, &data1),
                "Failed to add data1 to array");
        fail_if(!array->data, "Failed to allocate array->data");
        fail_if(array->len != 1,
                "Failed to update array->len with the size of the array");
        fail_if(*((int *)array->data[0]) != 1,
                "Failed to store correct data value to array");
        array->len = (uint16_t)(0 - 1);
        fail_if(cbm_array_add(array, &data1),
                "Able to add more than max number of elements");
        array->len = 1;
        /* Test resize */
        fail_if(!cbm_array_add(array, &data2),
                "Failed to add second element to array");
        fail_if(!array->data, "Failed to keep array->data");
        fail_if(array->len != 2,
                "Failed to update array->len with new size");
        fail_if(*((int *)array->data[0]) != 1,
                "Changed the first array element");
        fail_if(*((int *)array->data[1]) != 2,
                "Failed to set the second array element");
        cbm_array_free(&array, NULL);
        fail_if(array, "Failed to set array to NULL 1");
        data3 = malloc(sizeof(int));
        fail_if(!data3, "Failed to allocate data");
        *data3 = 3;
        array = cbm_array_new();
        fail_if(!array, "Failed to allocate new array");
        fail_if(!cbm_array_add(array, data3),
                "Failed to add pointer data");
        fail_if(*((int *)array->data[0]) != 3,
                "Failed to store correct pointer data value to array");
        cbm_array_free(&array, cbm_array_free_fun);
        fail_if(array, "Failed to set array to NULL 2");
}
END_TEST

START_TEST(cbm_array_get_check)
{
        CBMArray *array = NULL;
        int data1 = 1;

        array = cbm_array_new();
        fail_if(!array, "Failed to allocate new array");
        fail_if(cbm_array_get(NULL, 0),
                "Got data from NULL array");
        fail_if(cbm_array_get(array, 0),
                "Got data from empty array");
        fail_if(!cbm_array_add(array, &data1),
                "Failed to add data1 to array");
        fail_if(*((int *)cbm_array_get(array, 0)) != 1,
                "Failed to get correct value for element 0");
        fail_if(cbm_array_get(array, 1),
                "Got data past end of array");
        cbm_array_free(&array, NULL);
        fail_if(array, "Failed to set array to NULL");
}
END_TEST

START_TEST(cbm_array_check)
{
        CBMArray *array = NULL;
        char *value;
        char *element;
        void *f;
        bool r;

        array = cbm_array_new();
        fail_if(array == NULL, "Failed to allocate memory for CBMArray");
        element = strdup("test");
        fail_if(!element, "Failed to allocate memory for array item");
        r = cbm_array_add(NULL, element);
        fail_if(r, "Added element to NULL array");
        r = cbm_array_add(array, NULL);
        fail_if(r, "Added NULL element to array");
        r = cbm_array_add(array, element);
        fail_if(r  == false, "Failed to add element to CBMArray");
        fail_if(array->len != 1,
                "Failed to get correct value for number of elements in array");

        f = cbm_array_get(NULL, 0);
        fail_if(f, "Got value from NULL array");
        f = cbm_array_get(array, (uint16_t)(array->len + 1));
        fail_if(f, "Got value from index bigger than maximum index");
        value = (char *)cbm_array_get(array, 0);

        fail_if(value == NULL,
                "Failed to get value from CBMArray");

        fail_if(strcmp(value, "test") != 0,
                "Failed to retrieve the stored value");

        cbm_array_free(&array, cbm_array_free_fun);
        fail_if(array != NULL,
                "Failed to free CBMArray");
}
END_TEST

int main(void)
{
	int number_failed;
	Suite *s;
	SRunner *sr;
	TCase *tc;

	s = suite_create("cbm_array");
	tc = tcase_create("cbm_array_functions");
	tcase_add_test(tc, cbm_array_new_check);
	tcase_add_test(tc, cbm_array_free_check);
	tcase_add_test(tc, cbm_array_add_check);
	tcase_add_test(tc, cbm_array_get_check);
	tcase_add_test(tc, cbm_array_check);
	suite_add_tcase(s, tc);

	sr = srunner_create(s);
	srunner_run_all(sr, CK_VERBOSE);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
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
