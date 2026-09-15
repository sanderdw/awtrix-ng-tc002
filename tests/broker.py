"""Local broker for integration tests; no external services needed."""
import asyncio
import logging
import sys
import os
from amqtt.broker import Broker


async def main():
    logging.basicConfig(level=logging.ERROR)
    broker = Broker({
        "listeners": {"default": {"type": "tcp", "bind": f"{os.environ.get('AWTRIX_TEST_BROKER_HOST','127.0.0.1')}:{sys.argv[1]}"}},
        "plugins": {"amqtt.plugins.authentication.AnonymousAuthPlugin": {"allow_anonymous": True}},
    })
    await broker.start()
    await asyncio.Event().wait()


asyncio.run(main())
