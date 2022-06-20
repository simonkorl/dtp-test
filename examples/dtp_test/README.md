# examples

## *_test

server_test 和 client_test 为使用 argp 重构的测试用程序

### server_test

```shell
❯ ./server_test --help
Usage: server_test [OPTION...] SERVER_IP SERVER_PORT DTP_CONFIG
libev dtp server for test

  -c, --color                log with color
  -l, --log=FILE             log file with debug and error info
  -o, --out=FILE             output file with received file info
  -t, --tos                  set tos
  -v, --log-level=LEVEL      log level ERROR 1 -> TRACE 5
  -?, --help                 Give this help list
      --usage                Give a short usage message
  -V, --version              Print program version
```

`SERVER_IP` 和 `SERVER_PORT` 为监听的地址，`DTP_CONFIG` 为传输用的 trace 文件（含路径）。

其余参数：
- 'c' 'color': 是否在输出日志信息前后添加颜色代码。启用后，error 为红色，warn 为黄色……输出到文件时**最好不启用**
- 'l' 'log=FILE': 日志信息输出的位置，不加此参数则输出到命令行
- 'o' 'out=FILE': 数据信息输出的位置，server 端基本上不会用到此参数
- 't' 'tos': 是否启用 tos，不启用则所有包都以 tos = 0 发送
- 'v' 'log-level=LEVEL': 日志的最高级别，从低到高为 error warn info debug trace 1-5

典型用法：
```shell
./server_test 127.0.0.1 5555 ~/trace.txt -v 3 -c
```

### client_test

```shell
❯ ./client_test --help
Usage: client_test [OPTION...] SERVER_IP SERVER_PORT
libev dtp client for test

  -c, --color                log with color
  -l, --log=FILE             log file with debug and error info
  -o, --out=FILE             output file with received file info
  -t, --tos                  set tos
  -v, --log-level=LEVEL      log level ERROR 1 -> TRACE 5
  -?, --help                 Give this help list
      --usage                Give a short usage message
  -V, --version              Print program version
```

`SERVER_IP` 和 `SERVER_PORT` 为 server 端监听的地址。

其余参数：
- 'c' 'color': 是否在输出日志信息前后添加颜色代码。启用后，error 为红色，warn 为黄色……输出到文件时**最好不启用**
- 'l' 'log=FILE': 日志信息输出的位置，不加此参数则输出到命令行
- 'o' 'out=FILE': 数据信息输出的位置，格式为 csv，推荐设置为 `client.csv` 等名字
- 't' 'tos': 是否启用 tos，不启用则所有包都以 tos = 0 发送
- 'v' 'log-level=LEVEL': 日志的最高级别，从低到高为 error warn info debug trace 1-5

典型用法：
```shell
./client_test 127.0.0.1 5556 -c -v 3 -o client.csv
```