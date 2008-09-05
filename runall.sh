#!/bin/sh

rm -rf server/mdird/checks/mdird/*
rm -f /tmp/*.log
source devenv

sudo tcpdump -ni lo port 21654 -s 0 -w /tmp/mdird.pcap&
build/server/mdird/mdird&
build/client/mdsync/mdsync&

wait
