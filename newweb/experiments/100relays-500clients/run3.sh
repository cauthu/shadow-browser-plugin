#!/bin/bash

time shadow-tor --workers=16 --heartbeat-frequency=30 -i ../../../shadow.config.xml --cpu-threshold=-1 --seed=337589913
