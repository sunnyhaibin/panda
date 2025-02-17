#!/usr/bin/env sh
set -e

DFU_UTIL="dfu-util"

cd ..
scons -u -j$(nproc) --escc
cd escc

$DFU_UTIL -d 0483:df11 -a 0 -s 0x08004000 -D obj/escc.bin.signed
$DFU_UTIL -d 0483:df11 -a 0 -s 0x08000000:leave -D obj/bootstub.escc.bin
