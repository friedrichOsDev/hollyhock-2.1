#!/usr/bin/env python3
"""Unit tests for CPV host encoder helper functions.

Run: python3 -m unittest discover -s apps/cpv-player/tools -p 'test_*.py'
"""
from __future__ import annotations

import importlib.util
import sys
import unittest
import subprocess
import tempfile
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("cpv_convert", TOOLS / "convert.py")
convert = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = convert
SPEC.loader.exec_module(convert)

PLAY_SPEC = importlib.util.spec_from_file_location("cpv_play", TOOLS / "play.py")
play = importlib.util.module_from_spec(PLAY_SPEC)
sys.modules[PLAY_SPEC.name] = play
PLAY_SPEC.loader.exec_module(play)


class PaletteTests(unittest.TestCase):
    def test_palette_quality_budget(self):
        cases = [(0, 36), (4, 36), (39, 36), (40, 64), (59, 64),
                 (60, 128), (79, 128), (80, 256), (99, 256), (100, 256)]
        for quality, expected in cases:
            with self.subTest(quality=quality):
                self.assertEqual(convert.palette_limit_for_palette_quality(quality), expected)

    def test_legacy_seed_palette_is_exact_and_first(self):
        import numpy as np
        seed_rgb = convert.build_legacy_seed_rgb24()
        seed_palette = convert.build_legacy_seed_palette()
        generated = np.array([0x1234, int(seed_palette[0])], dtype=np.uint16)
        palette = convert.merge_legacy_seed_palette(generated, 64)
        self.assertEqual(len(seed_rgb), 36 * 3)
        self.assertEqual(palette[:36].tolist(), seed_palette.tolist())
        self.assertIn(0x1234, palette.tolist())
        self.assertLessEqual(palette.size, 64)



class PackingTests(unittest.TestCase):
    def test_index_widths(self):
        cases = [(1, 1), (2, 1), (16, 4), (17, 5), (32, 5),
                 (33, 6), (64, 6), (65, 7), (128, 7), (129, 8), (256, 8)]
        for palette_size, expected in cases:
            with self.subTest(palette_size=palette_size):
                self.assertEqual(convert.bits_per_index(palette_size), expected)

    def test_pack_unpack_round_trip(self):
        import numpy as np
        for width in range(1, 9):
            maximum = (1 << width) - 1
            values = np.array([0, maximum, maximum // 2, min(1, maximum), max(0, maximum - 1)], dtype=np.uint8)
            packed = convert.pack_palette_indices(values, width)
            bits = np.unpackbits(np.frombuffer(packed, dtype=np.uint8), bitorder="little")
            recovered = []
            for offset in range(values.size):
                chunk = bits[offset * width:(offset + 1) * width]
                recovered.append(int(np.sum(chunk * (1 << np.arange(width)))))
            self.assertEqual(recovered, values.tolist())

    def test_frame_compression_selects_lz4_or_deflate_and_round_trips(self):
        payload = (b"CPV repeated test data " * 500) + bytes(range(256))
        stored, flags, saved = convert.compress_frame_payload(payload, True)
        self.assertIn(flags, (convert.FRAME_FLAG_LZ4, convert.FRAME_FLAG_DEFLATE))
        self.assertGreater(saved, 0)
        decoded_size = int.from_bytes(stored[:4], "little")
        if flags == convert.FRAME_FLAG_LZ4:
            decoded = convert.lz4_block.decompress(stored[4:], uncompressed_size=decoded_size)
        else:
            import zlib
            decoded = zlib.decompress(stored[4:])
        self.assertEqual(decoded_size, len(payload))
        self.assertEqual(decoded, payload)


class GlobalMotionTests(unittest.TestCase):
    def test_shift_frame_moves_pixels_and_fills_edges(self):
        import numpy as np
        previous = np.arange(20 * 16, dtype=np.uint16)
        shifted = convert.shift_frame(previous, 20, 16, 3, -2, 0x1234)
        image = shifted.reshape(16, 20)
        source = previous.reshape(16, 20)
        self.assertTrue(np.all(image[:2, :] == 0x1234))
        self.assertTrue(np.all(image[2:, :3] == 0x1234))
        np.testing.assert_array_equal(image[2:, 3:], source[:-2, :-3])

    def test_motion_estimator_finds_translation(self):
        import numpy as np
        rng = np.random.default_rng(1234)
        previous = rng.integers(0, 65536, size=20 * 16, dtype=np.uint16)
        current = convert.shift_frame(previous, 20, 16, 3, -2, 0xFFFF)
        self.assertEqual(
            convert.estimate_global_motion(current, previous, 20, 16),
            (3, -2),
        )

class CompressionDecoderTests(unittest.TestCase):
    def test_deflate_decoder_payload(self):
        import zlib
        payload = (b"palette indices and packet streams " * 200)
        compressed = zlib.compress(payload, level=9)
        record_payload = len(payload).to_bytes(4, "little") + compressed
        with tempfile.TemporaryDirectory(prefix="cpv_deflate_") as temp:
            path = Path(temp) / "frame.bin"
            path.write_bytes(record_payload)
            reader = object.__new__(play.CPVFile)
            reader.width = 100
            reader.height = 100
            reader.version_minor = 2
            reader.file_size = path.stat().st_size
            reader.stream = path.open("rb")
            record = play.FrameRecord(
                number=0, frame_type=play.FRAME_I, duration_ms=200,
                flags=play.FRAME_FLAG_DEFLATE, payload_size=len(record_payload),
                file_offset=0, payload_offset=0, start_ms=0,
            )
            try:
                self.assertEqual(reader._read_payload(record), payload)
            finally:
                reader.close()

    def test_deflate_flag_rejected_for_v1_1(self):
        import zlib
        payload = b"small packet data " * 50
        stored = len(payload).to_bytes(4, "little") + zlib.compress(payload, level=9)
        with tempfile.TemporaryDirectory(prefix="cpv_deflate_v11_") as temp:
            path = Path(temp) / "frame.bin"
            path.write_bytes(stored)
            reader = object.__new__(play.CPVFile)
            reader.width = 32
            reader.height = 24
            reader.version_minor = 1
            reader.file_size = path.stat().st_size
            reader.stream = path.open("rb")
            record = play.FrameRecord(
                number=0, frame_type=play.FRAME_I, duration_ms=200,
                flags=play.FRAME_FLAG_DEFLATE, payload_size=len(stored),
                file_offset=0, payload_offset=0, start_ms=0,
            )
            try:
                with self.assertRaises(play.CPVError):
                    reader._read_payload(record)
            finally:
                reader.close()


class IntegrationTests(unittest.TestCase):
    def test_short_video_encode_decode_round_trip(self):
        import numpy as np
        import shutil
        if shutil.which("ffmpeg") is None:
            self.skipTest("FFmpeg is not installed")
        with tempfile.TemporaryDirectory(prefix="cpv_test_") as temp:
            root = Path(temp)
            source = root / "source.mp4"
            output = root / "test.cpv"
            subprocess.run(
                [
                    "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
                    "-f", "lavfi", "-i", "testsrc2=size=64x48:rate=5:duration=2",
                    "-c:v", "mpeg4", "-q:v", "3", str(source),
                ],
                check=True,
            )
            subprocess.run(
                [
                    sys.executable, str(TOOLS / "convert.py"), str(source),
                    "--output", str(output), "--width", "32", "--height", "24",
                    "--fps", "5", "--palette-quality", "10",
                    "--preprocess-quality", "0", "--keyframe-interval", "1",
                    "--background", "000000",
                ],
                check=True,
            )
            cpv = play.CPVFile(output)
            try:
                self.assertGreaterEqual(cpv.palette_count, 8)
                self.assertEqual(cpv.palette[:36].tolist(), convert.build_legacy_seed_palette().tolist())
                framebuffer = np.zeros(cpv.width * cpv.height, dtype=np.uint16)
                for frame_number in range(cpv.frame_count):
                    cpv.decode_frame(frame_number, framebuffer)
                    self.assertEqual(framebuffer.size, cpv.width * cpv.height)
                    self.assertTrue(np.all(framebuffer <= 0xFFFF))
                self.assertGreater(cpv.frame_count, 1)
            finally:
                cpv.close()


if __name__ == "__main__":
    unittest.main()
