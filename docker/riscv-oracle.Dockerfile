FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        cmake \
        g++ \
        gcc-riscv64-linux-gnu \
        git \
        make \
        qemu-user \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
