#!/usr/bin/env python3
"""Drive an owned PPSSPP instance through its loopback WebSocket debugger.

Requires websockets. Enable RemoteDebuggerOnStartup/RemoteDebuggerLocal and set
RemoteISOPort in that instance's config. Profile reads require the matching
`psp-nm -n openrbx_psp.elf` map; never release that ELF as the PSP executable.
Protocol: PPSSPP v1.20.4 Core/Debugger/WebSocket/{Input,CPUCore,Memory}Subscriber.cpp.
No system keyboard input, private C++ layout assumptions or memory writes.
"""
import argparse
import asyncio
import base64
import json
import struct
import time
from pathlib import Path

import websockets

FIELDS = ("version fps heapBytes liveInstances arenaSlots collectedInstances "
          "collectionMs physicsMs player character health grounded runtimeError "
          "luaErrors x y z").split()
WIRE = struct.Struct("<IfIIIIffIIfIIIfff")


async def run(args):
    async with websockets.connect(
        f"ws://127.0.0.1:{args.port}/debugger", subprotocols=["debugger.ppsspp.org"],
        max_size=8 * 1024 * 1024, open_timeout=5
    ) as ws:
        serial = 0

        async def request(event, **params):
            nonlocal serial
            serial += 1
            ticket = str(serial)
            await ws.send(json.dumps(dict(event=event, ticket=ticket, **params)))
            while True:
                response = json.loads(await asyncio.wait_for(ws.recv(), 20))
                if (str(response.get("ticket", "")) == ticket or
                    event in ("cpu.stepping", "cpu.resume") and response.get("event") == event):
                    if response.get("event") == "error":
                        raise RuntimeError(response)
                    return response

        await request("version")
        if args.action == "button":
            for button in args.buttons:
                await request("input.buttons.press", button=button, duration=8)
                await asyncio.sleep(.25)
            return
        if args.symbols is None:
            raise ValueError("--symbols must name the matching psp-nm map")
        symbol = None
        for line in args.symbols.read_text().splitlines():
            fields = line.split()
            if len(fields) == 3 and fields[2] == "openrbxProfile":
                symbol = int(fields[0], 16)
        if symbol is None:
            raise ValueError("Map has no versioned openrbxProfile symbol")
        modules = (await request("hle.module.list"))["modules"]
        module = next(m for m in modules if "OpenRBX PSP" in m["name"] and m["isActive"])
        address = module["address"] + symbol

        async def profile():
            was_stepping = (await request("cpu.status"))["stepping"]
            if not was_stepping:
                await request("cpu.stepping")
            try:
                response = await request("memory.read", address=address, size=WIRE.size)
                result = dict(zip(FIELDS, WIRE.unpack(base64.b64decode(response["base64"]))))
                if result["version"] != 1:
                    raise ValueError("Profile ABI mismatch; map may not match PBP")
                return result
            finally:
                if not was_stepping:
                    await request("cpu.resume")

        initial = await profile()
        print(json.dumps(dict(cycle=0, **initial)), flush=True)
        if args.action == "profile":
            return
        if not initial["player"] or initial["health"] <= 0:
            raise AssertionError("Load and settle the place before gameplay checks")
        if args.action == "steady":
            samples = []
            until = time.monotonic() + args.seconds
            while time.monotonic() < until:
                await asyncio.sleep(.35)
                current = await profile()
                samples.append(current)
                print(json.dumps(dict(sample=len(samples), **current)), flush=True)
            good = sum(25 <= s["fps"] <= 30.5 and s["physicsMs"] < 25 and
                       not s["runtimeError"] for s in samples)
            if not samples or good < len(samples) * .8:
                raise AssertionError(f"Steady performance regression: {good}/{len(samples)} near-30 FPS samples")
            print(f"PASS: {good}/{len(samples)} steady samples near 30 FPS without repeated world rebuild cost", flush=True)
            return
        previous = initial["character"]
        for cycle in range(1, args.count + 1):
            await request("input.buttons.press", button="square", duration=8)
            await asyncio.sleep(args.delay)
            current = await profile()
            print(json.dumps(dict(cycle=cycle, **current)), flush=True)
            if args.steady:
                await asyncio.sleep(1)
                current = await profile()
                print(json.dumps(dict(cycle=cycle, phase="steady", **current)), flush=True)
                if not (25 <= current["fps"] <= 30.5 and current["physicsMs"] < 25):
                    raise AssertionError("World rebuild cost persists after respawn settling")
            if (current["runtimeError"] or current["health"] <= 0 or
                current["player"] != initial["player"] or current["character"] == previous):
                raise AssertionError("Character did not die/respawn cleanly")
            if (current["arenaSlots"] > initial["arenaSlots"] + 64 or
                current["liveInstances"] > initial["liveInstances"] + 8 or
                current["heapBytes"] > initial["heapBytes"] + 65536):
                raise AssertionError("Respawn memory did not remain bounded")
            if current["luaErrors"] != initial["luaErrors"]:
                raise AssertionError("New Lua error during respawn checks")
            previous = current["character"]
        print(f"PASS: {args.count} PSP-emulated resets, fresh generations, stable Player and bounded memory", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=24689)
    parser.add_argument("--symbols", type=Path)
    sub = parser.add_subparsers(dest="action", required=True)
    button = sub.add_parser("button")
    button.add_argument("buttons", nargs="+")
    sub.add_parser("profile")
    steady = sub.add_parser("steady")
    steady.add_argument("--seconds", type=float, default=6)
    stress = sub.add_parser("respawns")
    stress.add_argument("count", type=int)
    stress.add_argument("--delay", type=float, default=4.5)
    stress.add_argument("--steady", action="store_true", help="Also verify near-30 FPS after every reset")
    asyncio.run(run(parser.parse_args()))


if __name__ == "__main__":
    main()
