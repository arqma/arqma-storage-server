#!/bin/bash
set -e

IP=$(curl -s https://api.ipify.org)
PORT=19996
ARQMAD_RPC_PORT=19994

echo "Starting arqma-storage-server on ${IP}:${PORT} with RPC port ${ARQMAD_RPC_PORT}"
exec /usr/local/bin/arqma-storage-server "$IP" "$PORT" --arqmad-rpc-port "$ARQMAD_RPC_PORT"
