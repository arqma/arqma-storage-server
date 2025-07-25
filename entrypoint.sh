#!/bin/bash
set -e

IP=$(curl -s https://api.ipify.org)
PORT=19996
ARQMAD_RPC_PORT=19994
ARQMA_DIR="$HOME/.arqma"

# Check if ~/.arqma exists and is writable
if [ ! -d "$ARQMA_DIR" ]; then
  echo "Creating directory $ARQMA_DIR..."
  mkdir -p "$ARQMA_DIR" || {
    echo "❌ Failed to create $ARQMA_DIR"
    exit 1
  }
fi

if [ ! -w "$ARQMA_DIR" ]; then
  echo "❌ Error: No write permission to $ARQMA_DIR"
  ls -ld "$ARQMA_DIR"
  exit 1
fi

echo "✅ $ARQMA_DIR is accessible and writable"
echo "Starting arqma-storage on ${IP}:${PORT} with RPC port ${ARQMAD_RPC_PORT}"

exec /usr/local/bin/arqma-storage "$IP" "$PORT" --arqmad-rpc-port "$ARQMAD_RPC_PORT"
