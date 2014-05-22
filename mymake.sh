#!/bin/sh

make ${1}_app_test DEBUG=graphite | ./scripts/gdb.py
#make ${1}_unit_test DEBUG=graphite | ./scripts/gdb.py
