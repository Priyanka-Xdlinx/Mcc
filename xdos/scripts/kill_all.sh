#!/usr/bin/env bash
# Emergency cleanup of leftover XDOS processes.
pkill -INT  -f 'xdos_(eps|adcs|gps|payload|xlink|uhf)_sim' 2>/dev/null || true
pkill -INT  -f 'xdos_obc'                2>/dev/null || true
pkill -INT  -f 'xdos_gs'                 2>/dev/null || true
sleep 0.3
pkill -KILL -f 'xdos_(eps|adcs|gps|payload|xlink|uhf)_sim' 2>/dev/null || true
pkill -KILL -f 'xdos_obc'                2>/dev/null || true
pkill -KILL -f 'xdos_gs'                 2>/dev/null || true
echo "all XDOS processes terminated"
