#!/bin/bash

git clone https://github.com/ismrmrd/ismrmrd.git
sed -i 's/ismrmrd SHARED/ismrmrd STATIC/g' ismrmrd/CMakeLists.txt
mkdir ismrmrd/build

CMD="export HDF5_ROOT=/usr/local/orchestra/include/recon/3p/Linux/hdf5-1.8.12_dev_linux64 &&
    cd /ismrmrd/ismrmrd/build && \
    cmake -D CMAKE_INSTALL_PREFIX=/ismrmrd -D HDF5_USE_STATIC_LIBRARIES=yes -D CMAKE_EXE_LINKER_FLAGS=\"-lpthread -lz -ldl\" .. && \
    make && make install && cd && \
    chown -R \`stat -c \"%u:%g\" /ismrmrd\` /ismrmrd"

docker run -it --rm -v "$PWD":/ismrmrd ox:1.7 bash -c "$CMD"
