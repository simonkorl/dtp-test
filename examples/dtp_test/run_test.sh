#!/bin/bash
NET_TRACE=${NET_TRACE:-${HOME}/dtp_test_scripts/net_trace/traces_17.txt}
TRAFFIC_CONTROL_SCRIPT=${TRAFFIC_CONTROL_SCRIPT:-${HOME}/dtp_test_scripts/traffic_control.py}
TRAFFIC_CONTROL_ARGS=${TRAFFIC_CONTROL_ARGS:-"-nic enp1s0"}
TRAFFIC_CONTROL=${TRAFFIC_CONTROL:-false}
SERVER=${SERVER:-./server}
CLIENT=${CLIENT:-./client}
SERVER_IP=${SERVER_IP:-192.168.0.2}
SERVER_PORT=${SERVER_PORT:-5555}
TRACE_FILE=${TRACE_FILE:-trace/block_trace/aitrans_block.txt}
TRACE_FILE_BASENAME=${TRACE_FILE##*/}
TRACE_FILE_BASENAME=${TRACE_FILE_BASENAME%.*}
TC_LOG=${TC_LOG:-tc.log}
SERVER_LOG=${SERVER_LOG:-server.log}
COUNT=0
CLIENT_LOG=${CLIENT_LOG:-client}-${TRACE_FILE_BASENAME}

# sudo python /home/ubuntu1/dtp_test_scripts/traffic_control.py --load /home/ubuntu1/dtp_test_scripts/net_trace/traces_17.txt -nic enp1s0 > tc.log 2>&1 & ./server 192.168.0.2 5555 trace/block_trace/aitrans_block.txt

if [ "$1" == "help" ]; then
    echo "TODO: HELP MESSAGE"

elif [ "$1" == "server" ]; then

    if [ $TRAFFIC_CONTROL == true ]; then

        python ${TRAFFIC_CONTROL_SCRIPT} --load ${NET_TRACE} ${TRAFFIC_CONTROL_ARGS} > ${TC_LOG} 2>&1 & ${SERVER} ${SERVER_IP} ${SERVER_PORT} ${TRACE_FILE} &> ${SERVER_LOG}
    
    else

        {SERVER} ${SERVER_IP} ${SERVER_PORT} ${TRACE_FILE} &> ${SERVER_LOG}
    
    fi

elif [ "$1" == "client" ]; then

    if [ $TRAFFIC_CONTROL == true ]; then

        for ((COUNT; COUNT < $2; COUNT++)); do

            python ${TRAFFIC_CONTROL_SCRIPT} --load ${NET_TRACE} ${TRAFFIC_CONTROL_ARGS} > ${TC_LOG} 2>&1 & ${CLIENT} ${SERVER_IP} ${SERVER_PORT} && mv client.log ${CLIENT_LOG}-${COUNT}.log

        done
    
    else

        for ((COUNT; COUNT < $2; COUNT++)); do

            {CLIENT} ${SERVER_IP} ${SERVER_PORT} && mv client.log ${CLIENT_LOG}-${COUNT}.log

        done
    
    fi

elif [ "$1" == "reset" ]; then

    python ${TRAFFIC_CONTROL_SCRIPT} --reset $2

fi