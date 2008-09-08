#!/bin/sh

rm -rf server/mdird/checks/mdird/*
rm -f /tmp/*.log
source devenv
mkdir -p client/checks/msgs/.put
echo "glop: pas glop" > client/checks/msgs/.put/test
echo "truc: muche" >> client/checks/msgs/.put/test

#sudo tcpdump -ni lo port 21654 -s 0 -w /tmp/mdird.pcap&
build/server/mdird/mdird&
build/client/mdsync/mdsync&

wait
