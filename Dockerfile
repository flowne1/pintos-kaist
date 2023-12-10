# Dockerfile for setting up the development environment on aarch64
FROM ubuntu:18.04

RUN apt-get update && \
    apt-get install -y python3 \
    make \
    git \
    gdb \
    gdb-multiarch \
    gcc-multilib-arm-linux-gnueabi \
    gcc-x86-64-linux-gnu \
    g++-x86-64-linux-gnu \
    qemu;

RUN for file in /usr/bin/x86_64-linux-gnu-*; \
        do link_name=$(basename $file | sed 's|x86_64-linux-gnu-||'); \
        ln -s $file /usr/bin/$link_name; \
    done

ENV CROSS_COMPILE x86_64-linux-gnu-
ENV CC ${CROSS_COMPILE}gcc
ENV CXX ${CROSS_COMPILE}g++
ENV LD ${CROSS_COMPILE}ld
ENV AR ${CROSS_COMPILE}ar
ENV AS ${CROSS_COMPILE}as
ENV RANLIB ${CROSS_COMPILE}ranlib
ENV NM ${CROSS_COMPILE}nm
ENV OBJCOPY ${CROSS_COMPILE}objcopy
ENV OBJDUMP ${CROSS_COMPILE}objdump
ENV STRIP ${CROSS_COMPILE}strip