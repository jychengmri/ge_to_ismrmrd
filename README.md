# Getting started with the Orchestra to ISMRMRD converter library

Orchestra conversion tools

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
    cmake -D OX_INSTALL_DIRECTORY=~/orchestra-sdk-1.6-1 ..
    make install
    cd ../
    ```

1. A typical command line to convert the supplied P-file using this library is:

   ```bash
   pfile2ismrmrd -v -l libp2i-generic.so -p GenericConverter -x $GE_TOOLS_HOME/share/ge-tools/config/default.xsl P12800_sample.7
   ```
