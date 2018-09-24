#!/bin/bash

mkdir build
cd build
CC=gcc-4.9 CXX=g++-4.9 cmake ..
make all
