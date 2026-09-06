#!/usr/bin/env bash

set -u

# ============================================================
# Configurazione
# ============================================================
SERVER="../bin/server"
PORT="$1"
LOG_DIR="./logs"

# ============================================================
# Controlli preliminari
# ============================================================
if [[ ! -x "$SERVER" ]]; then
    echo "Errore: $SERVER non esiste o non è eseguibile."
    exit 1
fi

RUN_ID="$(date '+%Y%m%d_%H%M%S')"
RUN_DIR="$LOG_DIR/run_$RUN_ID"

mkdir -p "$RUN_DIR"

# ============================================================
# Avvio server
# ============================================================
echo "=========================================="
echo "              SERVER TEST                 "
echo "=========================================="
echo
echo "Server : $SERVER"
echo "Port   : $PORT"
echo "Log    : $RUN_DIR"
echo
echo "Avvio server..."

trap '' SIGINT

(
    trap - SIGINT
    "$SERVER" \
        -s "$PORT" \
        -d "$RUN_DIR"
) &

SERVER_PID=$!

echo "Server avviato con PID: $SERVER_PID"
echo 

# ============================================================
# Attesa fine server
# ============================================================
if wait "$SERVER_PID"; then 
    echo "Server terminato con successo" 
    exit 0
else 
    echo "Server terminato con errore"
    exit 1
fi 


