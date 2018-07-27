FROM debian:8

# Install dependencies
RUN apt-get update && apt-get -y \
    install \
    sudo \
    dos2unix \
    build-essential \
    pkg-config \
    bison \
    flex \
    autoconf \
    automake \
    libtool \
    make \
    nodejs \
    git

COPY . /usr/local/src/OpenPLC
WORKDIR /usr/local/src/OpenPLC

# Convert Windows-type line endings
RUN find . -type f -exec dos2unix {} \;

# Build the OpenPLC project
RUN DNP3_SUPPORT=N COMM_DRIVER=Blank bash /usr/local/src/OpenPLC/build.sh

#Start the server
CMD sudo nodejs server.js

