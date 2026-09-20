FROM ubuntu:24.04@sha256:4fbb8e6a8395de5a7550b33509421a2bafbc0aab6c06ba2cef9ebffbc7092d90

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       autoconf \
       automake \
       bc \
       binutils \
       bison \
       build-essential \
       ca-certificates \
       ccache \
       clang \
       cmake \
       curl \
       device-tree-compiler \
       file \
       flex \
       gdb \
       git \
       libelf-dev \
       libssl-dev \
       lld \
       libtool \
       meson \
       ninja-build \
       pkg-config \
       python3 \
       python3-pip \
       python3-venv \
       rsync \
       u-boot-tools \
    && rm -rf /var/lib/apt/lists/*

COPY toolchain-check.sh /usr/local/bin/c-builder-check

RUN chmod 0755 /usr/local/bin/c-builder-check \
    && /usr/local/bin/c-builder-check

ENV CCACHE_DIR=/ccache

WORKDIR /workspace
CMD ["sleep", "infinity"]
