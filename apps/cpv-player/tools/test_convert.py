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
        cases = [(0, 8), (4, 8), (5, 8), (9, 8), (10, 16), (24, 16),
                 (25, 32), (39, 32), (40, 64), (59, 64), (60, 128),
                 (79, 128), (80, 256), (99, 256), (100, 256)]
        for quality, expected in cases:
            with self.subTest(quality=quality):
                self.assertEqual(convert.palette_limit_for_palette_quality(quality), expected)

    def test_palette_contains_fixed_base_colours_first(self):
        import numpy as np
        generated = np.array([0x1234, 0xF800, 0x07E0], dtype=np.uint16)
        palette = convert.merge_hybrid_palette(generated, 16)
        self.assertEqual(palette[:8].tolist(), list(convert.FIXED_BASE_PALETTE))
        self.assertIn(0x1234, palette.tolist())
        self.assertLessEqual(palette.size, 16)



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
                self.assertEqual(cpv.palette[:8].tolist(), list(convert.FIXED_BASE_PALETTE))
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
