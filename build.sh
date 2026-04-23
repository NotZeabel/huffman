#!/usr/bin/env bash

if [ "$EUID" -ne 0 ]; then
    echo "This script must be run as root"
    exit 1
fi

echo "Installing dependencies"

apt update && apt install gcc

echo "Compiling files"
gcc -o compressor_serial serial_compressor.c
gcc -o compressor_threads threads_compressor.c
gcc -o compressor_process process_compressor.c
gcc -o decompressor_serial serial_decompressor.c
gcc -o decompressor_threads threads_decompressor.c
gcc -o decompressor_process process_decompressor.c

echo "Done. Exiting"
