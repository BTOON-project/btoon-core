/* SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
 * SPDX-License-Identifier: MIT
 *
 * The C API used from C: capi.h compiles as C, zero-initialized options are the defaults,
 * timestamps keep nanoseconds and offsets, and errors come back as status codes. Exits
 * nonzero on the first failed check. */
#include "btoon/capi.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

static int same(const btoon_result_t* r, const unsigned char* bytes, size_t n) {
    return r->status == BTOON_OK && r->size == n && memcmp(r->data, bytes, n) == 0;
}

int main(void) {
    /* 2024-01-01T00:00:00.5+02:00: the 14-byte timestamp with an offset. */
    static const unsigned char ts_offset[] = {0xc7, 0x0e, 0xff, 0x1d, 0xcd, 0x65, 0x00, 0, 0, 0, 0,
                                              0x65, 0x92, 0x00, 0x80, 0x00, 0x78};
    btoon_status_t status = BTOON_ERROR_GENERIC;
    char* error = NULL;
    btoon_value_t* ts = btoon_decode(ts_offset, sizeof ts_offset, NULL, &status, &error);
    CHECK(ts != NULL && status == BTOON_OK && error == NULL);
    if (ts) {
        CHECK(ts->type == BTOON_TYPE_TIMESTAMP);
        CHECK(ts->as.ts.nanoseconds == 500000000u);
        CHECK(ts->as.ts.has_timezone && ts->as.ts.timezone_offset == 120);
        btoon_result_t again = btoon_encode(ts, NULL);
        CHECK(same(&again, ts_offset, sizeof ts_offset));
        btoon_free_result(again);
        btoon_value_destroy(ts);
    }

    /* Zero-initialized options: tabular stays on, as by default. */
    {
        btoon_value_t* rows[8];
        int i;
        btoon_encode_options_t eopts = {0};
        btoon_result_t with_null, with_zero;
        btoon_value_t* arr;
        for (i = 0; i < 8; ++i) {
            btoon_string_t key = {"id", 2};
            btoon_value_t* val = btoon_value_create_int(i);
            const btoon_value_t* vals[1];
            vals[0] = val;
            rows[i] = btoon_value_create_map(&key, vals, 1);
            btoon_value_destroy(val);
        }
        arr = btoon_value_create_array((const btoon_value_t**)rows, 8);
        with_null = btoon_encode(arr, NULL);
        with_zero = btoon_encode(arr, &eopts);
        CHECK(with_null.status == BTOON_OK && with_zero.status == BTOON_OK);
        CHECK(with_null.size == with_zero.size && memcmp(with_null.data, with_zero.data, with_null.size) == 0);
        CHECK(with_zero.size > 3 && with_zero.data[2] == 0xf6); /* the tabular extension, -10 */
        btoon_free_result(with_null);
        btoon_free_result(with_zero);
        for (i = 0; i < 8; ++i) btoon_value_destroy(rows[i]);
        btoon_value_destroy(arr);
    }

    /* Errors are codes, never crashes. */
    {
        static const unsigned char truncated[] = {0x92, 0x01};
        btoon_decode_options_t dopts;
        btoon_value_t node;
        btoon_result_t r;
        memset(&dopts, 0, sizeof dopts);
        CHECK(btoon_decode(truncated, sizeof truncated, &dopts, &status, &error) == NULL);
        CHECK(status == BTOON_ERROR_TRUNCATED && error != NULL);
        CHECK(strcmp(btoon_status_name(status), "truncated") == 0);
        btoon_free_error(error);
        CHECK(btoon_value_create_string("x", (size_t)-1) == NULL);
        CHECK(btoon_value_create_binary(NULL, 1) == NULL);
        memset(&node, 0, sizeof node);
        node.type = (btoon_type_t)77; /* any int is a valid enum value in C */
        r = btoon_encode(&node, NULL);
        CHECK(r.status == BTOON_ERROR_INVALID_ARGUMENT && r.data == NULL);
        btoon_free_result(r);
        CHECK(strcmp(btoon_status_name((btoon_status_t)99), "unknown") == 0);
    }

    if (failures == 0) printf("C API from C: all checks passed (btoon %s, spec %s)\n", btoon_version(),
                              btoon_spec_version());
    return failures == 0 ? 0 : 1;
}
