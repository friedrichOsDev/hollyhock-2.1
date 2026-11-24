#!/usr/bin/env python3
"""Integration tests for the native CPV v1.3 encoder."""
from __future__ import annotations

import importlib.util
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

TOOLS = Path(__file__).resolve().parent
CONVERTER = TOOLS / "cpv_convert"

PLAY_SPEC = importlib.util.spec_from_file_location("cpv_play", TOOLS / "play.py")
play = importlib.util.module_from_spec(PLAY_SPEC)
assert PLAY_SPEC and PLAY_SPEC.loader
sys.modules[PLAY_SPEC.name] = play
PLAY_SPEC.loader.exec_module(play)


def expected_seed_palette() -> list[int]:
    hues = [
        (255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0),
        (0, 255, 255), (255, 0, 255), (255, 128, 0), (255, 0, 128),
        (0, 255, 128), (128, 0, 255), (139, 69, 19), (128, 128, 128),
    ]
    result = []
    for r, g, b in hues:
        for rr, gg, bb in (
            (r // 2, g // 2, b // 2),
            (r, g, b),
            (r + (255 - r) // 2, g + (255 - g) // 2, b + (255 - b) // 2),
        ):
            result.append(((rr >> 3) << 11) | ((gg >> 2) << 5) | (bb >> 3))
    return result


class NativeEncoderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        if shutil.which("ffmpeg") is None or shutil.which("ffprobe") is None:
            raise unittest.SkipTest("FFmpeg and FFprobe are required")
        subprocess.run(["make", "-C", str(TOOLS)], check=True)
        if not CONVERTER.is_file():
            raise unittest.SkipTest("native converter binary was not built")
        cls.temp = tempfile.TemporaryDirectory(prefix="cpv_native_test_")
        cls.root = Path(cls.temp.name)
        cls.source = cls.root / "source.mp4"
        subprocess.run(
            [
                "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
                "-f", "lavfi", "-i", "testsrc2=size=64x48:rate=5:duration=2",
                "-c:v", "mpeg4", "-q:v", "3", str(cls.source),
            ],
            check=True,
        )

    @classmethod
    def tearDownClass(cls) -> None:
        if hasattr(cls, "temp"):
            cls.temp.cleanup()

    def encode(self, output: Path, palette_quality: int) -> None:
        subprocess.run(
            [
                str(CONVERTER), str(self.source), "--output", str(output),
                "--width", "32", "--height", "24", "--fps", "5",
                "--palette-quality", str(palette_quality),
                "--preprocess-quality", "0", "--keyframe-interval", "1",
                "--background", "000000",
            ],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

    def assert_decodes(self, path: Path) -> None:
        cpv = play.CPVFile(path)
        try:
            self.assertEqual(cpv.width, 32)
            self.assertEqual(cpv.height, 24)
            self.assertEqual(cpv.version_minor, 3)
            self.assertGreaterEqual(cpv.palette_count, 36)
            self.assertEqual(cpv.palette[:36].tolist(), expected_seed_palette())
            self.assertGreater(cpv.frame_count, 1)
            framebuffer = np.zeros(cpv.width * cpv.height, dtype=np.uint16)
            for number in range(cpv.frame_count):
                cpv.decode_frame(number, framebuffer)
                self.assertEqual(framebuffer.size, 32 * 24)
        finally:
            cpv.close()

    def test_native_self_tests(self) -> None:
        subprocess.run([str(CONVERTER), "--self-test"], check=True)

    def test_standard_palette_video_round_trip(self) -> None:
        output = self.root / "standard.cpv"
        self.encode(output, 10)
        self.assert_decodes(output)

    def test_rgb565_preserving_quality_100_round_trip(self) -> None:
        output = self.root / "quality100.cpv"
        self.encode(output, 100)
        self.assert_decodes(output)


if __name__ == "__main__":
    unittest.main()
