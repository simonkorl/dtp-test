#!/bin/sh

TRACE_FILE=${TRACE_FILE:-trace/block_trace/aitrans_block.txt}
DTP_SERVER_LOG=${DTP_SERVER_LOG:-server.log}
DTP_CLIENT_LOG=${DTP_CLIENT_LOG:-client.log}
DTP_RESULT_FILE=${DTP_RESULT_FILE:-client.csv}
QUIC_SERVER_LOG=${QUIC_SERVER_LOG:-server_fifo.log}
QUIC_CLIENT_LOG=${QUIC_CLIENT_LOG:-client_fifo.log}
QUIC_RESULT_FILE=${QUIC_RESULT_FILE:-client_fifo.csv}

DTP_SERVER=${DTP_SERVER:-./server_test}
DTP_CLIENT=${DTP_CLIENT:-./client_test}
QUIC_SERVER=${QUIC_SERVER:-./server_test_fifo}
QUIC_CLIENT=${QUIC_CLIENT:-./client_test_fifo}

PY=${PY:-python}
DRAW_PY_SCRIPT=${DRAW_PY_SCRIPT:-~/dtp-test-script/liveshow.py}

SERVER_IP=${SERVER_IP:-127.0.0.1}
DTP_PORT=${DTP_PORT:-5555}
QUIC_PORT=${QUIC_PORT:-5556}

if [ "$1" = "server" ]; then
    ${DTP_SERVER} ${SERVER_IP} ${DTP_PORT} ${TRACE_FILE} -v 3 -l ${DTP_SERVER_LOG} &
    ${QUIC_SERVER} ${SERVER_IP} ${QUIC_PORT} ${TRACE_FILE} -v 3 -l ${QUIC_SERVER_LOG}
elif [ "$1" = "client" ]; then
    ${DTP_CLIENT} ${SERVER_IP} ${DTP_PORT} -v 3 -l ${DTP_CLIENT_LOG} -o ${DTP_RESULT_FILE} &
    ${QUIC_CLIENT} ${SERVER_IP} ${QUIC_PORT} -v 3 -l ${QUIC_CLIENT_LOG} -o ${QUIC_RESULT_FILE}
elif [ "$1" = "clean" ]; then
    killall -9 ${DTP_SERVER} ${QUIC_SERVER}
elif [ "$1" = "draw" ]; then
    ${PY} ${DRAW_PY_SCRIPT} -t ${TRACE_FILE} -r ${DTP_RESULT_FILE} --title DTP &
    ${PY} ${DRAW_PY_SCRIPT} -t ${TRACE_FILE} -r ${QUIC_RESULT_FILE} --title QUIC
else
    echo "Usage: $0 {server|client|clean|draw}"
fi