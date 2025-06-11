#!/bin/bash
set -e

# Automatically detect public IP
IP=$(curl -s https://api.ipify.org)

# Default ports
PORT=19996
ARQMAD_RPC_PORT=19994

echo "Starting arqma-storage on ${IP}:${PORT} with RPC port ${ARQMAD_RPC_PORT}"

# Launch the server
exec /usr/local/bin/arqma-storage "$IP" "$PORT" --arqmad-rpc-port "$ARQMAD_RPC_PORT"
