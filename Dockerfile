FROM ubuntu:latest

RUN apt update && apt install -y \
    build-essential \
    cmake \
    librdkafka-dev \
    libssl-dev \
    libmysqlclient-dev \
    libglib2.0-dev \
    libncurses-dev \
    pkg-config \
    gcc \
    neovim

    
WORKDIR /home/easycab
COPY CMakeLists.txt .
COPY src src
COPY fonts fonts
COPY res res
COPY README.md .
COPY setup.md .


RUN mkdir build

RUN cd build && cmake .. && make

