# SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
# SPDX-License-Identifier: GPLv3
#!/usr/bin/env python3

import argparse
import asyncio
import os

import capnp
from PIL import Image

capnp.remove_import_hook()
script_dir = os.path.dirname(os.path.abspath(__file__))
schema_path = os.path.join(script_dir, '..', 'capnp', 'test_runner_server.capnp')
test_runner_capnp = capnp.load(schema_path,
                       imports=[os.path.join(script_dir, '..', 'capnp'),
                                os.path.join(script_dir, '..', 'plugins')])

def parse_args():
    parser = argparse.ArgumentParser(description="Capture a screenshot from the Test Runner server")
    parser.add_argument("--host", default='localhost:4004', help="HOST:PORT")
    parser.add_argument("--output", default='screenshot.png', help="Output PNG file path")
    return parser.parse_args()

async def main(connection, output_path):
    client = capnp.TwoPartyClient(connection)
    server = client.bootstrap().cast_as(test_runner_capnp.TestRunnerService)

    response = await server.takeScreenshot()
    raw = bytes(response.imageData)
    print(f"Received {len(raw)} bytes, {response.width}x{response.height}")
    image = Image.frombytes('RGB', (response.width, response.height), raw, 'raw', 'BGRX')
    image.save(output_path)
    print(f"Screenshot saved to {output_path}")

async def cmd_main(host, output_path):
    host, port = host.split(":")
    await main(await capnp.AsyncIoStream.create_connection(host=host, port=port), output_path)

if __name__ == "__main__":
    args = parse_args()
    asyncio.run(capnp.run(cmd_main(args.host, args.output)))