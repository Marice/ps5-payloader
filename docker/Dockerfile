# Build and preview environment for PayLoader: the kit's prerequisites (tools/doctor.sh) with LLVM 18,
# and Mesa's EGL and GL for the off-screen preview (tools/host-snapshots.sh).
# SPDX-License-Identifier: GPL-3.0-or-later
FROM ubuntu:26.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        bash make ninja-build ccache python3 python3-pil git wget curl unzip tar coreutils pkg-config ffmpeg \
        clang-18 clang-format-18 clang-tidy-18 lld-18 llvm-18 libclang-rt-18-dev libc++-18-dev libc++abi-18-dev \
        libegl-dev libgl-dev libegl-mesa0 libgl1-mesa-dri libgtest-dev cmake ca-certificates \
    && for t in clang clang++ clang-format clang-tidy ld.lld; do ln -sf /usr/bin/$t-18 /usr/local/bin/$t; done \
    && ln -sf /usr/bin/llvm-ar-18 /usr/local/bin/llvm-ar && ln -sf /usr/bin/llvm-ranlib-18 /usr/local/bin/llvm-ranlib \
    && rm -rf /var/lib/apt/lists/*
