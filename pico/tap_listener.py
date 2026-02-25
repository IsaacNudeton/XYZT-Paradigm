#!/usr/bin/env python3
"""
tap_listener.py — Telemetry Tap Receiver

Catches the 131-byte 8N1 serial frames streaming off GPIO 28 at 2Mbps.
Reassembles the 128×128 wire graph. Pipes to WebSocket.

Frame format per tick:
    [0xAA] [row_index 0-127] [128 bytes edge weights] [XOR checksum]

Full matrix arrives every 128 ticks = 128ms at 1ms/tick.
That's ~7.8 full graph snapshots per second.

Hardware setup:
    Pico GPIO 28 → FTDI RX (or any USB-serial adapter)
    Baud: 2000000 (2Mbps)
    Format: 8N1

Usage:
    python tap_listener.py                    # print stats
    python tap_listener.py --ws              # start WebSocket server
    python tap_listener.py --port /dev/ttyUSB0  # specify serial port

Isaac Oravec & Claude — February 2026
"""

import sys
import time
import json
import struct
import threading
import argparse
from collections import deque

# ═══════════════════════════════════════════════════════════
# CONSTANTS — match telemetry.h + sense.c exactly
# ═══════════════════════════════════════════════════════════

BAUD = 2_000_000
SYNC = 0xAA
ROW_SIZE = 128
FRAME_SIZE = 1 + 1 + ROW_SIZE + 1  # sync + row_idx + data + checksum = 131
MATRIX_SIZE = 128
FULL_MATRIX_TICKS = 128  # one row per tick

# Feature position ranges — match sense.c
FEAT_RANGES = {
    'regularity': (0, 16),    # periodic signals, binned by period
    'correlation': (16, 24),  # multi-pin transitions
    'burst': (24, 32),        # packet boundaries
    'pattern': (32, 40),      # repeating bit sequences
    'activity': (40, 48),     # raw transition density
    'self': (48, 56),         # self-caused features (body awareness)
    'output': (56, 64),       # expression pressure
}

def feature_label(idx):
    """Human-readable label for a feature position."""
    for name, (lo, hi) in FEAT_RANGES.items():
        if lo <= idx < hi:
            return f"{name}[{idx - lo}]"
    return f"wire[{idx}]"

# ═══════════════════════════════════════════════════════════
# WIRE GRAPH STATE — the reassembled adjacency matrix
# ═══════════════════════════════════════════════════════════

class WireGraphState:
    """
    Accumulates row-by-row telemetry into a full 128x128 matrix.
    Thread-safe for concurrent serial read + WebSocket push.
    """
    
    def __init__(self):
        self.matrix = bytearray(MATRIX_SIZE * MATRIX_SIZE)
        self.row_timestamps = [0.0] * MATRIX_SIZE  # when each row last updated
        self.frames_received = 0
        self.frames_dropped = 0  # checksum failures
        self.full_sweeps = 0     # complete 128-row cycles
        self.last_row = -1
        self.lock = threading.Lock()
        self.t_start = time.time()
    
    def update_row(self, row_idx: int, data: bytes):
        """Update one row of the matrix."""
        with self.lock:
            offset = row_idx * MATRIX_SIZE
            self.matrix[offset:offset + MATRIX_SIZE] = data
            self.row_timestamps[row_idx] = time.time()
            self.frames_received += 1
            
            # Detect full sweep
            if row_idx == MATRIX_SIZE - 1 and self.last_row == MATRIX_SIZE - 2:
                self.full_sweeps += 1
            self.last_row = row_idx
    
    def get_snapshot(self) -> dict:
        """Thread-safe snapshot for WebSocket push."""
        with self.lock:
            return {
                'matrix': list(self.matrix),
                'frames': self.frames_received,
                'dropped': self.frames_dropped,
                'sweeps': self.full_sweeps,
                'fps': self.full_sweeps / max(time.time() - self.t_start, 0.001),
                'timestamp': time.time(),
            }
    
    def get_top_edges(self, n: int = 50) -> list:
        """Return the N strongest edges for sparse visualization."""
        edges = []
        with self.lock:
            for i in range(MATRIX_SIZE):
                for j in range(i + 1, MATRIX_SIZE):
                    w = self.matrix[i * MATRIX_SIZE + j]
                    if w > 0:
                        edges.append((i, j, w))
        edges.sort(key=lambda e: e[2], reverse=True)
        return edges[:n]
    
    def get_stats(self) -> dict:
        """Summary statistics of the wire graph."""
        with self.lock:
            weights = list(self.matrix)

        nonzero = sum(1 for w in weights if w > 0)
        crystallized = sum(1 for w in weights if w > 230)  # near saturate
        total = MATRIX_SIZE * MATRIX_SIZE
        mean = sum(weights) / total if total > 0 else 0
        max_w = max(weights)
        total_weight = sum(weights)

        return {
            'nonzero_edges': nonzero // 2,  # symmetric, count once
            'crystallized_edges': crystallized // 2,
            'dissolved_pct': (total - nonzero) / total * 100,
            'mean_weight': mean,
            'max_weight': max_w,
            'total_weight': total_weight,
            'total_possible': total // 2,
        }

    def get_feature_clusters(self) -> dict:
        """Which feature categories are wired together?"""
        with self.lock:
            m = self.matrix

        clusters = {}
        for name, (lo, hi) in FEAT_RANGES.items():
            # Total weight FROM this feature range TO each other range
            connections = {}
            for other_name, (olo, ohi) in FEAT_RANGES.items():
                w = 0
                for i in range(lo, hi):
                    for j in range(olo, ohi):
                        w += m[i * MATRIX_SIZE + j]
                if w > 0:
                    connections[other_name] = w
            if connections:
                clusters[name] = connections

        return clusters

    def is_converged(self, history: list, window: int = 10) -> bool:
        """Has the total weight stabilized? (convergence detection)"""
        if len(history) < window:
            return False
        recent = history[-window:]
        if recent[0] == 0:
            return False
        delta = abs(recent[-1] - recent[0]) / max(recent[0], 1)
        return delta < 0.02  # less than 2% change over window


# ═══════════════════════════════════════════════════════════
# SERIAL READER — parse frames from FTDI adapter
# ═══════════════════════════════════════════════════════════

def read_serial(port: str, state: WireGraphState, verbose: bool = False):
    """
    Continuously read 131-byte frames from serial port.
    Sync on 0xAA byte. Verify XOR checksum. Update state.
    """
    import serial
    
    ser = serial.Serial(port, BAUD, timeout=0.1)
    print(f"[TAP] Listening on {port} at {BAUD} baud")
    print(f"[TAP] Waiting for sync (0xAA)...")
    
    buf = bytearray()
    synced = False
    
    while True:
        # Read available bytes
        chunk = ser.read(512)
        if not chunk:
            continue
        buf.extend(chunk)
        
        while len(buf) >= FRAME_SIZE:
            # Hunt for sync byte
            if not synced:
                try:
                    idx = buf.index(SYNC)
                    buf = buf[idx:]  # align to sync
                    synced = True
                except ValueError:
                    buf.clear()
                    continue
            
            if len(buf) < FRAME_SIZE:
                break
            
            # Parse frame
            sync = buf[0]
            row_idx = buf[1]
            data = bytes(buf[2:2 + ROW_SIZE])
            checksum = buf[2 + ROW_SIZE]
            
            # Verify sync
            if sync != SYNC:
                synced = False
                buf = buf[1:]  # skip one byte, re-sync
                continue
            
            # Verify row index in range
            if row_idx >= MATRIX_SIZE:
                synced = False
                buf = buf[1:]
                state.frames_dropped += 1
                continue
            
            # Verify XOR checksum
            computed = row_idx
            for b in data:
                computed ^= b
            
            if computed != checksum:
                synced = False
                buf = buf[1:]
                state.frames_dropped += 1
                if verbose:
                    print(f"[TAP] Checksum fail row {row_idx}: "
                          f"got 0x{checksum:02x} expected 0x{computed:02x}")
                continue
            
            # Valid frame — update state
            state.update_row(row_idx, data)
            buf = buf[FRAME_SIZE:]  # consume frame
            
            if verbose and state.frames_received % 128 == 0:
                stats = state.get_stats()
                print(f"[TAP] Sweep #{state.full_sweeps}: "
                      f"{stats['nonzero_edges']} edges, "
                      f"{stats['crystallized_edges']} crystallized, "
                      f"max={stats['max_weight']}")


# ═══════════════════════════════════════════════════════════
# WEBSOCKET SERVER — push graph state to Three.js visualizer
# ═══════════════════════════════════════════════════════════

def run_websocket(state: WireGraphState, host: str = '0.0.0.0', ws_port: int = 8765):
    """
    WebSocket server that pushes wire graph snapshots.
    
    Two message types pushed to clients:
      {"type": "edges", "data": [...]}    — top 200 strongest edges
      {"type": "stats", "data": {...}}    — summary statistics
    
    Edges pushed at ~8Hz (every full sweep).
    Stats pushed at ~2Hz.
    """
    import asyncio
    import websockets
    
    clients = set()
    
    async def handler(websocket):
        clients.add(websocket)
        print(f"[WS] Client connected ({len(clients)} total)")
        try:
            async for msg in websocket:
                # Client can request specific data
                if msg == 'full':
                    snapshot = state.get_snapshot()
                    await websocket.send(json.dumps({
                        'type': 'full_matrix',
                        'data': snapshot
                    }))
                elif msg == 'edges':
                    edges = state.get_top_edges(200)
                    await websocket.send(json.dumps({
                        'type': 'edges',
                        'data': [{'src': s, 'dst': d, 'weight': w}
                                 for s, d, w in edges]
                    }))
        except websockets.exceptions.ConnectionClosed:
            pass
        finally:
            clients.discard(websocket)
            print(f"[WS] Client disconnected ({len(clients)} total)")
    
    async def push_loop():
        """Push edge updates to all clients at ~8Hz."""
        last_sweep = 0
        while True:
            await asyncio.sleep(0.125)  # 8Hz
            if state.full_sweeps > last_sweep and clients:
                last_sweep = state.full_sweeps
                edges = state.get_top_edges(200)
                stats = state.get_stats()
                msg = json.dumps({
                    'type': 'update',
                    'edges': [{'s': s, 'd': d, 'w': w} for s, d, w in edges],
                    'stats': stats,
                    'sweep': state.full_sweeps,
                })
                # Push to all connected clients
                if clients:
                    await asyncio.gather(
                        *[c.send(msg) for c in clients],
                        return_exceptions=True
                    )
    
    async def main():
        print(f"[WS] Server starting on ws://{host}:{ws_port}")
        async with websockets.serve(handler, host, ws_port):
            await push_loop()
    
    asyncio.run(main())


# ═══════════════════════════════════════════════════════════
# SIMULATION MODE — for testing without hardware
# ═══════════════════════════════════════════════════════════

def simulate_telemetry(state: WireGraphState):
    """
    Generate fake telemetry data as if a Pico were connected.
    Simulates a protocol crystallizing over time.
    """
    import random
    
    print("[SIM] Simulating telemetry — no hardware needed")
    print("[SIM] Simulating USB-like protocol crystallization...")
    
    tick = 0
    # Simulate: features 0-3 always co-occur (like SYNC pattern)
    # Features 10-15 sometimes co-occur (like variable data)
    
    while True:
        row = tick % MATRIX_SIZE
        data = bytearray(ROW_SIZE)
        
        # Strong cluster: rows 0-3 connected to each other
        if row < 4:
            for j in range(4):
                # Strengthen over time, approach saturation
                strength = min(255, tick // 10 + random.randint(0, 20))
                data[j] = strength
        
        # Weak cluster: rows 10-15, weaker connections
        if 10 <= row < 16:
            for j in range(10, 16):
                strength = min(128, tick // 50 + random.randint(0, 10))
                data[j] = strength
        
        # Random noise everywhere else — should decay
        for j in range(ROW_SIZE):
            if data[j] == 0 and random.random() < 0.05:
                data[j] = random.randint(1, 30)
        
        state.update_row(row, bytes(data))
        tick += 1
        time.sleep(0.001)  # 1ms per tick, matches firmware


# ═══════════════════════════════════════════════════════════
# MAIN
# ═══════════════════════════════════════════════════════════

def main():
    parser = argparse.ArgumentParser(description='XYZT Telemetry Tap Listener')
    parser.add_argument('--port', default='/dev/ttyUSB0',
                        help='Serial port (default: /dev/ttyUSB0)')
    parser.add_argument('--ws', action='store_true',
                        help='Start WebSocket server for visualization')
    parser.add_argument('--ws-port', type=int, default=8765,
                        help='WebSocket port (default: 8765)')
    parser.add_argument('--simulate', action='store_true',
                        help='Simulate telemetry without hardware')
    parser.add_argument('--verbose', '-v', action='store_true',
                        help='Print per-sweep diagnostics')
    args = parser.parse_args()
    
    state = WireGraphState()
    
    # Start serial reader or simulator in background thread
    if args.simulate:
        serial_thread = threading.Thread(
            target=simulate_telemetry, args=(state,), daemon=True)
    else:
        serial_thread = threading.Thread(
            target=read_serial, args=(args.port, state, args.verbose), daemon=True)
    serial_thread.start()
    
    if args.ws:
        # WebSocket server runs in main thread
        run_websocket(state, ws_port=args.ws_port)
    else:
        # Just print stats to terminal
        print("[TAP] Printing stats every second (Ctrl+C to stop)")
        print("[TAP] Use --ws to start WebSocket server for 3D visualization")
        weight_history = []
        converged = False
        try:
            while True:
                time.sleep(1.0)
                stats = state.get_stats()
                edges = state.get_top_edges(5)
                weight_history.append(stats['total_weight'])

                # Convergence detection
                was_converged = converged
                converged = state.is_converged(weight_history, window=10)
                if converged and not was_converged:
                    print("\n  *** CONVERGED — wire graph has crystallized ***")
                elif not converged and was_converged:
                    print("\n  *** DIVERGED — new structure detected ***")

                print(f"\n[Sweep {state.full_sweeps:4d}] "
                      f"frames={state.frames_received} "
                      f"dropped={state.frames_dropped} "
                      f"fps={state.full_sweeps / max(time.time() - state.t_start, 1):.1f}"
                      f"{' [STABLE]' if converged else ''}")
                print(f"  Edges: {stats['nonzero_edges']} active, "
                      f"{stats['crystallized_edges']} crystallized, "
                      f"{stats['dissolved_pct']:.1f}% dissolved")
                print(f"  Weight: total={stats['total_weight']} "
                      f"mean={stats['mean_weight']:.2f} "
                      f"max={stats['max_weight']}")

                if edges:
                    print(f"  Top: ", end='')
                    for s, d, w in edges:
                        print(f"[{feature_label(s)}↔{feature_label(d)}:{w}] ", end='')
                    print()

                # Show inter-category connections every 10 sweeps
                if state.full_sweeps > 0 and state.full_sweeps % 10 == 0:
                    clusters = state.get_feature_clusters()
                    if clusters:
                        print("  Clusters:")
                        for feat, conns in clusters.items():
                            top = sorted(conns.items(), key=lambda x: -x[1])[:3]
                            links = ', '.join(f"{n}={w}" for n, w in top)
                            print(f"    {feat} → {links}")
        except KeyboardInterrupt:
            print("\n[TAP] Stopped.")


if __name__ == '__main__':
    main()
