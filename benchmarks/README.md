# UKV Benchmarks

## Twitter

Operates on collections of `.ndjson` files gathered from Twitter Stream API.
Those were conducted on 1.2 TB collected of Tweets augmented to form a 10 TB dataset.

```sh
cmake -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Release . && make ukv_stl_twitter_benchmark && ./build/bin/ukv_stl_twitter_benchmark
```

Our baseline will be MongoDB.
The [`mongoimport`](https://www.mongodb.com/docs/database-tools/mongoimport/) official tool supports both `.csv` and `.ndjson` imports and typical performance will be as follows:

|         | Tweets    | Imports                     |        Retrieval        | Sampling |
| ------- | --------- | --------------------------- | :---------------------: | :------: |
| MongoDB | 1'048'576 | 9 K docs/s ~ **32 MB/s**    |                         |          |
|         |           |                             |                         |          |
| STL     | 1'048'576 | 157 K docs/s ~ **850 MB/s** |                         |          |
| LevelDB | 1'048'576 |                             |                         |          |
| RocksDB | 1'048'576 | 15 K docs/s ~ **80 MB/s**   | 140 K docs/s ~ 750 MB/s |          |
| UnumDB  | 1'048'576 |                             |


Even with provided tooling it generally performs around 10'000 insertions per second and won't surpass 100 MB/s.


## Adjacency List

Here we start with Neo4J, which also has an official tool for `.csv` import.
We take a real-world graph dataset, distributed in `.csv` form - the "Friendster" social network.
It contains:

* 8'254'696 vertices.
* 1'847'117'371 edges.
* ~225 edges per vertex.

The import too 3h 45m, averaging at:

* 136'449 edges/s.
* 5.3 MB/s.

Comparing that with 

## Bitcoin Graph