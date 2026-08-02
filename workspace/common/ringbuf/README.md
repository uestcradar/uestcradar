# Fixed-slot shared-memory RingBuffer

Sidecar 与 SDK 共用的 POSIX shared memory ABI。模块可独立编译为
`uestcradar::ringbuf`，不依赖 Sidecar、UCX 或业务数据类型。

## 数据模型

RingBuffer 是无锁 SPSC 记录队列，而不是无边界字节流。一条记录完整占用一个
Slot；长度不得超过创建时的 `max_payload_bytes`，因此不存在跨环尾拼接。

```text
4096-byte RingBufferHeader
Slot 0: 64-byte SlotHeader | max_payload_bytes Payload | alignment
Slot 1: 64-byte SlotHeader | max_payload_bytes Payload | alignment
...
```

控制头包含 magic、ABI version、`slot_count`、`max_payload_bytes`、
`type_id`、`type_version` 和分离缓存行的读写位置。Ring ABI v6 的 SlotHeader
就是冻结的 64B Envelope，包含 `frame_id/timestamp/type_id/type_version/`
`payload_length/flags/reserved[28]`。Payload 紧随 Envelope；
`max_payload_bytes` 不包含 Envelope。

## 生命周期与内存序

- 生产者：`ringbuf_reserve` → 写 Envelope/Payload → `ringbuf_commit`。
- 消费者：`ringbuf_acquire` → 使用 Payload → `ringbuf_release`。
- 放弃尚未提交的写入使用 `ringbuf_cancel`。
- commit 以 release 发布；acquire 以 acquire 观察。
- Ring 满时返回 `would_block`，绝不覆盖未释放 Slot。
- Read Lease 或 UCX send 完成前不得 release；UCX receive 成功且长度合法后才
  commit。

RingBuffer 仍是 SPSC：不能同时存在两个生产者或两个消费者。热路径不分配
Payload、不使用 Mutex。

## 构建、测试与 Benchmark

```bash
cmake -S workspace/common/ringbuf -B build/ringbuf \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build/ringbuf --parallel
ctest --test-dir build/ringbuf --output-on-failure

./build/ringbuf/ringbuf-benchmark \
  --payload-bytes 64,4096,65536,1048576 \
  --slot-counts 2,8,64 \
  --warmup 3 --duration 15 --repetitions 3 --format jsonl
```

Benchmark 使用两个进程重新映射同一共享内存，输出有效 Payload MiB/s、消息率、
平均/P50/P99 Slot 交接延迟和生产/消费进程 CPU。短于数秒的运行只用于冒烟，
不可作为正式性能结论。
