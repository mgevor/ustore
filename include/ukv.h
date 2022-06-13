/**
 * @file ukv.h
 * @author Ashot Vardanian
 * @date 9 Sep 2012
 * @brief C bindings for Unums Key-Value store, that provide:
 * > ABI stability for the essential CRUD operations,
 * > Interoperability with higher-level languages.
 *
 * @todo Iterators over transactions/snapshots.
 * @todo Manual state control over compactions.
 * @todo Bulk imports.
 * @todo Creating and removing columns/collections.
 *
 * @section Why prefer batch APIs?
 * Using the batch APIs to issue a single read/write request
 * is trivial, but achieving batch-level performance with
 * singular operations is impossible. Regardless of IO layer,
 * a lot of synchronization and locks must be issued to provide
 * consistency.
 *
 * @section Interface Conventions
 * 1. We try to expose just opaque struct pointers and functions to
 * 	  clients. This allows us to change internal representations
 *    without forcing clients to recompile code, that uses shared lib.
 * 2. Errors are encoded into NULL-terminated C strings.
 * 3. Functions that accept `columns`, @b can receive 0, 1 or N such
 *    arguments, where N is the number of passed `keys`.
 * 4. Collections, Iterators and Transactions are referencing the DB,
 *    so the DB shouldn't die/close before those objects are freed.
 *    This also allows us to reduce the number of function arguments for
 *    interface functions.
 *
 * @section Reference Designs
 * This interface is design as a generalization over most CRUD APIs
 * for key-value stores. It can be used to wrap anything like:
 * * LevelDB:
 *      https://github.com/google/leveldb/blob/main/include/leveldb/c.h
 *      https://github.com/google/leveldb/blob/main/db/c.cc
 *      https://plyvel.readthedocs.io
 * * RocksDB:
 *      https://github.com/facebook/rocksdb/blob/main/include/rocksdb/c.h
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*********************************************************/
/*****************		  Structures	  ****************/
/*********************************************************/

typedef void* ukv_t;
typedef void* ukv_txn_t;
typedef void* ukv_iter_t;
typedef void* ukv_column_t;
typedef void* ukv_options_read_t;
typedef void* ukv_options_write_t;

typedef uint64_t ukv_key_t;
typedef void* ukv_val_ptr_t;
typedef uint32_t ukv_val_len_t;
typedef char const* ukv_error_t;

/*********************************************************/
/*****************	 Primary Functions	  ****************/
/*********************************************************/

/**
 * @brief Opens the underlying Key-Value Store, which can be any of:
 * > embedded persistent transactional KVS
 * > embedded in-memory transactional KVS
 * > remote persistent transactional KVS
 * > remote in-memory transactional KVS
 *
 * @param config    Configuration NULL-terminated string.
 * @param db        A pointer to initialized and opened KVS, unless @p `error` is filled.
 * @param error     The error message to be handled by callee.
 */
void ukv_open(
    // Inputs:
    char const* config,
    // Outputs:
    ukv_t* db,
    ukv_error_t* error);

/**
 * @brief The primary "setter" interface that inserts into "HEAD" state.
 * Passing NULLs into @p `values` is identical to deleting entries.
 * If a fail had occured, @p `error` will be set to non-NULL.
 * @see `ukv_txn_put` for transactional consistent inserts.
 *
 * @param db                Already open database instance, @see `ukv_open`.
 * @param keys              Array of keys in one or more collections.
 * @param keys_count        Number of elements in @p `keys`.
 * @param columns           Array of columns with 0, 1 or `keys_count`
 *                          elements describing the locations of keys.
 *                          If NULL is passed, the default collection
 *                          is assumed.
 * @param columns_count     Number of elements in @p `columns`.
 * @param options           Write options.
 * @param values            Array of pointers to the first byte of each value.
 *                          NULLs, if you want to @b delete the values associated
 *                          with given @p `keys`.
 * @param values_lengths    Array of lengths of buffers, storing the @p `values`.
 * @param error             The error to be handled.
 */
void ukv_put(
    // Inputs:
    ukv_t const db,
    ukv_key_t const* keys,
    size_t const keys_count,
    ukv_column_t const* columns,
    size_t const columns_count,
    ukv_options_write_t const options,
    //
    ukv_val_ptr_t const* values,
    ukv_val_len_t const* values_lengths,
    // Outputs:
    ukv_error_t* error);

/**
 * @brief Checks if certain keys are present in passed collections.
 *
 * @param db                Already open database instance, @see `ukv_open`.
 * @param keys              Array of keys in one or more collections.
 * @param keys_count        Number of elements in @p `keys`.
 * @param columns           Array of columns with 0, 1 or `keys_count`
 *                          elements describing the locations of keys.
 *                          If NULL is passed, the default collection
 *                          is assumed.
 * @param columns_count     Number of elements in @p `columns`.
 * @param options           Write options.
 * @param arena             Points to a memory region that we use during
 *                          this request. If it's too small (@p `arena_length`),
 *                          we `realloc` a new buffer. You can't pass a memory
 *                          allocated by a third-party allocator.
 *                          During the first request you pass a `NULL`,
 *                          we allocate that buffer, put found values in it and
 *                          return you a pointer. You can later reuse it for future
 *                          requests, or `free` it via `ukv_get_free`.
 * @param arena_length      Current size of @p `arena`.
 * @param values            Array of @p `keys_count` output boolean indicators.
 * @param error             The error message to be handled by callee.
 */
void ukv_contains(
    // Inputs:
    ukv_t const db,
    ukv_key_t const* keys,
    size_t const keys_count,
    ukv_column_t const* columns,
    size_t const columns_count,

    // In-outs:
    void** arena,
    size_t* arena_length,

    // Outputs:
    bool* values,
    ukv_error_t* error);

/**
 * @brief The primary "getter" interface, that reads from "HEAD" state.
 * If a fail had occured, @p `error` will be set to non-NULL.
 * If a key wasn't found in target collection, the value is empty.
 * @see `ukv_txn_put` for transactional consistent lookups.
 *
 * @param db                Already open database instance, @see `ukv_open`.
 * @param keys              Array of keys in one or more collections.
 * @param keys_count        Number of elements in @p `keys`.
 * @param columns           Array of columns with 0, 1 or `keys_count`
 *                          elements describing the locations of keys.
 *                          If NULL is passed, the default collection
 *                          is assumed.
 * @param columns_count     Number of elements in @p `columns`.
 * @param options           Write options.
 * @param arena             Points to a memory region that we use during
 *                          this request. If it's too small (@p `arena_length`),
 *                          we `realloc` a new buffer. You can't pass a memory
 *                          allocated by a third-party allocator.
 *                          During the first request you pass a `NULL`,
 *                          we allocate that buffer, put found values in it and
 *                          return you a pointer. You can later reuse it for future
 *                          requests, or `free` it via `ukv_get_free`.
 * @param arena_length      Current size of @p `arena`.
 * @param values            Array of pointers to the first byte of each value.
 * @param values_lengths    Array of lengths of buffers, storing the @p `values`.
 * @param error             The error message to be handled by callee.
 */
void ukv_get(
    // Inputs:
    ukv_t const db,
    ukv_key_t const* keys,
    size_t const keys_count,
    ukv_column_t const* columns,
    size_t const columns_count,
    ukv_options_read_t const options,

    // In-outs:
    void** arena,
    size_t* arena_length,

    // Outputs:
    ukv_val_ptr_t* values,
    ukv_val_len_t* values_lengths,
    ukv_error_t* error);

/*********************************************************/
/*****************	Columns Management	  ****************/
/*********************************************************/

/**
 * @brief Upserts a new column into DB.
 * As of now, only the default column is available
 * and @p `column_name` is ignored across the interaface.
 */
void ukv_column_upsert(
    // Inputs:
    ukv_t const db,
    char const* column_name,
    // Outputs:
    ukv_column_t* column,
    ukv_error_t* error);

void ukv_column_remove(
    // Inputs:
    ukv_t const db,
    char const* column_name,
    // Outputs:
    ukv_error_t* error);

/**
 * @brief Pulls metadata, mostly for logging and customer support.
 */
void ukv_status(ukv_t db,
                size_t* version_major,
                size_t* version_minor,
                size_t* memory_usage,
                size_t* disk_usage,
                size_t* active_transactions,
                ukv_error_t* error);

/*********************************************************/
/*****************		Transactions	  ****************/
/*********************************************************/

/**
 * @brief
 *
 * @param db
 * @param sequence_number
 * @param txn May be pointing to an existing transaction. In that case, it's reset to new @p `sequence_number`.
 * @param error
 */
void ukv_txn_begin(
    // Inputs:
    ukv_t const db,
    size_t const sequence_number,
    // Outputs:
    ukv_txn_t* txn,
    ukv_error_t* error);

void ukv_txn_put(
    // Inputs:
    ukv_txn_t const txn,
    ukv_key_t const* keys,
    size_t const keys_count,
    ukv_column_t const* columns,
    size_t const columns_count,
    //
    ukv_val_ptr_t const* values,
    ukv_val_len_t const* values_lengths,
    // Outputs:
    ukv_error_t* error);

void ukv_txn_get(
    // Inputs:
    ukv_t const db,
    ukv_key_t const* keys,
    size_t const keys_count,
    ukv_column_t const* columns,
    size_t const columns_count,

    // In-outs:
    void** arena,
    size_t* arena_length,

    // Outputs:
    ukv_val_ptr_t* values,
    ukv_val_len_t* values_lengths,
    ukv_error_t* error);

/**
 * On success, the transaction content is wiped clean.
 * On failure, the entire transaction state is preserved to allow retries.
 */
void ukv_txn_commit(
    // Inputs:
    ukv_txn_t const txn,
    // Outputs:
    ukv_error_t* error);

/*********************************************************/
/*****************		  Iterators	      ****************/
/*********************************************************/

void ukv_iter_make(ukv_column_t const column, ukv_iter_t*, ukv_error_t* error);

void ukv_iter_seek(ukv_iter_t const iter, ukv_key_t key, ukv_error_t* error);

void ukv_iter_advance(ukv_iter_t const iter, size_t const count_to_skip, ukv_error_t* error);

void ukv_iter_get_key(ukv_iter_t const iter, ukv_key_t*, ukv_error_t* error);

void ukv_iter_get_value_size(ukv_iter_t const iter, size_t* length, size_t* arena_length, ukv_error_t* error);

/**
 * @brief Fetches currently targeted value from disk.
 * ! The entry received from yere should NOT
 * ! be deallocated via `ukv_get_free`.
 */
void ukv_iter_get_value(ukv_iter_t const iter,
                        // In-outs:
                        void** arena,
                        size_t* arena_length,
                        // Outputs:
                        ukv_val_ptr_t* values,
                        ukv_val_len_t* values_lengths,
                        ukv_error_t* error);

/*********************************************************/
/*****************	  Memory Management   ****************/
/*********************************************************/

/**
 * @brief A function to be used in conjunction with:
 * > `ukv_get`
 * > `ukv_txn_get`
 * to deallocate and return memory to UnumDB and OS.
 */
void ukv_get_free(ukv_t const db, void*, size_t);

void ukv_txn_free(ukv_t const db, ukv_txn_t const txn);

void ukv_iter_free(ukv_t const db, ukv_iter_t const iter);

void ukv_column_free(ukv_t const db, ukv_column_t const column);

/**
 * @brief A function to be called after any function failure,
 * that resulted in a non-NULL `ukv_error_t`.
 */
void ukv_error_free(ukv_t, ukv_error_t);

/**
 * @brief Closes the DB and deallocates the state.
 */
void ukv_free(ukv_t db);

#ifdef __cplusplus
} /* end extern "C" */
#endif
