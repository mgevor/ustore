/**
 * @file backend_rocksdb.cpp
 * @author Ashot Vardanian
 *
 * @brief Embedded Persistent Key-Value Store on top of @b RocksDB.
 * It natively supports ACID transactions and iterators (range queries)
 * and is implemented via @b Log-Structured-Merge-Tree. This makes RocksDB
 * great for write-intensive operations. It's already a common engine
 * choice for various Relational Database, built on top of it.
 * Examples: Yugabyte, TiDB, and, optionally: Mongo, MySQL, Cassandra, MariaDB.
 *
 * @section @b `PlainTable` vs `BlockBasedTable` Format
 * We use fixed-length integer keys, which are natively supported by `PlainTable`.
 * It, however, doesn't support @b non-prefix-based-`Seek()` in scans.
 * Moreover, not being the default variant, its significantly less optimized,
 * so after numerous tests we decided to stick to `BlockBasedTable`.
 * https://github.com/facebook/rocksdb/wiki/PlainTable-Format
 */

#include <rocksdb/db.h>
#include <rocksdb/utilities/options_util.h>
#include <rocksdb/utilities/transaction_db.h>

#include "ukv/ukv.h"
#include "helpers.hpp"

using namespace unum::ukv;
using namespace unum;

using rocks_status_t = rocksdb::Status;
using rocks_db_t = rocksdb::TransactionDB;
using rocks_value_t = rocksdb::PinnableSlice;
using rocks_txn_ptr_t = rocksdb::Transaction*;
using rocks_col_ptr_t = rocksdb::ColumnFamilyHandle*;
using value_uptr_t = std::unique_ptr<rocks_value_t>;
using rocks_iter_uptr_t = std::unique_ptr<rocksdb::Iterator>;

/*********************************************************/
/*****************   Structures & Consts  ****************/
/*********************************************************/

ukv_collection_t ukv_default_collection_k = NULL;
ukv_val_len_t ukv_val_len_missing_k = std::numeric_limits<ukv_val_len_t>::max();
ukv_key_t ukv_key_unknown_k = std::numeric_limits<ukv_key_t>::max();

struct rocks_db_wrapper_t {
    std::vector<rocks_col_ptr_t> columns;
    std::unique_ptr<rocks_db_t> db;
};

inline rocksdb::Slice to_slice(ukv_key_t const& key) noexcept {
    return {reinterpret_cast<char const*>(&key), sizeof(ukv_key_t)};
}

inline rocksdb::Slice to_slice(value_view_t value) noexcept {
    return {reinterpret_cast<const char*>(value.begin()), value.size()};
}

inline value_uptr_t make_value(ukv_error_t* c_error) noexcept {
    value_uptr_t value_uptr;
    try {
        value_uptr = std::make_unique<rocks_value_t>();
    }
    catch (...) {
        *c_error = "Fail to allocate value";
    }
    return value_uptr;
}

bool export_error(rocks_status_t const& status, ukv_error_t* c_error) {
    if (status.ok())
        return false;

    if (status.IsCorruption())
        *c_error = "Failure: DB Corrpution";
    else if (status.IsIOError())
        *c_error = "Failure: IO  Error";
    else if (status.IsInvalidArgument())
        *c_error = "Failure: Invalid Argument";
    else
        *c_error = "Failure";
    return true;
}

void ukv_open([[maybe_unused]] char const* c_config, ukv_t* c_db, ukv_error_t* c_error) {
    rocks_db_wrapper_t* db_wrapper = new rocks_db_wrapper_t;
    std::vector<rocksdb::ColumnFamilyDescriptor> column_descriptors;
    rocksdb::Options options;
    rocksdb::ConfigOptions config_options;

    rocks_status_t status = rocksdb::LoadLatestOptions(config_options, "./tmp/rocksdb/", &options, &column_descriptors);
    if (column_descriptors.empty())
        column_descriptors.push_back({ROCKSDB_NAMESPACE::kDefaultColumnFamilyName, rocksdb::ColumnFamilyOptions()});

    rocks_db_t* db = nullptr;
    options.create_if_missing = true;
    status = rocks_db_t::Open(options,
                              rocksdb::TransactionDBOptions(),
                              "./tmp/rocksdb/",
                              column_descriptors,
                              &db_wrapper->columns,
                              &db);

    db_wrapper->db = std::unique_ptr<rocks_db_t>(db);

    if (!status.ok())
        *c_error = "Open Error";
    *c_db = db_wrapper;
}

void write_one( //
    rocks_db_wrapper_t* db_wrapper,
    rocks_txn_ptr_t txn,
    write_tasks_soa_t const& tasks,
    ukv_size_t const,
    rocksdb::WriteOptions const& options,
    ukv_error_t* c_error) {

    auto task = tasks[0];
    auto key = to_slice(task.key);
    auto col = task.col ? reinterpret_cast<rocks_col_ptr_t>(task.col) : db_wrapper->db->DefaultColumnFamily();

    rocks_status_t status;
    if (txn)
        status = task.is_deleted() ? txn->SingleDelete(col, key) : txn->Put(col, key, to_slice(task.view()));
    else
        status = task.is_deleted() ? db_wrapper->db->SingleDelete(options, col, key)
                                   : db_wrapper->db->Put(options, col, key, to_slice(task.view()));

    export_error(status, c_error);
}

void write_many( //
    rocks_db_wrapper_t* db_wrapper,
    rocks_txn_ptr_t txn,
    write_tasks_soa_t const& tasks,
    ukv_size_t const n,
    rocksdb::WriteOptions const& options,
    ukv_error_t* c_error) {

    if (txn) {
        for (ukv_size_t i = 0; i != n; ++i) {
            write_task_t task = tasks[i];
            auto col = task.col ? reinterpret_cast<rocks_col_ptr_t>(task.col) : db_wrapper->db->DefaultColumnFamily();
            auto key = to_slice(task.key);
            task.is_deleted() ? txn->Delete(col, key) : txn->Put(col, key, to_slice(task.view()));
        }
        return;
    }

    rocksdb::WriteBatch batch;
    for (ukv_size_t i = 0; i != n; ++i) {
        write_task_t task = tasks[i];
        rocks_col_ptr_t col =
            task.col ? reinterpret_cast<rocks_col_ptr_t>(task.col) : db_wrapper->db->DefaultColumnFamily();
        auto key = to_slice(task.key);
        task.is_deleted() ? batch.Delete(col, key) : batch.Put(col, key, to_slice(task.view()));
    }

    rocks_status_t status = db_wrapper->db->Write(options, &batch);
    export_error(status, c_error);
}

void ukv_write( //
    ukv_t const c_db,
    ukv_txn_t const c_txn,
    ukv_size_t const c_tasks_count,

    ukv_collection_t const* c_cols,
    ukv_size_t const c_cols_stride,

    ukv_key_t const* c_keys,
    ukv_size_t const c_keys_stride,

    ukv_val_ptr_t const* c_vals,
    ukv_size_t const c_vals_stride,

    ukv_val_len_t const* c_offs,
    ukv_size_t const c_offs_stride,

    ukv_val_len_t const* c_lens,
    ukv_size_t const c_lens_stride,

    ukv_options_t const c_options,
    ukv_arena_t*,
    ukv_error_t* c_error) {

    rocks_db_wrapper_t* db_wrapper = reinterpret_cast<rocks_db_wrapper_t*>(c_db);
    rocks_txn_ptr_t txn = reinterpret_cast<rocks_txn_ptr_t>(c_txn);
    strided_iterator_gt<ukv_collection_t const> cols {c_cols, c_cols_stride};
    strided_iterator_gt<ukv_key_t const> keys {c_keys, c_keys_stride};
    strided_iterator_gt<ukv_val_ptr_t const> vals {c_vals, c_vals_stride};
    strided_iterator_gt<ukv_val_len_t const> offs {c_offs, c_offs_stride};
    strided_iterator_gt<ukv_val_len_t const> lens {c_lens, c_lens_stride};
    write_tasks_soa_t tasks {cols, keys, vals, offs, lens};

    rocksdb::WriteOptions options;
    if (c_options & ukv_option_write_flush_k)
        options.sync = true;

    try {
        auto func = c_tasks_count == 1 ? &write_one : &write_many;
        func(db_wrapper, txn, tasks, c_tasks_count, options, c_error);
    }
    catch (...) {
        *c_error = "Write Failure";
    }
}

void read_one( //
    rocks_db_wrapper_t* db_wrapper,
    rocks_txn_ptr_t txn,
    read_tasks_soa_t const& tasks,
    ukv_size_t const,
    rocksdb::ReadOptions const& options,
    ukv_val_len_t** c_found_lengths,
    ukv_val_ptr_t* c_found_values,
    stl_arena_t& arena,
    ukv_error_t* c_error) {

    read_task_t task = tasks[0];
    rocks_col_ptr_t col =
        task.col ? reinterpret_cast<rocks_col_ptr_t>(task.col) : db_wrapper->db->DefaultColumnFamily();

    auto value_uptr = make_value(c_error);
    rocks_value_t& value = *value_uptr.get();

    auto key = to_slice(task.key);
    rocks_status_t status = txn ? txn->Get(options, col, key, &value) : db_wrapper->db->Get(options, col, key, &value);

    if (!status.IsNotFound())
        if (export_error(status, c_error))
            return;

    auto bytes_in_value = static_cast<ukv_size_t>(value.size());
    auto exported_len = status.IsNotFound() ? ukv_val_len_missing_k : bytes_in_value;
    auto tape = prepare_memory(arena.output_tape, sizeof(ukv_size_t) + bytes_in_value, c_error);
    if (*c_error)
        return;
    std::memcpy(tape, &exported_len, sizeof(ukv_size_t));
    std::memcpy(tape + sizeof(ukv_size_t), value.data(), bytes_in_value);

    *c_found_lengths = reinterpret_cast<ukv_val_len_t*>(tape);
    *c_found_values = reinterpret_cast<ukv_val_ptr_t>(tape + sizeof(ukv_size_t));
}

void read_many( //
    rocks_db_wrapper_t* db_wrapper,
    rocks_txn_ptr_t txn,
    read_tasks_soa_t const& tasks,
    ukv_size_t const n,
    rocksdb::ReadOptions const& options,
    ukv_val_len_t** c_found_lengths,
    ukv_val_ptr_t* c_found_values,
    stl_arena_t& arena,
    ukv_error_t* c_error) {

    std::vector<rocks_col_ptr_t> cols(n);
    std::vector<rocksdb::Slice> keys(n);
    std::vector<std::string> vals(n);
    for (ukv_size_t i = 0; i != n; ++i) {
        read_task_t task = tasks[i];
        cols[i] = task.col ? reinterpret_cast<rocks_col_ptr_t>(task.col) : db_wrapper->db->DefaultColumnFamily();
        keys[i] = to_slice(task.key);
    }

    std::vector<rocks_status_t> statuses =
        txn ? txn->MultiGet(options, cols, keys, &vals) : db_wrapper->db->MultiGet(options, cols, keys, &vals);

    // 1. Estimate the total size
    ukv_size_t total_bytes = sizeof(ukv_val_len_t) * n;
    for (ukv_size_t i = 0; i != n; ++i)
        total_bytes += vals[i].size();

    // 2. Allocate a tape for all the values to be fetched
    byte_t* tape = prepare_memory(arena.output_tape, total_bytes, c_error);
    if (*c_error)
        return;

    // 3. Fetch the data
    ukv_val_len_t* lens = reinterpret_cast<ukv_val_len_t*>(tape);
    ukv_size_t exported_bytes = sizeof(ukv_val_len_t) * n;
    *c_found_lengths = lens;
    *c_found_values = reinterpret_cast<ukv_val_ptr_t>(tape + exported_bytes);

    for (ukv_size_t i = 0; i != n; ++i) {
        auto bytes_in_value = vals[i].size();
        if (bytes_in_value) {
            std::memcpy(tape + exported_bytes, vals[i].data(), bytes_in_value);
            lens[i] = static_cast<ukv_val_len_t>(bytes_in_value);
            exported_bytes += bytes_in_value;
        }
        else
            lens[i] = ukv_val_len_missing_k;
    }
}

void ukv_read( //
    ukv_t const c_db,
    ukv_txn_t const c_txn,
    ukv_size_t const c_tasks_count,

    ukv_collection_t const* c_cols,
    ukv_size_t const c_cols_stride,

    ukv_key_t const* c_keys,
    ukv_size_t const c_keys_stride,

    ukv_options_t const c_options,

    ukv_val_len_t** c_found_lengths,
    ukv_val_ptr_t* c_found_values,

    ukv_arena_t* c_arena,
    ukv_error_t* c_error) {

    if (c_txn && !(c_options & ukv_option_read_transparent_k)) {
        *c_error = "RocksDB only supports transparent reads!";
        return;
    }

    rocks_db_wrapper_t* db_wrapper = reinterpret_cast<rocks_db_wrapper_t*>(c_db);
    rocks_txn_ptr_t txn = reinterpret_cast<rocks_txn_ptr_t>(c_txn);
    strided_iterator_gt<ukv_collection_t const> cols_stride {c_cols, c_cols_stride};
    strided_iterator_gt<ukv_key_t const> keys_stride {c_keys, c_keys_stride};
    read_tasks_soa_t tasks {cols_stride, keys_stride};
    stl_arena_t& arena = *cast_arena(c_arena, c_error);
    rocksdb::ReadOptions options;
    if (txn)
        options.snapshot = txn->GetSnapshot();
    try {
        auto func = c_tasks_count == 1 ? &read_one : &read_many;
        func(db_wrapper, txn, tasks, c_tasks_count, options, c_found_lengths, c_found_values, arena, c_error);
    }
    catch (...) {
        *c_error = "Read Failure";
    }
}

void ukv_scan( //
    ukv_t const c_db,
    ukv_txn_t const c_txn,
    ukv_size_t const c_min_tasks_count,

    ukv_collection_t const* c_cols,
    ukv_size_t const c_cols_stride,

    ukv_key_t const* c_min_keys,
    ukv_size_t const c_min_keys_stride,

    ukv_size_t const* c_scan_lengths,
    ukv_size_t const c_scan_lengths_stride,

    ukv_options_t const c_options,

    ukv_key_t** c_found_keys,
    ukv_val_len_t** c_found_lengths,

    ukv_arena_t* c_arena,
    ukv_error_t* c_error) {

    if (c_txn && !(c_options & ukv_option_read_transparent_k)) {
        *c_error = "RocksDB only supports transparent reads!";
        return;
    }

    stl_arena_t& arena = *cast_arena(c_arena, c_error);
    if (*c_error)
        return;

    rocks_db_wrapper_t* db_wrapper = reinterpret_cast<rocks_db_wrapper_t*>(c_db);
    rocks_txn_ptr_t txn = reinterpret_cast<rocks_txn_ptr_t>(c_txn);
    strided_iterator_gt<ukv_collection_t const> cols {c_cols, c_cols_stride};
    strided_iterator_gt<ukv_key_t const> keys {c_min_keys, c_min_keys_stride};
    strided_iterator_gt<ukv_size_t const> lengths {c_scan_lengths, c_scan_lengths_stride};
    scan_tasks_soa_t tasks {cols, keys, lengths};

    rocksdb::ReadOptions options;
    options.fill_cache = false;
    ukv_size_t keys_bytes = sizeof(ukv_key_t) * c_min_tasks_count;
    ukv_size_t val_len_bytes = sizeof(ukv_val_len_t) * c_min_tasks_count;
    byte_t* tape = prepare_memory(arena.output_tape, keys_bytes + val_len_bytes, c_error);
    if (*c_error)
        return;

    ukv_key_t* scanned_keys = reinterpret_cast<ukv_key_t*>(tape);
    ukv_val_len_t* scanned_lens = reinterpret_cast<ukv_val_len_t*>(tape + keys_bytes);
    *c_found_keys = scanned_keys;
    *c_found_lengths = scanned_lens;

    for (ukv_size_t i = 0; i != c_min_tasks_count; ++i) {
        scan_task_t task = tasks[i];
        auto col = task.col ? reinterpret_cast<rocks_col_ptr_t>(task.col) : db_wrapper->db->DefaultColumnFamily();

        rocks_iter_uptr_t it;
        try {
            it = txn ? rocks_iter_uptr_t(txn->GetIterator(options, col))
                     : rocks_iter_uptr_t(db_wrapper->db->NewIterator(options, col));
        }
        catch (...) {
            *c_error = "Fail To Create Iterator";
        }
        it->Seek(to_slice(task.min_key));
        for (; it->Valid() && i != task.length; i++, it->Next()) {
            std::memcpy(&scanned_keys[i], it->key().data(), sizeof(ukv_key_t));
            scanned_lens[i] = static_cast<ukv_val_len_t>(it->value().size());
        }
    }
}

void ukv_collection_open( //
    ukv_t const c_db,
    ukv_str_view_t c_col_name,
    ukv_str_view_t,
    ukv_collection_t* c_col,
    ukv_error_t* c_error) {

    rocks_db_wrapper_t* db_wrapper = reinterpret_cast<rocks_db_wrapper_t*>(c_db);
    if (!c_col_name) {
        *c_col = db_wrapper->db->DefaultColumnFamily();
        return;
    }

    for (auto handle : db_wrapper->columns) {
        if (handle && handle->GetName() == c_col_name) {
            *c_col = handle;
            return;
        }
    }

    rocks_col_ptr_t col = nullptr;
    rocks_status_t status = db_wrapper->db->CreateColumnFamily(rocksdb::ColumnFamilyOptions(), c_col_name, &col);
    if (export_error(status, c_error)) {
        db_wrapper->columns.push_back(col);
        *c_col = col;
    }
}

void ukv_collection_remove( //
    ukv_t const c_db,
    ukv_str_view_t c_col_name,
    ukv_error_t* c_error) {

    rocks_db_wrapper_t* db_wrapper = reinterpret_cast<rocks_db_wrapper_t*>(c_db);
    for (auto handle : db_wrapper->columns) {
        if (c_col_name == handle->GetName()) {
            rocks_status_t status = db_wrapper->db->DestroyColumnFamilyHandle(handle);
            if (export_error(status, c_error))
                return;
        }
    }
}

void ukv_control( //
    ukv_t const,
    ukv_str_view_t,
    ukv_str_view_t* c_response,
    ukv_error_t* c_error) {
    *c_response = NULL;
    *c_error = "Controls aren't supported in this implementation!";
}

void ukv_txn_begin(
    // Inputs:
    ukv_t const c_db,
    ukv_size_t const,
    ukv_options_t const,
    // Outputs:
    ukv_txn_t* c_txn,
    ukv_error_t* c_error) {

    rocks_db_t* db = reinterpret_cast<rocks_db_wrapper_t*>(c_db)->db.get();
    rocks_txn_ptr_t txn = reinterpret_cast<rocks_txn_ptr_t>(*c_txn);
    rocksdb::TransactionOptions options;
    options.set_snapshot = true;
    txn = db->BeginTransaction(rocksdb::WriteOptions(), options, txn);
    if (!txn)
        *c_error = "Couldn't start a transaction!";
    else
        *c_txn = txn;
}

void ukv_txn_commit( //
    ukv_txn_t const c_txn,
    ukv_options_t const,
    ukv_error_t* c_error) {

    rocks_txn_ptr_t txn = reinterpret_cast<rocks_txn_ptr_t>(c_txn);
    rocks_status_t status = txn->Commit();
    export_error(status, c_error);
}

void ukv_arena_free(ukv_t const, ukv_arena_t c_arena) {
    if (!c_arena)
        return;
    stl_arena_t& arena = *reinterpret_cast<stl_arena_t*>(c_arena);
    delete &arena;
}

void ukv_txn_free(ukv_t const, ukv_txn_t) {
}

void ukv_collection_free(ukv_t const, ukv_collection_t const) {
}

void ukv_free(ukv_t c_db) {
    if (!c_db)
        return;
    rocks_db_wrapper_t* db_wrapper = reinterpret_cast<rocks_db_wrapper_t*>(c_db);
    delete db_wrapper;
}

void ukv_error_free(ukv_error_t const) {
}