# Reproducible build/test environment: GCC 13, Clang 18, CMake, Ninja, Python.
#   docker build -t tlm-dev -f docker/Dockerfile .
#   docker run --rm -v "$PWD:/src" -w /src tlm-dev ./scripts/ci.sh
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build clang libclang-rt-18-dev lld llvm python3 python3-pytest ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
