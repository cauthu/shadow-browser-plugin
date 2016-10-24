#!/bin/bash


killall -9 io_process render_process webserver driver transport_proxy

transport_proxy --conf=conf/tproxy_ssp.conf --v=1 \
                > tproxy_ssp.log.txt 2>&1 &

sleep 1

transport_proxy --conf=conf/tproxy_csp.conf --v=2 \
                > tproxy_csp.log.txt 2>&1 &

webserver --conf=conf/webserver.conf --v=2 \
          > webserver.log.txt 2>&1 &

io_process --conf=conf/ioservice.conf --v=2 \
           > ioservice.log.txt 2>&1 &

sleep 2

render_process --conf=conf/renderer.conf > renderer.log.txt 2>&1 &

sleep 1

driver_process --conf=conf/driver.conf > driver.log.txt 2>&1
