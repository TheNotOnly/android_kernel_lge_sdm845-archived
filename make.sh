#!/bin/bash
if [ "$2" == "gcc" ]
then
make O=out -j$1
elif [ "$2" == "clang" ]
then
make -j$1 O=out CC=clang CLANG_TRIPLE=aarch64-linux-gnu- CROSS_COMPILE=aarch64-linux-gnu-
fi
