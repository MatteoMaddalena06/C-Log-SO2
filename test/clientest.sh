#!/usr/bin/env bash

set -u 

# ============================================================
# Configurazione
# ============================================================
CLIENT="../bin/client"

HOST="$1"
PORT="$2" 

MIN_CLIENTS=10
MAX_CLIENTS=100

MIN_MESSAGES=1
MAX_MESSAGES=100

MIN_DELAY_MS=0
MAX_DELAY_MS=500

# ============================================================
# Controlli preliminari
# ============================================================
if [[ ! -x "$CLIENT" ]]; then
    echo "Errore: $CLIENT non esiste o non è eseguibile."
    exit 1
fi

# ============================================================
# Cleanup
# ============================================================
cleanup() {
    echo
    echo "=========================================="
    echo "Cleanup..."
    echo "=========================================="

    # Termina eventuali client ancora attivi
    for pid in "${CLIENT_PIDS[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null || true
        fi
    done

    echo "Cleanup completato."
}

# ============================================================
# Avvio clients
# ============================================================
CLIENT_PIDS=()
NUM_CLIENTS=$((RANDOM % (MAX_CLIENTS - MIN_CLIENTS + 1) + MIN_CLIENTS))

echo "=========================================="
echo "              CLIENT TEST                 "
echo "=========================================="
echo
echo "Clients : $CLIENT [$NUM_CLIENTS]"
echo "Host    : $HOST"
echo "Port    : $PORT"
echo  

TOTAL_MS=0

for ((i=1; i<=NUM_CLIENTS; i++)); do

    TIMES=$((RANDOM % (MAX_MESSAGES - MIN_MESSAGES + 1) + MIN_MESSAGES))
    TOTAL_MS=$((TOTAL_MS + TIMES))

    DELAY_MS=$((RANDOM % (MAX_DELAY_MS - MIN_DELAY_MS + 1) + MIN_DELAY_MS))
    DELAY_NS=$((DELAY_MS * 1000000))

    echo "Client #$i:"
    echo "  messaggi = $TIMES"
    echo "  delay    = ${DELAY_MS} ms"

    "$CLIENT" \
        -h "$HOST" \
        -s "$PORT" \
        -t "$TIMES" \
        -d "$DELAY_NS" \
        > /dev/null 2>&1 & 

    PID=$!
    CLIENT_PIDS+=("$PID")

    echo "  PID      = $PID"
    echo

done

echo "Messaggi totali inviati al server: $TOTAL_MS"

# ============================================================
# Attesa client
# ============================================================
echo 
echo "=========================================="
echo "Attesa terminazione client..."
echo "=========================================="

SUCCESS=0
FAILED=0

for ((i=0; i<${#CLIENT_PIDS[@]}; i++)); do

    PID="${CLIENT_PIDS[$i]}"

    if wait "$PID"; then
        echo "Client #$((i+1)) terminato correttamente."
        ((SUCCESS++))
    else
        RET=$?
        echo "Client #$((i+1)) FALLITO (exit code $RET)."
        ((FAILED++))
    fi

done

# ============================================================
# Risultato
# ============================================================
echo
echo "=========================================="
echo "             RISULTATO TEST"
echo "=========================================="
echo
echo "Client totali : $NUM_CLIENTS"
echo "Successi      : $SUCCESS"
echo "Fallimenti    : $FAILED"
echo

if (( FAILED > 0 )); then
    exit 1
fi

exit 0
