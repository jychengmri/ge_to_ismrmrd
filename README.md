# Getting started with the Orchestra to ISMRMRD converter library

Orchestra conversion tools

## TODO

1. Option to directly compile ISMRMRD files in rather than compile separate libraries and link.


## To build and install the tools to convert GE raw files into ISMRMRD files:

1.  Obtain the ISMRMRD source code:

    ```bash
    git clone https://github.com/ismrmrd/ismrmrd
    ```

1. Configure, compile, and install ISMRMRD:

    ```bash
    cd ismrmrd/
    mkdir build
    cd build/
    cmake ..
    make install
    cd ../
    ```

1. Obtain the GE converter source code:

    ```bash
    git clone https://github.com/ismrmrd/ge_to_ismrmrd.git
    ```

1. Configure, compile and install the converter:

    ```bash
    cd ge_to_ismrmrd/
    mkdir build
    cd build/
    CC=gcc-4.9 CXX=g++-4.9 cmake -G Ninja ..
    ninja install
    cd ../
    ```

1. A typical command line to convert the supplied P-file using this library is:

   ```bash
   ge_to_ismrmrd -v P12800_sample.7
   ```
