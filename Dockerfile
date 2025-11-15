# Multi-stage build for BTOON

# Build stage
FROM ubuntu:20.04 AS builder

# Avoid interactive prompts during build
ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    ninja-build \
    python3-dev \
    python3-pip \
    zlib1g-dev \
    liblz4-dev \
    libzstd-dev \
    && rm -rf /var/lib/apt/lists/*

# Copy source code
WORKDIR /btoon
COPY . .

# Build BTOON
RUN mkdir build && cd build && \
    cmake .. \
        -GNinja \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTS=OFF \
        -DBUILD_TOOLS=ON \
        -DBUILD_SHARED_LIBS=OFF \
    && ninja

# Runtime stage
FROM ubuntu:20.04

# Install runtime dependencies
RUN apt-get update && apt-get install -y \
    zlib1g \
    liblz4-1 \
    libzstd1 \
    && rm -rf /var/lib/apt/lists/*

# Copy built binaries and libraries
COPY --from=builder /btoon/build/btoon /usr/local/bin/
COPY --from=builder /btoon/build/btoon-schema /usr/local/bin/
COPY --from=builder /btoon/build/btoon-convert /usr/local/bin/
COPY --from=builder /btoon/build/libbtoon.a /usr/local/lib/
COPY --from=builder /btoon/include/btoon /usr/local/include/btoon

# Add non-root user
RUN useradd -m -s /bin/bash btoon
USER btoon
WORKDIR /home/btoon

# Set up entry point
ENTRYPOINT ["btoon"]
CMD ["--help"]

# Metadata
LABEL org.opencontainers.image.title="BTOON"
LABEL org.opencontainers.image.description="Binary Tree Object Notation - High-performance serialization format"
LABEL org.opencontainers.image.version="0.0.1"
LABEL org.opencontainers.image.authors="BTOON Project"
LABEL org.opencontainers.image.url="https://github.com/BTOON-project/btoon-core"
LABEL org.opencontainers.image.source="https://github.com/BTOON-project/btoon-core"
LABEL org.opencontainers.image.licenses="MIT"
