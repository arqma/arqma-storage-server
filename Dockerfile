# syntax=docker/dockerfile:1

# === Stage 1: Build the binary ===
FROM --platform=$BUILDPLATFORM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

WORKDIR /src
COPY . .

ARG TARGETPLATFORM
ARG TARGETARCH
ARG TARGETOS
ENV TARGETPLATFORM=$TARGETPLATFORM \
    TARGETARCH=$TARGETARCH \
    TARGETOS=$TARGETOS

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    pkg-config \
    git \
    cmake \
    curl \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Build using Makefile
RUN make all

# === Stage 2: Runtime image ===
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Install runtime dependencies
RUN apt-get update && apt-get install -y \
    curl \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Copy binary
COPY --from=builder /src/binaries/arqma-storage /usr/local/bin/arqma-storage

# Copy entrypoint script
COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

# Create user with home directory
RUN useradd --create-home --home-dir /home/storage --shell /bin/bash storage

# Ensure ~/.arqma can be used
RUN mkdir -p /home/storage/.arqma && chown -R storage:storage /home/storage

USER storage

EXPOSE 19996

ENTRYPOINT ["/entrypoint.sh"]
