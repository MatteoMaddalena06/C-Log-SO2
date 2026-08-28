#!/usr/bin/env bash

set -u

# ============================================================
# Configurazione
# ============================================================

SERVER="../bin/server"
CLIENT="../bin/client"

HOST="127.0.0.1"
PORT=8080

# Numero casuale di client
MIN_CLIENTS=1
MAX_CLIENTS=20

# Numero casuale di messaggi per client
MIN_MESSAGES=1
MAX_MESSAGES=100

# Delay casuale tra le trasmissioni dei client, in millisecondi
MIN_DELAY_MS=0
MAX_DELAY_MS=500

# Directory dove salvare i log del test
LOG_DIR="./logs"

# ============================================================
# Variabili globali
# ============================================================

SERVER_PID=""
CLIENT_PIDS=()

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

    # Termina il server
    if [[ -n "$SERVER_PID" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
        echo "Arresto server (PID $SERVER_PID)..."
        kill "$SERVER_PID" 2>/dev/null || true

        # Gli diamo un po' di tempo per terminare
        sleep 0.5

        # Se è ancora vivo, SIGKILL
        if kill -0 "$SERVER_PID" 2>/dev/null; then
            kill -9 "$SERVER_PID" 2>/dev/null || true
        fi
    fi

    echo "Cleanup completato."
}

trap cleanup EXIT
trap 'exit 130' INT TERM

# ============================================================
# Controlli preliminari
# ============================================================

if [[ ! -x "$SERVER" ]]; then
    echo "Errore: $SERVER non esiste o non è eseguibile."
    exit 1
fi

if [[ ! -x "$CLIENT" ]]; then
    echo "Errore: $CLIENT non esiste o non è eseguibile."
    exit 1
fi

# Directory specifica per questa esecuzione
RUN_ID="$(date '+%Y%m%d_%H%M%S')"
RUN_DIR="$LOG_DIR/run_$RUN_ID"

mkdir -p "$RUN_DIR"

echo "=========================================="
echo "       CLIENT/SERVER RANDOM TEST"
echo "=========================================="
echo
echo "Server : $SERVER"
echo "Client : $CLIENT"
echo "Host   : $HOST"
echo "Port   : $PORT"
echo "Log    : $RUN_DIR"
echo

# ============================================================
# Generazione parametri casuali
# ============================================================

NUM_CLIENTS=$((RANDOM % (MAX_CLIENTS - MIN_CLIENTS + 1) + MIN_CLIENTS))

echo "Numero di client: $NUM_CLIENTS"
echo

# ============================================================
# Avvio server
# ============================================================

echo "Avvio server..."

"$SERVER" \
    -s "$PORT" \
    -d "$RUN_DIR" \
    > /dev/null 2>&1 &

SERVER_PID=$!

echo "Server avviato con PID: $SERVER_PID"

# ============================================================
# Attesa avvio server
# ============================================================

# Attesa iniziale per evitare che i client partano prima
# che il server abbia completato l'inizializzazione.

sleep 1

# Controlliamo che il server sia ancora vivo
if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "ERRORE: il server è terminato prematuramente."
    exit 1
fi

echo "Server operativo."
echo

# ============================================================
# Avvio client
# ============================================================

echo "=========================================="
echo "Avvio client"
echo "=========================================="

TOTAL_MS=0

for ((i=1; i<=NUM_CLIENTS; i++)); do

    # Numero casuale di messaggi
    TIMES=$((RANDOM % (MAX_MESSAGES - MIN_MESSAGES + 1) + MIN_MESSAGES))
    TOTAL_MS=$((TOTAL_MS + TIMES))

    # Delay casuale
    DELAY_MS=$((RANDOM % (MAX_DELAY_MS - MIN_DELAY_MS + 1) + MIN_DELAY_MS))

    # Conversione millisecondi -> nanosecondi
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
echo "Log disponibili in: $RUN_DIR"

# Exit code diverso da zero se almeno un client è fallito
if (( FAILED > 0 )); then
    exit 1
fi

exit 0