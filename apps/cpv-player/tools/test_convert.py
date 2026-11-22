#!/usr/bin/env python3
"""Unit tests for CPV host encoder helper functions.

Run: python3 -m unittest discover -s apps/cpv-player/tools -p 'test_*.py'
"""
from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("cpv_convert", TOOLS / "convert.py")
convert = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = convert
SPEC.loader.exec_module(convert)


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

    def test_anchor_colours_are_from_video_sample(self):
        # RGB565: black, near-white, red, green, blue, plus an unrelated colour.
        colours = __import__("numpy").array(
            [0x0000, 0xFFFF, 0xF800, 0x07E0, 0x001F, 0x1234],
            dtype="uint16",
        )
        counts = __import__("numpy").array([1000, 800, 700, 600, 500, 2], dtype="uint64")
        anchors = convert.select_hybrid_anchors(colours, counts, 16)
        self.assertGreaterEqual(len(anchors), 1)
        self.assertLessEqual(len(anchors), 4)
        self.assertTrue(set(map(int, anchors)).issubset(set(map(int, colours))))

    def test_anchor_budget_small_palette(self):
        import numpy as np
        colours = np.array([0x0000, 0xFFFF, 0xF800, 0x07E0], dtype=np.uint16)
        counts = np.array([500, 400, 300, 200], dtype=np.uint64)
        anchors = convert.select_hybrid_anchors(colours, counts, 4)
        self.assertLessEqual(anchors.size, 2)
        self.assertTrue(set(map(int, anchors)).issubset(set(map(int, colours))))

    def test_empty_sample(self):
        import numpy as np
        anchors = convert.select_hybrid_anchors(
            np.empty(0, dtype=np.uint16), np.empty(0, dtype=np.uint64), 16
        )
        self.assertEqual(anchors.size, 0)


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
        if convert.lz4_block is None:
            self.skipTest("python-lz4 not installed")
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


if __name__ == "__main__":
    unittest.main()
