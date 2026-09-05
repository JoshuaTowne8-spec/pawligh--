import asyncio
import unittest

from gateway.app.main import normalize_emotion, receive_device_messages


class FakeDevice:
    def __init__(self, messages: list[dict]):
        self.messages = messages

    async def receive(self) -> dict:
        return self.messages.pop(0)


class DeviceQueueTests(unittest.IsolatedAsyncioTestCase):
    async def test_accepts_one_5120_byte_audio_batch(self) -> None:
        audio = bytes(5120)
        device = FakeDevice([
            {"type": "websocket.receive", "bytes": audio},
            {"type": "websocket.disconnect", "code": 1000},
        ])
        queue: asyncio.Queue[bytes | str] = asyncio.Queue(maxsize=12)

        await receive_device_messages(device, queue)

        self.assertEqual(await queue.get(), audio)

    async def test_full_queue_discards_oldest_batch(self) -> None:
        old = bytes([1]) * 5120
        new = bytes([2]) * 5120
        queue: asyncio.Queue[bytes | str] = asyncio.Queue(maxsize=1)
        queue.put_nowait(old)
        device = FakeDevice([
            {"type": "websocket.receive", "bytes": new},
            {"type": "websocket.disconnect", "code": 1000},
        ])

        await receive_device_messages(device, queue)

        self.assertEqual(await queue.get(), new)

    def test_five_emotion_mapping(self) -> None:
        self.assertEqual(normalize_emotion("happy", ""), "happy")
        self.assertEqual(normalize_emotion("neutral", "我很想你"), "miss")
        self.assertEqual(normalize_emotion("sad", "谢谢你陪着我"), "warm")
        self.assertEqual(normalize_emotion("angry", ""), "sad")
        self.assertEqual(normalize_emotion("neutral", ""), "calm")


if __name__ == "__main__":
    unittest.main()
