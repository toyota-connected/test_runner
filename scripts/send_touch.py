# SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
# SPDX-License-Identifier: GPLv3
#!/usr/bin/env python3

import argparse
import asyncio
import os
import time

import capnp

capnp.remove_import_hook()
script_dir = os.path.dirname(os.path.abspath(__file__))
schema_path = os.path.join(script_dir, '..', 'capnp', 'test_runner_server.capnp')
test_runner_capnp = capnp.load(schema_path,
                       imports=[os.path.join(script_dir, '..', 'capnp'),
                                os.path.join(script_dir, '..', 'plugins')])

def parse_args():
    parser = argparse.ArgumentParser(description="Send a single touch event to the Test Runner server")
    parser.add_argument("--host", default='localhost:4004', help="HOST:PORT")
    parser.add_argument('--xcoord', '-x', required=True, type=int, help='X coordinate')
    parser.add_argument('--ycoord', '-y', required=True, type=int, help='Y coordinate')
    return parser.parse_args()

async def main(connection, x, y):
    client = capnp.TwoPartyClient(connection)
    server = client.bootstrap().cast_as(test_runner_capnp.TestRunnerService)
    await server.handleSingleTouch({"absX": x, "absY": y, "pressed": True})
    time.sleep(0.01)
    await server.handleSingleTouch({"absX": x, "absY": y, "pressed": False})

async def cmd_main(host, x, y):
    host, port = host.split(":")
    await main(await capnp.AsyncIoStream.create_connection(host=host, port=port), x, y)

if __name__ == "__main__":
    args = parse_args()
    asyncio.run(capnp.run(cmd_main(args.host, args.xcoord, args.ycoord)))