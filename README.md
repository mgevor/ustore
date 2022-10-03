# Universal Keys & Values

⚠️ Under active development! Not all APIs are stable!

## The BLAS of CRUD

![Universal Key Values by Unum](assets/UKV.png)

Imagine having a standardized cross-lingual interface for all your things "Data":

* Storing binary blobs
* Building up graphs & indexes
* Querying structured documents
* [ACID](https://en.wikipedia.org/wiki/ACID) transactions across tables, docs & graphs
* [Apache Arrow](https://arrow.apache.org/) interop and [Flight RPC](https://arrow.apache.org/docs/format/Flight.html)
* Familiar high-level [drivers](#frontends) for tabular & graph analytics
* Handling JSON, [BSON](https://www.mongodb.com/json-and-bson), [MsgPack](https://msgpack.org/index.html)
* [JSON-Pointers](https://datatracker.ietf.org/doc/html/rfc6901) & [Field-level Patches](https://datatracker.ietf.org/doc/html/rfc6902), no custom Query Languages
* Packing Tensors for [PyTorch](https://pytorch.org/) and [TensorFlow](tensorflow.org)

UKV does just that, abstracting away the implementation from the user.
In under 20K LOC you get a reference implementation in C++, support for any classical backend, and bindings for [Python](#python), [GoLang](#golang), [Java](#java).
You can combine every [engine](#engines) with every modality, [frontend](#frontends) and distribution form:

| Engine  | Modality | Distribution | Frontend  |
| :------ | :------- | :----------- | :-------- |
|         |          |              |           |
| STL     | Blobs    | Embedded     | C and C++ |
| LevelDB | Docs     | Standalone   | Python    |
| RocksDB | Graphs   | Distributed <sup>*coming*</sup>  | GoLang <sup>*in-progress*</sup>   |
| UnumDB  |          |              | Java <sup>*in-progress*</sup>     |

This would produce hundreds of binaries for all kinds of use cases, like:

* Python, GoLang, Java and other high-level bindings for [RocksDB](rocksdb.org) and [LevelDB](https://github.com/google/leveldb).
* Performant embedded store in the foundation of your in-house storage solution.
* Document store, that is simpler and faster than putting JSONs in MongoDB or Postgres.
* Graph database, with the feel of [NetworkX](https://networkx.org), ~~soon~~ speed of [GunRock](http://gunrock.github.io) and scale of [Hadoop](https://hadoop.apache.org).
* Low-latency media storage for games, CDNs and ML/BI pipelines.

But more importantly, if you choose backends that support transactions and collections, you can get an all-in one solution:

![UKV Monolithic Data-lake](assets/UKV_Combo.png)

It is normal to have a separate Postgres for your transactional data, a MongoDB for your large flexible-schema document collections, a Neo4J instance for your graphs, and an S3 storage bucket for your media data, all serving the different data needs of a single business.

> Example: a social network, storing passwords in Postgres, posts in MongoDB, user relations in Neo4J and post attachments in S3.

So when the data is updated, you have to apply changes across all those instances, manually rolling them back if one of the parts failed.
Needless to say, every system has a different API, different guarantees, and runtime constraints.
UKV provides something far more uniform, simple, and performant *with the right backend*.
When picking the UnumDB backend, we bring our entire IO stack, bypassing the Linux kernel for storage and networking operations.
This yields speedups not just for small-ish OLTP and mid-size OLAP, but even streaming-out Gigabyte-sized videos.
One ~~ring~~ data-lake to rule them all.

## Engines

Backends differ in their functionality and purposes.
The underlying embedded key value stores include:

| Name            | Approximate<br/>Speed |      OSes       | [ACID][3]<br/>Transactions | Auxiliary<br/>Collections | Persistent | [Snapshots][2] | [Watching][1]<br/>Reads |
| :-------------- | :-------------------: | :-------------: | :------------------------: | :-----------------------: | :--------: | :------------: | :---------------------: |
| STL *in-memory* |        **10x**        | POSIX + Windows |             ✅              |             ✅             |     ❌      |       ❌        |            ✅            |
| LevelDB         |         0.5x          | POSIX + Windows |             ❌              |             ❌             |     ✅      |       ❌        |            ❌            |
| RocksDB         |          1x           | POSIX + Windows |             ✅              |             ✅             |     ✅      |       ✅        |            ✅            |
| UnumDB          |        **5x**         |      Linux      |             ✅              |             ✅             |     ✅      |       ✅        |            ✅            |

The STL backend originally served educational purposes, yet, with a proper web-server implementation, is comparable to other in-memory stores like Redis, MemCached or ETCD.
LevelDB is Key-Value stored designed at Google and extensively adopted across the industry, thanks to its simplicity.
RocksDB improves over LevelDB, extending its functionality with transactions, named collections, and higher performance.
All of those backends were [benchmarked for weeks](https://unum.cloud/ucsb) using [UCSB](https://github.com/unum-cloud/ucsb), so you can choose the best stack for you specific use case.

![UCSB 10 TB Results](https://unum.cloud/assets/post/2022-09-13-ucsb-10tb/ucsb-10tb-duration.png)

[1]: https://redis.io/commands/watch/
[2]: https://github.com/facebook/rocksdb/wiki/Snapshot
[3]: https://en.wikipedia.org/wiki/ACID

## Frontends

Currently, at Proof-of-Concept stage, we support only the essential functionality in select programming languages.

| Name        | Transact | Collections | Batches | Docs  | Graphs | Copies |
| :---------- | :------: | :---------: | :-----: | :---: | :----: | :----: |
| C++ ³       |    ✅     |      ✅      |    ✅    |   ✅   |   ✅    |   0    |
| Python ¹ ³  |    ✅     |      ✅      |    ✅    |   ✅   |   ✅    |  0-1   |
| GoLang      |    ✅     |      ✅      |    ✅    |   ❌   |   ❌    |   1    |
| Java        |    ✅     |      ✅      |    ❌    |   ❌   |   ❌    |   1    |
| C# ²        |    ❌     |      ❌      |    ❌    |   ❌   |   ❌    |        |
| REST API ²  |    ✅     |      ✅      |    ✅    |   ✅   |   ❌    |        |
| Arrow RPC ² |    ✅     |      ✅      |    ✅    |   ✅   |   ❌    |        |
| Wolfram ¹ ² |    ❌     |      ✅      |    ✅    |   ❌   |   ✅    |        |

* Copies: Number of re-allocations/conversions per byte.
* ¹: Support tensor lookups and media data.
* ²: Missing, to be implemented.
* ³: Supports tabular Arrow exports.

### Python

Current implementation relies on [PyBind11](https://github.com/pybind/pybind11).
It's feature-rich, but not very performant, supporting:

* Named Collections
* ACID Transactions
* Single & Batch Operations
* Tensors support via [Buffer Protocol](https://docs.python.org/3/c-api/buffer.html)
* [NetworkX](https://networkx.org)-like interface for Graphs
* [Pandas](https://pandas.pydata.org)-like interface for Document collections ~~in-progress~~

Using it can be as easy as:

```python
import ukv.stl as ukv
# import ukv.level as ukv
# import ukv.rocks as ukv

db = ukv.DataBase()
db[42] = 'purpose of life'.encode()
db['sub-collection'][0] = db[42]
del db[42]
assert len(db['sub-collection'][0]) == 15
```

All familiar Pythonic stuff!

### Java

These bindings are implemented via [Java Native Interface](https://docs.oracle.com/javase/8/docs/technotes/guides/jni/spec/jniTOC.html).
This interface is more performant than Python, but is not feature complete yet.
It mimics native `HashMap` and `Dictionary` classes, but has no support for batch operations yet.

```java
DataBase db = new DataBase("");
db.put(42, "purpose of life".getBytes());
assert db.get(42) == "purpose of life".getBytes() : "Big surprise";
db.close();
```

All `get` requests cause memory allocations in Java Runtime and export data into native Java types.
Most `set` requests will simply cast and forward values without additional copies.
Aside from opening and closing this class is **thread-safe** for higher interop with other Java-based tools.

Implementation follows the ["best practices" defined by IBM](https://developer.ibm.com/articles/j-jni/).

### GoLang

GoLang bindings are implemented using [cGo](https://pkg.go.dev/cmd/cgo).
The language lacks operator and function overloads, so we can't mimic native collections.
Instead we mimic the interfaces of most commonly used ORMs.

```go
db := DataBase{}
db.Reconnect()
db.Set(42, &[]byte{4, 2})
db.Get(42)
```

Implementation-wise, GoLang variant performs `memcpy`s on essentially every call.
As GoLang has no exceptions in the classical OOP sense, most functions return multiple values, error being the last one in each pack.
Batch lookup operations are implemented via channels sending slices, to avoid reallocations.

<details>
<summary>JavaScript</summary>

* Node.js
* V8
* Deno
* [`bun:ffi`](https://twitter.com/jarredsumner/status/1521527222514774017)
</details>

<details>
<summary>Rust</summary>

Rust implementation is designed to support:

* Named Collections
* ACID Transactions
* Single & Batch Operations
* [Apache DataFusion](https://arrow.apache.org/datafusion/) `TableProvider` for SQL

Using it should be, again, familiar, as it mimics [`std::collections`](https://doc.rust-lang.org/std/collections/hash_map/struct.HashMap.html):

```rust
let mut db = DataBase::new();
if db.contains_key(&42) {
    db.remove(&42);
    db.insert(43, "New Meaning".to_string());
}
for (key, value) in &db {
    println!("{key}: \"{value}\"");
}
db.clear();
```
</details>

<details>
<summary>RESTful API & Clients</summary>

We implement a REST server using `Boost.Beast` and the underlying `Boost.Asio`, as the go-to Web-Dev libraries in C++.
To test the REST API, `./src/run_rest.sh` and then cURL into it:

```sh
curl -X PUT \
  -H "Accept: Application/json" \
  -H "Content-Type: application/octet-stream" \
  0.0.0.0/8080/one/42?col=sub \
  -d 'purpose of life'

curl -i \
  -H "Accept: application/octet-stream" \
  0.0.0.0/8080/one/42?col=sub
```

The [`OneAPI` specification](/openapi.yaml) documentation is in-development.
</details>

## Installation

* For Python: `pip install ukv`
* For Conan: `conan install ukv`
* For Docker image: `docker run --rm --name test_ukv -p 38709:38709 unum/ukv`

## Development

To build the whole project:

```sh
cmake \
    -DUKV_BUILD_PYTHON=1 \
    -DUKV_BUILD_TESTS=1 \
    -DUKV_BUILD_BENCHMARKS=1 \
    -DUKV_BUILD_FLIGHT_RPC=1 . && \
    make -j16
```

For Flight RPC, Apache Arrow must be preinstalled.
To build language bindings:

```sh
./python/run.sh
./java/run.sh
./golang/run.sh
```

Building Flight RPC Docker Image:

```sh
docker build -t ukv .
```

Building Conan package, without installing it:

```sh
conan create . ukv/testing --build=missing
```

## Similar Projects

* [EJDB](https://github.com/Softmotions/ejdb) is a pure C embeddable JSON database engine.
  * **Pros**:
    * C11 API.
    * Many bindings, including JS and *currently* better Java support.
    * MacOS and Windows support, that we *currently* don't prioritize.
  * **Cons**:
    * Very slow.
    * No ACID transactions.
    * No way to swap the backend "engine".
    * No support for non-document modalities, like Graphs.
    * No support for batch operations.
    * Bindings are just string exchange interfaces.
* [SurrealDB](https://github.com/surrealdb/surrealdb) is a scalable, distributed, collaborative, document-graph database, for the realtime web.
  * **Pros**:
    * Many bindings, including JS.
    * MacOS and Windows support, that we *currently* don't prioritize.
    * User permissions management functionality.
  * **Cons**:
    * Very slow.
    * No way to swap the backend "engine".
    * Custom string-based query language.
    * Bindings are just string exchange interfaces.
    * No C API [yet](https://surrealdb.com/docs/integration/libraries/c).

## Presets, Limitations and FAQ

<details>
<summary>Keys are 64-bit integers. String keys are possible, but discouraged on hot-paths</summary>

Using variable length keys forces numerous limitations on the design of a Key-Value store.
Besides slow comparisons it means solving the "persistent space allocation" problem twice - for both keys and values.

The recommended approach to dealing with string keys is:

1. Choose a mechanism to generate unique integer keys (UID). Ex: monotonically increasing values.
2. Use "paths" modality to build-up a persistent hash-map of strings to UIDs.
3. Use those UIDs to address the rest of the data in binary, document and graph modalities.

Once support of 16-byte integers becomes industry standard, we will add support for UUID keys.

</details>

<details>
<summary>Values are binary strings under 4 GB long</summary>

</details>


<details>
<summary>Transactions are ACI(D) by-default</summary>

Atomicity is guaranteed.
Even on non-transactional writes - either all updates pass or all fail.

Consistency is implemented in the strongest form - tracking all key and metadata lookups by default.
Just like with `WATCH` verb in Redis, we will track collisions between all the touched keys.
To avoid such checks - pass the `ukv_option_transaction_dont_watch_k` option.

Isolation is guaranteed, but may be implemented differently, depending on backend.
In-memory systems generally prefer "locking" over "multi-versioning".

Durability doesn't apply to in-memory systems by definition.
In hybrid or persistent systems we prefer to disable it by default.
Almost every DBMS that builds on top of KVS prefers to implement its own durability mechanism.
Even more so in distributed databases, where three separate Write Ahead Logs may exist:

* In KVS,
* In DBMS,
* In Distributed Consensus implementation.

If you still need durability, flush writes with `ukv_option_write_flush_k`.

</details>

<details>
<summary>Not all engines support ordered scans</summary>

</details>

<details>
<summary>Why not use LevelDB interface?</summary>

... Which was also adopted by RocksDB.

1. Dynamic polymorphism.
2. Dependance on Standard Templates Library containers.
3. No support for custom allocators.

These and other problems mean that interface can't be portable, ABI-safe or performant.
</details>

<details>
<summary>Why mix Docs and Graphs in one DBMS?</summary>

There are too extremes these days: consistency and scalability, especially when working with heavily linked flexible schema data.
The consistent camp would take a tabular/relational DBMS and add a JSON column and additional columns for every relationship they want to maintain.
The others would take 2 different DBMS solutions - one for large collections of entries and one for the links between them, often - MongoDB and Neo4J.
In that case, every DBMS will have a custom modality-specific scaling, sharding, and replication strategy, but synchronizing them would be impossible in mutable conditions.
This makes it hard for the developers to choose a future-proof solution for their projects.
By putting different modality collections in one DBMS, we allow operation-level consistency controls giving the users all the flexibility one can get.
</details>

<details>
<summary>Why not adapt MQL or Cypher?</summary>

Mongo Query Language and Cypher by Neo4J are pretty popular but are vendor-specific.
Furthermore, for core functionality, using text-based protocols in 2022 is inefficient.
Instead, we adopt JSON-Pointer, JSON-Patch, and JSON-MergePatch RFCs for document and sub-document level updates.
And even those operations, as well as graph operations, are transmitted in binary form.
</details>
