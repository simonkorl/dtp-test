# DTP for TOS

为了在指定 TOS 的时候可以得知发送的 stream 对应块的信息，修改了 send API 的使用方法。

Rust 版本现在可以接受一个 Option<Vec<Block>> 来决定是否保存当前数据包中发送的全部块的信息

C 语言的版本则是可以通过地址读取一个 Block* 作为数组的首地址，通过下标可以进行访问。如果不想启用这个记录功能则传入 NULL 指针即可。

修改的文件包括：

1. lib.rs 的 send 函数。除了参数外还修改了增加 stream_frame 那一块的逻辑
2. ffi.rs 中 quiche_conn_send 部分的逻辑

其他的修改包括：

1. struct Block 被声明在了 quiche.h 中

## 演示与测试程序

在 exmaple 目录下，添加了一个名为 dtp_test 的项目。该项目使用 C 语言实现的服务端和客户端测试程序书写，并且已经修改了相关的实现，其会在命令行中打印出每一次数组中的块的信息。同时，ping-pong 项目也已经进行过相同的修改。

上述的测试项目都可以通过查看 git commit log 来确认对应的修改，主要需要处理的地方为 `quiche_conn_send` 附近的代码。

dtp_test 项目可以使用下面的命令进行测试：

1. `make test`: 自动编译并且运行测试，如果可以在命令行看到大量的打印数据，并且 client.log 可以完整生成（共 1059 行，最后一行为空行）则说明运行正确。
2. `make kill`：如果运行失败则需要手动结束客户端和服务端。 **该命令不保证可以正确运行**，请自行确认。

ping-pong 项目和原来的编译方法没有任何区别。

# 原始说明文档
# DTP (Deadline Transport Protocol)
[中文文档](README_zh.md)

DTP is a user-space secure and high performance transport protocol based on QUIC.
DTP transfers data in blocks and deadline-aware service for applications.  DTP strategically balances blocks' factors including deadline, priority, and dependency when deciding which block to send or drop. In addition, provide adaptive FEC to alleviate the delay caused by retransmission.
DTP can improve performance in scenarios like streaming media transmission. 

We outline the DTP’s modification of the QUIC in IETF [draft](https://datatracker.ietf.org/doc/html/draft-shi-quic-dtp-06).

The DTP protocol is developed based on [quiche](https://github.com/cloudflare/quiche). This implementation provides the API to send and process packets of QUIC. The application is responsible for providing I/O (e.g. sockets handling) as well as an event loop with support for timers.

## Quick Start

### Connection setup

In order to create a DTP connection, we first need to create a "configuration":

```rust
let config = quiche::Config::new(quiche::PROTOCOL_VERSION)?;
```

A "configuration" (`quiche::Config`) contains several configurable options related to the connection, including the length of the connection timeout, the maximum number of streams, etc. This "configuration" can be shared by multiple "connection"s.

With the "configuration", we can create a "connection" using the `connect()` function on the client side and the `accept()` function on the server side:

```rust
// Client connection.
let conn = quiche::connect(Some(&server_name), &scid, &mut config)?;

// Server connection.
let conn = quiche::accept(&scid, None, &mut config)?;
```

Then we can use "connection" (`quiche::Connection`) for DTP block and packet processing.

### Handling incoming packets

Using the `recv()` method of a "connection", an application can process packets from the network belonging to that "connection":

```rust
loop {
    let read = socket.recv(&mut buf).unwrap(); // `socket` is a UDP socket

    let read = match conn.recv(&mut buf[..read]) {
        Ok(v) => v,

        Err(quiche::Error::Done) => {
            // Done reading.
            break;
        },

        Err(e) => {
            // An error occurred, handle it.
            break;
        }
    }
}
```

The DTP protocol processes the packets after they are received, extracts the block data and waits for the application to call the interface to get the payload.

###  Handling outgoing packets

The packets to be sent are processed through the connection's `send()` function:

```rust
loop {
    let write = match conn.send(&mut out) {
        Ok(v) => v,

        Err(quiche::Error::Done) => {
            // Done writing.
            break;
        },

        Err(e) => {
            // An error occurred, handle it.
            break;
        },
    };

    socket.send(&out[..write]).unwrap(); // `socket` is a UDP socket
}
```

The "connection" will use the `send()` function to provide the packet to be sent and write it into the specified buffer, and the application needs to write the data in the buffer to the UDP socket.

### Handling the timer

After the packet is sent, the application needs to maintain a timer (timer) to process the time-related events in the DTP protocol. Applications can use the connection's `timeout()` method to get the time remaining until the next event occurs:

```rust
let timeout = conn.timeout();
```

The application needs to provide a timer implementation corresponding to the operating system or network processing framework. When the timer triggered, the application needs to call the `on_timeout()` function of "connection". There may be some packets that need to be sent after this function is called.

```rust
// Timeout expired, handle it.
conn.on_timout();

// Send more packets as needed after timeout.
loop {
    let write = match conn.send(&mut out) {
        Ok(v) => v,

        Err(quiche::Error::Done) => {
            // Done writing.
            break;
        },

        Err(e) => {
            // An error occurred, handle it.
            break;
        }
    };

    socket.send(&out[..write]).unwrap(); // `socket` is a UDP socket.
}
```

### Sending and receiving block data

#### Sending of data blocks

After completing the above-mentioned steps, the "connection" will send some data successively to establish the connection. The application can send and receive application data after the connection is established successfully.

Data blocks can be sent using the `stream_send_full()` method. The user can specify the "Deadline", "Priority" (Priority) and "Depend Block" when a data block is expected to arrive. DTP will schedule data packets according to the provided information to achieve the good delivery results. Blocks whose wait time has expired their deadlines will be dropped:

```rust
if conn.is_established() {
    // Send a block of data with deadline 200ms and priority 1, depend on no block
    conn.stream_send_full(0, b"a block of data", true, 200, 1, 0)?;
}
```

#### Receiving of data blocks

Applications can find out if a "connection" has blocks that can be read by calling the "connection"'s `readable()`. This function returns an iterator of blocks where all blocks that can read data are placed.

After knowing the blocks that can be read, the application can use the `stream_recv()` function to read the data in the block:

```rust
if conn.is_established() {
    // Iterate over readable streams.
    for stream_id in conn.readable() {
        // Stream is readable, read until there's no more data.
        while let Ok((read, fin)) = conn.stream_recv(stream_id, &mut buf) {
            println!("Got {} bytes on stream {}", read, stream_id);
        }
    }
}
```

DTP is also compatible with quiche's original stream sending/receiving. See [quiche: Sending and receiving stream data](https://github.com/cloudflare/quiche#sending-and-receiving-stream-data) for details.

## C/C++ Support

DTP has a [C API] that is simply wrapped around the Rust interface, making it relatively easy to integrate DTP into C/C++ applications (and also for other languages to use FFI to call C APIs).

The C API follows the same design of the Rust one, modulo the constraints imposed by the C language itself.

When running `cargo build`, the project generates a `libquiche.a` static library. This is a standalone library and can be linked into C/C++ applications.

Please refer to the sample code in examples/ping-pong for using DTP in C/C++.

[C API]: https://github.com/STAR-Tsinghua/DTP/blob/main/include/quiche.h

## Building Requirements

Tested environments and dependencies:

1. OS: Ubuntu 18.04/20.04/22.04
2. Docker: 20.10.5
3. Rustc: 1.50.0
4. go: 1.10.4 linux/amd64
5. cmake: 3.10.2
6. perl: v5.26.1
7. gcc: 9.3.0, 10.3.0

### Images for compilation and implementation

We provide a Debian:buster version of the image that can be used to build executable programs: `simonkorl0228/aitrans_build`. Copy the entire repository to the image to compile.

If you need a basic development environment, you can use the `simonkorl0228/dtp-docker` image for test and development. This image is based on the Ubuntu image, and the subsequent version numbers are the same as the Ubuntu image version.

If you want to build your own environment, please refer to the following steps.

### Rust toolchain installation

The Rust toolchain can be installed by following the [official](https://www.rust-lang.org/) instructions.

Most Linux operating systems can install by the following ways:

```sh
$ curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
```

### Go language installation

For the installation of Go, please refer to [Official Website Instructions](https://golang.google.cn/doc/install).

### Other dependencies

Some dependencies' installation methods (take Ubuntu18.04 as an example):

1. libev: `sudo apt install libev-dev`
2. uthash: `sudo apt install uthash-dev`

This project uses git submodule to manage some components, don't forget to synchronize.

```bash
$ git submodule init
$ git submodule update
```

## Building

After setting up the environment, you can use git to get the source code:

```bash
$ git clone --recursive https://github.com/STAR-Tsinghua/DTP
```

then build with cargo:

```bash
$ cargo build --release
```

The build result is in the target/release directory and contains three files: `libquiche.a` static library, `libquiche.so` dynamic library and `libquiche.rlib` Rust link library.

Note: This project relies on [BoringSSL]. In order to compile this library, `cmake`, `go`, `perl` dependencies may be required. It may also require [NASM](https://www.nasm.us/) on Windows. See the [BoringSSL documentation](https://github.com/google/boringssl/blob/master/BUILDING.md) for more details.

[BoringSSL]: https://boringssl.googlesource.com/boringssl/

## Examples

The server.rs and client.rs in the examples directory are Rust sample programs, and the examples/ping-pong directory is sample programs in C.

The Rust sample programs are built using `cargo build --examples`, and run the following commands in the project root directory to try them.

```bash
$ cargo run --example server
$ cargo run --example client http://127.0.0.1:4433/hello_world --no-verify
```

`Hello World!` can be received on the client side. The test programs perform a simple HTTP3 GET operation to get the files located in the examples/root directory.

In the examples/c_ping-pong directory, a simple example is implemented in C language. The usage can be found in the corresponding directory.

In the examples/c_features_example directory, a sample program is implemented using C language to display the newly added features of DTP. You can view the operation method in the corresponding directory.

## More references

* The DTP protocol is an extension of the QUIC protocol. You can refer to the QUIC protocol standards [RFC9000](https://www.rfc-editor.org/rfc/rfc9000.html) and [RFC9001](https://www.rfc-editor.org/rfc/rfc9001).
* DTP is developed on the [quiche 0.2.0](https://github.com/cloudflare/quiche/tree/0.2.0) version, you can refer to related documents for more content.

* The `cargo doc` command can be used to generate help documentation to assist development work.

* DTP is used in some other projects, such as "Intelligent Network Technology Challenge" (AItrans), its [official website](https://www.aitrans.online/) provides some introduction to DTP, which is helpful for preliminary understanding of DTP. We also held [Meet Deadline Requirements](https://github.com/AItransCompetition/Meet-Deadline-Requirements-Challenge) to explore the implementation of Deadline related algorithms. These projects provide container or simulator environments for exploration.
