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

DTP 协议是一个用户态的安全传输协议。DTP 基于 QUIC 协议进行研发，保留 QUIC 的其他特性的同时将数据进行了“块”的抽象，为用户提供了根据用户设定的截止时间（Deadline）和优先级（Priority）进行源端调度和源端丢弃的服务，在例如流媒体传输等传输场景下可以降低平均传输时延，提升用户体验。

DTP 协议基于 [quiche](https://github.com/cloudflare/quiche) 进行开发，实现了 QUIC 传输协议并且加入了基于“块”的调度算法。该实现提供了一个底层的 API 来进行数据包的发送与处理。上层应用需要提供 I/O （如处理接口）并且需要使用具有定时器的事件循环来实现相关的数据收发逻辑。

## 简单 API 说明

### 创建连接

为了创建一个 DTP 连接，首先需要需要创建一个“配置”(configuration):

```rust
let config = quiche::Config::new(quiche::PROTOCOL_VERSION)?;
```

一个“配置”(`quiche::Config`)会包含若干与连接相关的可配置选项，包括连接超时的时长、最大的流的数量等。这个“配置”可以被多个“连接”(connection)共享。

有了“配置”之后便可以创建“连接”，在客户端使用`connect()`函数，在服务端则使用`accept()`函数：

```rust
// Client connection.
let conn = quiche::connect(Some(&server_name), &scid, &mut config)?;

// Server connection.
let conn = quiche::accept(&scid, None, &mut config)?;
```

一个“连接”(`quiche::Connection`)可以用于进行数据包的收发以及数据流的收发。一方面，“连接”可以将数据包从 UDP socket 进行读取并且将数据打包发送到 socket 中；另一方面，应用可以使用“连接”进行通用的“流”的数据传输。

### 处理到来的数据包

使用“连接”的`recv()`函数，应用可以处理属于该“连接”的从网络传来的数据包:

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

DTP 协议会在接收到数据包之后对数据包进行处理，提取出其中的流数据并且等待应用调用接口以呈递数据。

### 处理发送的数据包

DTP 是一个用户态传输协议，这意味着应用需要手动处理网络数据包的发送工作。要发送的数据包通过“连接”的`send()`函数进行处理：

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

“连接”会通过`send()`函数将要发送的数据打包后写入到提供的 buffer 中，应用则需要将该 buffer 中的数据写入 UDP socket 中。

### 处理计时器

当数据包发送后，应用需要维护一个计时器(timer)来处理 DTP 协议中与时间有关的事件。应用可以使用“连接”的`timeout()`函数来获取下一个事件发生剩余的时间：

```rust
let timeout = conn.timeout();
```

应用需要提供对应操作系统或是网络处理框架的计时器(timer)实现。当计时器触发时，应用需要调用“连接”的`on_timeout()`函数。调用后该函数后可能有一些额外的数据包需要进行发送。

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

### 收发流数据

#### 数据流的发送

在完成了上述的传输框架后，“连接”会先后发送一些数据进行连接建立工作。此时应用即可进行应用数据的收发。

数据可以使用`stream_send()`函数进行发送：

```rust
if conn.is_established() {
    // Handshake completed, send some data on stream 0.
    conn.stream_send(0, b"hello", true)?;
}
```

DTP 还提供了`stream_send_full()`函数，使得用户可以指定某个数据块（对应一个流）所预期到达的“截止时间” (Deadline) 和“优先级” (Priority)。DTP 会根据提供的信息进行数据包的调度以达到最优的发送效果。所有等待时间超时的数据块将被丢弃。

```rust
if conn.is_established() {
    // Send a block of data with deadline 200ms and priority 1
    conn.stream_send_full(0, b"a block of data", true, 200, 1, 0)?;
}
```

#### 数据流的接收

应用可以通过调用“连接”的`readable()`来得知一个“连接”中是否有可以读取的流。该函数返回一个流的迭代器，所有可以读取数据的流都会被放入其中。

得知可以读取的流之后，应用便可以使用`stream_recv()`函数读取流中的数据：

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

## 在 C/C++ 中使用 DTP

DTP 有一个在 Rust 接口外简单包装的 [C 接口]，使得 DTP 可以比较简单地被整合入 C/C++ 应用（同样对于其他语言而言也可以使用 FFI 调用 C API）

C API 和 Rust API 的设计相同，只是处理了 C 语言自身的一些限制。

当运行`cargo build`时，项目会生成一个`libquiche.a`静态库。这是一个独立的库并且可以连接到 C/C++ 的应用当中。

关于在 C/C++ 中使用 DTP 请参考 examples/ping-pong 中的样例代码。

[C 接口]: https://github.com/STAR-Tsinghua/DTP/blob/main/include/quiche.h

## 环境安装

目前确认正确运行的环境：

1. OS: Ubuntu 18.04/Ubuntu 20.04
2. Docker: 20.10.5
3. Rustc: 1.50.0
4. go: 1.10.4 linux/amd64
5. cmake: 3.10.2
6. perl: v5.26.1

### 语言依赖

DTP 需要使用 Rust 和 Go 两个语言环境来完成编译过程。我们提供了一个 Debian:buster 版本的可用于编译的镜像：`simonkorl0228/aitrans_build`可供使用。

#### Rust 工具链安装

Rust 工具链可按照[官方](https://www.rust-lang.org/)指示方法进行安装。

大多数 Linux 操作系统可以用下面的方法进行安装：

```sh
$ curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
```

#### Go 语言安装

Go 的安装请参考[官网说明](https://golang.google.cn/doc/install) 。

#### 换源

中国大陆地区建议对 Rust 和 Go 进行换源，否则在编译过程中可能出现超时的问题。换源方法可以参考下面链接的内容。

* Rust: https://mirrors.tuna.tsinghua.edu.cn/help/crates.io-index.git/
* Go: https://blog.csdn.net/Kevin_lady/article/details/108700915

### 其他依赖

一些依赖的安装方法(以 Ubuntu18.04 为例)：

1. libev: `sudo apt install libev-dev`
2. uthash: `sudo apt install uthash-dev`

于此同时，本项目的采用了 git submodule 来管理部分组件，不要忘记进行同步。

```bash
$ git submodule init
$ git submodule update
```

boringssl 库在国内 git 下载速度缓慢，建议直接下载压缩文件：https://github.com/google/boringssl

## 样例程序说明

在 examples 目录下的 server.rs 与 client.rs 为 Rust 的样例程序，examples/ping-pong 目录下为 C 的样例程序。

Rust 样例程序使用 `cargo build --examples` 进行构建，在项目根目录下运行下面指令进行测试。

```bash
$ cargo run --example server
$ cargo run --example client http://127.0.0.1:4433/hello_world --no-verify
```

可在客户端侧接收到 `Hello World!` 。该测试程序执行了一个简单的 HTTP3 GET 操作，获取位于 examples/root 目录下的文件。

在 examples/ping-pong 目录下则用 C 语言实现了一个简单示例，可到对应目录下查看运行方法。

## 项目构建

项目需要 Rust 1.39 及以上进行构建。可以使用 [rustup](https://rustup.rs/) 获得 Rust 的最新稳定版本。

在设定好环境后，可以使用 git 获取源代码：

```bash
$ git clone --recursive https://github.com/STAR-Tsinghua/DTP
```

之后使用 cargo 构建：

```bash
$ cargo build --release
```

构建结果在 target/release 目录下，包含三个文件：`libquiche.a`静态库、`libquiche.so`动态库和`libquiche.rlib`Rust 链接库。

注意：该项目依赖 [BoringSSL] 来完成加密握手环节。为了编译该库，可能需要`cmake`, `go`,`perl`的依赖。在 Windows 上可能还需要 [NASM](https://www.nasm.us/)。 可以查看[ BoringSSL 文档](https://github.com/google/boringssl/blob/master/BUILDING.md) 了解更多细节。

[BoringSSL]: https://boringssl.googlesource.com/boringssl/

## 其他参考资料

* DTP 协议是 QUIC 协议的拓展，可以参考 QUIC 协议的标准 [RFC9000](https://www.rfc-editor.org/rfc/rfc9000.html) 与 [RFC9001](https://www.rfc-editor.org/rfc/rfc9001)。
* DTP 在 [quiche 0.2.0](https://github.com/cloudflare/quiche/tree/0.2.0) 版本上进行开发，可以参考相关文档了解相关内容。

* 可以使用`cargo doc`命令生成帮助文档来辅助开发工作。

* DTP 被用于了一些其他项目，例如“智能网络技术挑战赛”(AItrans)，其[官网](https://www.aitrans.online/)提供了部分对于 DTP 的介绍，有助于对 DTP 项目有初步的理解。
