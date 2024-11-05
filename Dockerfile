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
    gcc

WORKDIR /home/easycab
COPY . .