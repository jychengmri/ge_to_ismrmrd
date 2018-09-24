#!/bin/bash

dir_orchestra=orchestra-sdk-1.7-1
if [ ! -d $dir_orchestra ]; then
   echo "ERROR! orchestra library ($dir_orchestra) not found!"
   exit
fi

docker build -t ox:1.7 .
