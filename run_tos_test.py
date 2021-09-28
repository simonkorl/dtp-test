import os
import platform
import json
import argparse
import time

server_ip = "127.0.0.1"
port = "5555"

parser = argparse.ArgumentParser()

parser.add_argument('--ip', type=str, required=False, help="the ip of container_server_name that required")

parser.add_argument('--port', type=str, required=False, default="5555", help="the port of dtp_server that required,default is 5555, and you can randomly choose")

parser.add_argument('--server_name', type=str, required=False, default="tos_server", help="the container_server_name")

parser.add_argument('--client1_name', type=str, required=False, default="tos_client1", help="the container_client1_name")

parser.add_argument('--client2_name', type=str, required=False, default="tos_client2", help="the container_client2_name")

parser.add_argument('--run_times', type=int, default=1, help="The times that you want run repeatly")

params = parser.parse_args()
server_ip = params.ip
port = params.port
container_server_name = params.server_name
container_client1_name = params.client1_name
container_client2_name = params.client2_name
run_times = params.run_times

run_seq = 0
while run_seq < run_times:
    print(f"Round {run_seq}:\n")

    print("Restart docker")
    os.system(f"docker restart {container_server_name} {container_client1_name} {container_client2_name}")
    time.sleep(5)

    if not server_ip:
        out = os.popen(f"docker inspect {container_server_name}").read()
        out_dt = json.loads(out)
        server_ip = out_dt[0]["NetworkSettings"]["IPAddress"]

    input("Start?")

    print(f"run server on {server_ip} {port}")
    print(f"docker exec -itd {container_server_name} /bin/bash /home/aitrans-server/server_run.sh")
    # os.system(f"docker exec -it {container_server_name} sh -c \"cd /home/aitrans-server && ./bin/server {server_ip} {port} trace/block_trace/aitrans_block.txt &> server_err.log\"")
    os.system(f"docker exec -itd {container_server_name} /bin/bash /home/aitrans-server/server_run.sh")

    time.sleep(3)

    print("run client")
    # os.system(f"docker exec -it {container_client1_name} sh -c \"cd /home/aitrans-server && ./client {server_ip} {port} &> client_err.log\"")
    os.system(f"docker exec -itd {container_client1_name} /bin/bash /home/aitrans-server/client_run.sh")
    # os.system(f"docker exec -it {container_client2_name} /home/aitrans-server/client {server_ip} {port}")

    time.sleep(30)
    run_seq += 1