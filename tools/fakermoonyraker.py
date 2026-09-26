#!/usr/bin/env python3
"""fakermoonyraker: a fake Moonraker, for testing DragonPWR's Klipper source without a printer.

It speaks the small part of Moonraker's websocket API that dc_moonraker uses -
printer.objects.subscribe, server.files.metadata, and notify_status_update
pushed once a second - and lets you drive the printer's state from the
keyboard. Point the plug at this machine's IP, port 7125, in
Settings > Device setup > Moonraker, with the printer source set to Klipper.

    pip install websockets
    python tools/fakermoonyraker.py

Then type a command and press Enter:

    idle         nothing loaded (print_stats "standby")
    print        start a print: preparing below 1 %, printing above
    progress N   set progress to N percent
    pause        pause the print
    resume       resume it
    complete     finish it
    cancel       cancel it (DragonPWR reports this as "error")
    shutdown     Klipper shutdown, e.g. a thermistor fault
    ready        Klipper back up after a shutdown
    bed N        set the bed temperature and target to N (0 = heater off)
    nozzle N     set the nozzle temperature and target to N
    cool         bed and nozzle back to room temperature, heaters off
    silent       stop sending updates (the plug should reconnect after ~45 s)
    status       show what is being reported
    quit
"""

import asyncio
import json
import sys
import threading
import time

try:
    import websockets
except ImportError:
    sys.exit("needs the websockets package: pip install websockets")

PORT = 7125
ROOM = 22.0

state = {
    "webhooks": {"state": "ready", "state_message": "Printer is ready"},
    "print_stats": {"state": "standby", "filename": ""},
    "virtual_sdcard": {"progress": 0.0},
    "heater_bed": {"temperature": ROOM, "target": 0.0},
    "extruder": {"temperature": ROOM, "target": 0.0},
    "toolhead": {"extruder": "extruder"},
}
silent = False
clients = set()
lock = threading.Lock()


def snapshot():
    with lock:
        return json.loads(json.dumps(state))


def heat_towards(heater):
    """Temperatures drift towards target (or room) so updates look alive."""
    goal = heater["target"] or ROOM
    heater["temperature"] += (goal - heater["temperature"]) * 0.2


async def handle(ws, path=None):
    peer = ws.remote_address[0] if ws.remote_address else "?"
    print(f"\n[connected: {peer}]", flush=True)
    clients.add(ws)
    try:
        async for raw in ws:
            try:
                msg = json.loads(raw)
            except ValueError:
                continue
            method, mid = msg.get("method"), msg.get("id")
            if method == "printer.objects.subscribe":
                reply = {"result": {"eventtime": time.monotonic(), "status": snapshot()}}
            elif method == "server.files.metadata":
                reply = {"result": {"filename": msg.get("params", {}).get("filename", ""),
                                    "filament_type": "PLA"}}
            else:
                reply = {"error": {"code": -32601, "message": f"Method not found: {method}"}}
            reply.update({"jsonrpc": "2.0", "id": mid})
            await ws.send(json.dumps(reply))
    except websockets.ConnectionClosed:
        pass
    finally:
        clients.discard(ws)
        print(f"\n[disconnected: {peer}]", flush=True)


async def pusher():
    while True:
        await asyncio.sleep(1)
        with lock:
            heat_towards(state["heater_bed"])
            heat_towards(state["extruder"])
        if silent or not clients:
            continue
        note = json.dumps({"jsonrpc": "2.0", "method": "notify_status_update",
                           "params": [snapshot(), time.monotonic()]})
        for ws in list(clients):
            try:
                await ws.send(note)
            except websockets.ConnectionClosed:
                clients.discard(ws)


def set_print(ps_state, filename=None):
    state["print_stats"]["state"] = ps_state
    if filename is not None:
        state["print_stats"]["filename"] = filename


def command(line):
    global silent
    words = line.split()
    if not words:
        return
    cmd, arg = words[0].lower(), (words[1] if len(words) > 1 else None)
    with lock:
        if cmd == "idle":
            set_print("standby", "")
            state["virtual_sdcard"]["progress"] = 0.0
        elif cmd == "print":
            set_print("printing", "fake_benchy.gcode")
            state["virtual_sdcard"]["progress"] = 0.0
        elif cmd == "progress" and arg:
            state["virtual_sdcard"]["progress"] = max(0.0, min(1.0, float(arg) / 100))
        elif cmd == "pause":
            set_print("paused")
        elif cmd == "resume":
            set_print("printing")
        elif cmd == "complete":
            set_print("complete")
            state["virtual_sdcard"]["progress"] = 1.0
        elif cmd == "cancel":
            set_print("cancelled")
        elif cmd == "shutdown":
            state["webhooks"] = {"state": "shutdown", "state_message": "ADC out of range"}
        elif cmd == "ready":
            state["webhooks"] = {"state": "ready", "state_message": "Printer is ready"}
        elif cmd == "bed" and arg:
            state["heater_bed"]["target"] = float(arg)
        elif cmd == "nozzle" and arg:
            state["extruder"]["target"] = float(arg)
        elif cmd == "cool":
            for h in ("heater_bed", "extruder"):
                state[h] = {"temperature": ROOM, "target": 0.0}
        elif cmd == "silent":
            silent = not silent
            print(f"updates {'paused' if silent else 'resumed'}")
        elif cmd == "status":
            pass
        else:
            print("unknown command - see the top of this file")
            return
    s = snapshot()
    print(f"  klipper={s['webhooks']['state']} print={s['print_stats']['state']} "
          f"progress={s['virtual_sdcard']['progress']:.0%} "
          f"bed={s['heater_bed']['temperature']:.0f}/{s['heater_bed']['target']:.0f} "
          f"nozzle={s['extruder']['temperature']:.0f}/{s['extruder']['target']:.0f} "
          f"clients={len(clients)}")


def keyboard(loop):
    while True:
        try:
            line = input("> ")
        except EOFError:
            line = "quit"
        if line.strip().lower() in ("quit", "exit"):
            loop.call_soon_threadsafe(loop.stop)
            return
        try:
            command(line)
        except ValueError:
            print("that needs a number")


async def main():
    async with websockets.serve(handle, "0.0.0.0", PORT, ping_interval=None):
        print(f"fakermoonyraker on port {PORT} (websocket /websocket). Type a command, or quit.")
        threading.Thread(target=keyboard, args=(asyncio.get_running_loop(),), daemon=True).start()
        await pusher()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except (KeyboardInterrupt, RuntimeError):
        pass
