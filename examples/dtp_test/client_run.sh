
#!/bin/bash
cd /home/aitrans-server/

rm client.log > tmp.log 2>&1
sleep 0.2
./client 172.17.0.5 5555
