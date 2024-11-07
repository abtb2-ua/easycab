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
RUN apt install mysql-client
    
WORKDIR /home/easycab
COPY CMakeLists.txt .
COPY src src
COPY fonts fonts
COPY res res
COPY README.md .
COPY setup.md .


RUN mkdir build
WORKDIR /home/easycab/build

RUN cd build && cmake .. && make

