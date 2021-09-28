#!/bin/bash
cd /home/aitrans-server/

# check port
a=`lsof -i:5555 | awk '/server/ {print$2}'`
if [ $a > 0 ]; then
    kill -9 $a
fi

./bin/server 172.17.0.5 5555 trace/block_trace/aitrans_block.txt &> ./log/server_aitrans.log &
