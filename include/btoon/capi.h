//  ██████╗ ████████╗ ██████╗  ██████╗ ███╗   ██╗
//  ██╔══██╗╚══██╔══╝██╔═══██╗██╔═══██╗████╗  ██║
//  ██████╔╝   ██║   ██║   ██║██║   ██║██╔██╗ ██║
//  ██╔══██╗   ██║   ██║   ██║██║   ██║██║╚██╗██║
//  ██████╔╝   ██║   ╚██████╔╝╚██████╔╝██║ ╚████║
//  ╚═════╝    ╚═╝    ╚═════╝  ╚═════╝ ╚═╝  ╚═══╝
//
//  BTOON Core
//  https://btoon.net & https://github.com/BTOON-project/btoon-core
//
// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT

/**
 * @file capi.h
 * @brief C API for BTOON.
 *
 * Errors: no C++ exception crosses this API. Functions that create values return NULL
 * when memory runs out or an argument is invalid. btoon_encode() and btoon_decode()
 * report a btoon_status_t code (the docs/SPEC.md §7 error codes) and a message.
 *
 * Ownership:
 * - Every btoon_value_t returned by a btoon_value_create_*() function or by
 *   btoon_decode() belongs to the caller, who frees it with btoon_value_destroy(). That
 *   frees the whole tree: its strings, buffers, elements and entries.
 * - The create functions copy what they're given. btoon_value_create_array() and
 *   btoon_value_create_map() deep-copy their elements, so the caller still owns, and
 *   must still destroy, the values it passed in.
 * - Strings in values are NUL-terminated copies; `len` doesn't count the NUL. Other
 *   buffers (binary, bigint, extension, vectors) have `ptr == NULL` exactly when
 *   `len == 0`.
 * - btoon_encode() only reads its value. Any well-formed tree works, including one built
 *   by hand, but don't free the parts of a value that the library created, or replace
 *   them, except through btoon_value_destroy().
 * - btoon_result_t's `data` and `error` are freed together by btoon_free_result(). An
 *   error message from btoon_decode() is freed with btoon_free_error().
 *
 * Options: a zero-initialized options struct gives the defaults, and NULL options mean
 * the same.
 *
 * Since 0.3.0. The ABI changed in 0.3.0 (timestamp fields, options, status codes); code
 * written against 0.2.x must be updated and recompiled.
 */
#ifndef BTOON_CAPI_H
#define BTOON_CAPI_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The library is static on Windows (see CMakeLists.txt), so nothing is imported from a DLL. */
#if !defined(_WIN32) && defined(__GNUC__)
    #define BTOON_API __attribute__((visibility("default")))
#else
    #define BTOON_API
#endif

/* --- Status codes ----------------------------------------------------------------- */

/** BTOON_OK, or one of the error codes of docs/SPEC.md §7. */
typedef enum {
    BTOON_OK = 0,
    BTOON_ERROR_GENERIC = 1,
    BTOON_ERROR_TRUNCATED = 2,
    BTOON_ERROR_INVALID_MARKER = 3,
    BTOON_ERROR_TRAILING_DATA = 4,
    BTOON_ERROR_INVALID_UTF8 = 5,
    BTOON_ERROR_NON_STRING_KEY = 6,
    BTOON_ERROR_DUPLICATE_KEY = 7,
    BTOON_ERROR_MAX_DEPTH = 8,
    BTOON_ERROR_INVALID_EXTENSION = 9,
    BTOON_ERROR_INVALID_TABULAR = 10,
    BTOON_ERROR_INVALID_ENVELOPE = 11,
    BTOON_ERROR_SIZE_LIMIT = 12,
    BTOON_ERROR_UNSUPPORTED_COMPRESSION = 13,
    BTOON_ERROR_AUTHENTICATION_FAILED = 14,
    /** A bad argument or option, such as a NULL pointer with a nonzero length, an empty
        HMAC key, or a value the encoder can't write. */
    BTOON_ERROR_INVALID_ARGUMENT = 15,
    BTOON_ERROR_OUT_OF_MEMORY = 16
} btoon_status_t;

/** The spec name of a status: "ok", "truncated", "max_depth", …, "out_of_memory". */
BTOON_API const char* btoon_status_name(btoon_status_t status);

/* --- Values ------------------------------------------------------------------------ */

typedef enum {
    BTOON_TYPE_NIL,
    BTOON_TYPE_BOOL,
    BTOON_TYPE_INT,
    BTOON_TYPE_UINT,
    BTOON_TYPE_FLOAT,
    BTOON_TYPE_STRING,
    BTOON_TYPE_BINARY,
    BTOON_TYPE_ARRAY,
    BTOON_TYPE_MAP,
    BTOON_TYPE_TIMESTAMP,
    BTOON_TYPE_DATE,
    BTOON_TYPE_DATETIME,
    BTOON_TYPE_BIGINT,
    BTOON_TYPE_VECTOR_FLOAT,
    BTOON_TYPE_VECTOR_DOUBLE,
    BTOON_TYPE_EXTENSION
} btoon_type_t;

typedef struct btoon_value btoon_value_t;

typedef struct {
    const char* ptr;
    size_t len;
} btoon_string_t;

typedef struct {
    const uint8_t* ptr;
    size_t len;
} btoon_binary_t;

typedef struct {
    btoon_value_t** elements;
    size_t len;
} btoon_array_t;

typedef struct {
    btoon_string_t* keys;
    btoon_value_t** values;
    size_t len;
} btoon_map_t;

typedef struct {
    int8_t type;
    const uint8_t* ptr;
    size_t len;
} btoon_extension_t;

/** An instant in UTC (extension -1, docs/SPEC.md §5.1). */
typedef struct {
    int64_t seconds;          /**< Seconds since the Unix epoch, UTC. */
    uint32_t nanoseconds;     /**< 0 … 999999999. */
    int16_t timezone_offset;  /**< Minutes east of UTC, -1080 … 1080, if has_timezone. */
    bool has_timezone;        /**< Written in the 14-byte form with the offset. */
} btoon_timestamp_t;

typedef struct {
    int64_t milliseconds;
} btoon_date_t;

typedef struct {
    int64_t nanoseconds;
} btoon_datetime_t;

/** Two's complement, big-endian. */
typedef struct {
    const uint8_t* ptr;
    size_t len;
} btoon_bigint_t;

typedef struct {
    const float* ptr;
    size_t len;  /**< Number of floats. */
} btoon_vector_float_t;

typedef struct {
    const double* ptr;
    size_t len;  /**< Number of doubles. */
} btoon_vector_double_t;

struct btoon_value {
    btoon_type_t type;
    union {
        bool b;
        int64_t i;
        uint64_t u;
        double f;
        btoon_string_t s;
        btoon_binary_t bin;
        btoon_array_t a;
        btoon_map_t m;
        btoon_timestamp_t ts;
        btoon_date_t date;
        btoon_datetime_t dt;
        btoon_bigint_t bi;
        btoon_vector_float_t vf;
        btoon_vector_double_t vd;
        btoon_extension_t ext;
    } as;
};

/* --- Creating and destroying values ---------------------------------------------------
 * Each returns a new value that the caller frees with btoon_value_destroy(), or NULL if
 * memory runs out or a pointer is NULL while its length isn't 0. Values aren't checked
 * here: btoon_encode() rejects, for example, nanoseconds of 10^9 or more, or an
 * extension id that BTOON defines (-1 … -6, -10). */

BTOON_API btoon_value_t* btoon_value_create_nil(void);
BTOON_API btoon_value_t* btoon_value_create_bool(bool value);
BTOON_API btoon_value_t* btoon_value_create_int(int64_t value);
BTOON_API btoon_value_t* btoon_value_create_uint(uint64_t value);
BTOON_API btoon_value_t* btoon_value_create_float(double value);
/** Copies `len` bytes of UTF-8 (not checked until encoding). */
BTOON_API btoon_value_t* btoon_value_create_string(const char* str, size_t len);
BTOON_API btoon_value_t* btoon_value_create_binary(const uint8_t* data, size_t len);
/** Deep-copies the elements; NULL elements are an error. */
BTOON_API btoon_value_t* btoon_value_create_array(const btoon_value_t** elements, size_t len);
/** Deep-copies the entries. NULL values and duplicate keys are errors. */
BTOON_API btoon_value_t* btoon_value_create_map(const btoon_string_t* keys, const btoon_value_t** values, size_t len);
/** A UTC instant without an offset. */
BTOON_API btoon_value_t* btoon_value_create_timestamp(int64_t seconds, uint32_t nanoseconds);
/** A UTC instant that keeps a timezone offset in minutes (the 14-byte form). */
BTOON_API btoon_value_t* btoon_value_create_timestamp_with_offset(int64_t seconds, uint32_t nanoseconds,
                                                                  int16_t offset_minutes);
BTOON_API btoon_value_t* btoon_value_create_date(int64_t milliseconds);
BTOON_API btoon_value_t* btoon_value_create_datetime(int64_t nanoseconds);
/** Two's complement, big-endian, at least one byte. Encoded as an integer if it fits. */
BTOON_API btoon_value_t* btoon_value_create_bigint(const uint8_t* bytes, size_t len);
BTOON_API btoon_value_t* btoon_value_create_vector_float(const float* data, size_t len);
BTOON_API btoon_value_t* btoon_value_create_vector_double(const double* data, size_t len);
BTOON_API btoon_value_t* btoon_value_create_extension(int8_t type, const uint8_t* data, size_t len);

/** Frees a value and everything it holds. NULL is ignored. */
BTOON_API void btoon_value_destroy(btoon_value_t* value);

/* --- Encoding and decoding ------------------------------------------------------------ */

typedef enum {
    BTOON_COMPRESSION_ZLIB = 0,
    BTOON_COMPRESSION_LZ4 = 1,
    BTOON_COMPRESSION_ZSTD = 2,
    BTOON_COMPRESSION_BROTLI = 3,
    BTOON_COMPRESSION_SNAPPY = 4
} btoon_compression_t;

/** Zero-initialized (or NULL) means the defaults. */
typedef struct {
    bool compress;                              /**< Add a compression envelope (§6) when it saves ≥ 5%. */
    btoon_compression_t compression_algorithm;  /**< Used when compress is set; zlib by default. */
    int compression_level;                      /**< 0 means the algorithm's default. */
    bool no_tabular;                            /**< Never use the columnar encoding (§5.6). */
    const uint8_t* hmac_key;                    /**< Sign with HMAC-SHA256 (§8); NULL means don't sign. */
    size_t hmac_key_len;                        /**< Must not be 0 when hmac_key is set. */
} btoon_encode_options_t;

/** Zero-initialized (or NULL) means the defaults. */
typedef struct {
    bool no_decompress;            /**< Reject compressed input instead of decompressing it. */
    bool non_strict;               /**< Non-strict mode (§7): allows trailing data, duplicate keys and
                                        invalid UTF-8 in string values. */
    size_t max_depth;              /**< 0 means 128. At most 1000. */
    size_t max_decompressed_size;  /**< 0 means 64 MiB. */
    size_t max_values;             /**< 0 means 2^20 (§7.1). */
    const uint8_t* hmac_key;       /**< Require and verify a signed envelope (§8); NULL means no key. */
    size_t hmac_key_len;           /**< Must not be 0 when hmac_key is set. */
} btoon_decode_options_t;

typedef struct {
    uint8_t* data;          /**< The encoded bytes, or NULL on failure. */
    size_t size;
    char* error;            /**< NULL on success, else a message. */
    btoon_status_t status;  /**< BTOON_OK on success. */
} btoon_result_t;

/** Encodes `value` canonically. Free the result with btoon_free_result(). */
BTOON_API btoon_result_t btoon_encode(const btoon_value_t* value, const btoon_encode_options_t* options);

/**
 * Decodes a complete document. Returns a value the caller frees with
 * btoon_value_destroy(), or NULL on failure. Then *status (if `status` isn't NULL) holds
 * the error code, and *error (if `error` isn't NULL) a message to free with
 * btoon_free_error(); on success they're BTOON_OK and NULL. `data` may be NULL only if
 * `size` is 0.
 */
BTOON_API btoon_value_t* btoon_decode(const uint8_t* data, size_t size, const btoon_decode_options_t* options,
                                      btoon_status_t* status, char** error);

/** Frees a result's data and error message. */
BTOON_API void btoon_free_result(btoon_result_t result);

/** Frees an error message returned by btoon_decode(). NULL is ignored. */
BTOON_API void btoon_free_error(char* error);

/* --- Version ------------------------------------------------------------------------ */

/** Library version, e.g. "0.3.0". */
BTOON_API const char* btoon_version(void);
/** Version of docs/SPEC.md implemented, e.g. "0.3". */
BTOON_API const char* btoon_spec_version(void);

#ifdef __cplusplus
}
#endif

#endif // BTOON_CAPI_H
