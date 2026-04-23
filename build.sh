#!/usr/bin/env bash

if [ "$EUID" -ne 0 ]; then
    echo "This script must be run as root"
    exit 1
fi

echo "Installing dependencies"

apt install gcc

echo "Compiling files"
gcc -o s_compressor serial_compressor.c
gcc -o t_compressor threads_compressor.c
gcc -o p_compressor process_compressor.c
gcc -o s_decompressor serial_decompressor.c
gcc -o t_decompressor threads_decompressor.c
gcc -o p_decompressor process_decompressor.c

echo "Done. Exiting"
