FROM ubuntu:22.04

ARG KERNEL_VERSION=6.2.0-39-generic

# Install build dependencies
RUN apt update
RUN apt install -y build-essential linux-headers-$KERNEL_VERSION kmod dwarves flex bison

# Set up the kernel module build environment
RUN cp /sys/kernel/btf/vmlinux /usr/lib/modules/$KERNEL_VERSION/build/
RUN ln -s /lib/modules/$KERNEL_VERSION /lib/modules/$(uname -r)

##### Commands #####

WORKDIR /build

# Build the kernel module
CMD make