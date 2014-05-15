#!/bin/sh

make ${1}_app_test DEBUG=graphite | ./gdb.pl
