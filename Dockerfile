# syntax=docker/dockerfile:1

# === Stage 1: Build the binary ===
FROM --platform=$BUILDPLATFORM ubuntu:22.04 AS builder

WORKDIR /src
COPY . .

ARG TARGETPLATFORM
ARG TARGETARCH
ARG TARGETOS
ENV TARGETPLATFORM=$TARGETPLATFORM \
    TARGETARCH=$TARGETARCH \
    TARGETOS=$TARGETOS

RUN apt-get update && apt-get install -y \
    build-essential pkg-config git cmake curl ca-certificates \
    && rm -rf /var/lib/apt/lists/*

RUN make all

# === Stage 2: Runtime image ===
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    curl ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Copy the compiled binary from the correct location
COPY --from=builder /src/binaries/arqma-storage-server /usr/local/bin/arqma-storage-server

COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

RUN useradd --no-create-home --shell /bin/false storage
USER storage

EXPOSE 19996

ENTRYPOINT ["/entrypoint.sh"]
