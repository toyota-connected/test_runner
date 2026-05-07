# SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
# SPDX-License-Identifier: GPLv3
#!/usr/bin/env python3

import argparse
import asyncio
import os

import capnp

capnp.remove_import_hook()
script_dir = os.path.dirname(os.path.abspath(__file__))
schema_path = os.path.join(script_dir, '..', 'capnp', 'test_runner_server.capnp')
test_runner_capnp = capnp.load(schema_path,
                       imports=[os.path.join(script_dir, '..', 'capnp'),
                                os.path.join(script_dir, '..', 'plugins')])

def parse_args():
    parser = argparse.ArgumentParser(description="Retrieve a snapshot recording from the Test Runner server")
    parser.add_argument("--host", default='localhost:4004', help="HOST:PORT")
    return parser.parse_args()

async def main(connection):
    client = capnp.TwoPartyClient(connection)
    server = client.bootstrap().cast_as(test_runner_capnp.TestRunnerService)
    response = await server.getSnapshot()
    print(response.path)

async def cmd_main(host):
    host, port = host.split(":")
    await main(await capnp.AsyncIoStream.create_connection(host=host, port=port))

if __name__ == "__main__":
    args = parse_args()
    asyncio.run(capnp.run(cmd_main(args.host)))