FROM gcc:4.9

RUN apt update && apt install -y \
    build-essential \
    fftw-dev \
    libboost-all-dev \
    cmake \
    git

ENV SDKTOP /usr/local/orchestra

COPY orchestra-sdk-1.7-1 /usr/local/orchestra

CMD ["make"]
