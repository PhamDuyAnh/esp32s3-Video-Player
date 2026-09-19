"""Convert videos into media pairs for esp32s3-Video-Player.

Input: ``input_videos``. Output: ``output_sd`` (both beside this script).
Each input produces raw baseline MJPEG at 240x240/15 fps and RIFF/WAVE PCM
signed 16-bit little-endian mono at 24000 Hz. Video keeps its aspect ratio and
uses black padding. Audio is filtered and normalized to -18 LUFS/-2 dBFS peak.
The MJPEG starts at the first JPEG marker; it has no custom FPS byte/header.
Existing output is replaced only after both temporary outputs validate.
"""

from __future__ import annotations

import re
import subprocess
import wave
import mmap
from pathlib import Path

import imageio_ffmpeg

SCRIPT_DIR = Path(__file__).resolve().parent
INPUT_DIR = SCRIPT_DIR / "input_videos"
OUTPUT_DIR = SCRIPT_DIR / "output_sd"
FFMPEG = Path(imageio_ffmpeg.get_ffmpeg_exe())
TARGET_WIDTH, TARGET_HEIGHT, TARGET_FPS, TARGET_AUDIO_RATE = 240, 240, 15, 24_000
VIDEO_EXTENSIONS = {".mp4", ".mkv", ".avi", ".mov", ".webm"}
MAX_JPEG_BYTES = 96 * 1024  # Must fit firmware MJPEG_BUFFER_SIZE.


def safe_name(stem: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9._-]+", "_", stem.strip())
    return cleaned.strip("._") or "video"


def run_ffmpeg(command: list[str]) -> None:
    result = subprocess.run(command, text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError("ffmpeg failed:\n" + "\n".join(result.stderr.splitlines()[-20:]))


def inspect_mjpeg(path: Path) -> tuple[int, int]:
    if path.stat().st_size == 0:
        raise ValueError("MJPEG contains no frames")
    position, frames, largest = 0, 0, 0
    # mmap searches the output without copying the entire video into Python RAM.
    with path.open("rb") as source, mmap.mmap(source.fileno(), 0, access=mmap.ACCESS_READ) as data:
        while position < len(data):
            start = data.find(b"\xff\xd8", position)
            if start != position:
                raise ValueError(f"unexpected MJPEG data at byte {position}")
            end = data.find(b"\xff\xd9", start + 2)
            if end < 0:
                raise ValueError(f"truncated JPEG frame at byte {start}")
            frame_bytes = end + 2 - start
            if frame_bytes > MAX_JPEG_BYTES:
                raise ValueError(f"JPEG frame at byte {start} exceeds {MAX_JPEG_BYTES} bytes")
            largest = max(largest, frame_bytes)
            frames += 1
            position = end + 2
    return frames, largest


def inspect_wav(path: Path) -> tuple[int, float]:
    with wave.open(str(path), "rb") as wav:
        actual = (wav.getnchannels(), wav.getsampwidth(), wav.getframerate(), wav.getcomptype())
        if actual != (1, 2, TARGET_AUDIO_RATE, "NONE"):
            raise ValueError("WAV must be PCM signed 16-bit LE, mono, 24000 Hz")
        frames = wav.getnframes()
        return frames, frames / TARGET_AUDIO_RATE


def convert(source: Path) -> None:
    name = safe_name(source.stem)
    final_video, final_audio = OUTPUT_DIR / f"{name}.mjpeg", OUTPUT_DIR / f"{name}.wav"
    temp_video, temp_audio = OUTPUT_DIR / f".{name}.mjpeg.tmp", OUTPUT_DIR / f".{name}.wav.tmp"
    video_filter = (
        f"scale={TARGET_WIDTH}:{TARGET_HEIGHT}:force_original_aspect_ratio=decrease,"
        f"pad={TARGET_WIDTH}:{TARGET_HEIGHT}:(ow-iw)/2:(oh-ih)/2:black,fps={TARGET_FPS}"
    )
    audio_filter = "highpass=f=100,lowpass=f=10500,loudnorm=I=-18:TP=-2:LRA=7"
    print(f"\nConverting: {source.name}")
    try:
        run_ffmpeg([str(FFMPEG), "-hide_banner", "-y", "-i", str(source), "-an",
                    "-vf", video_filter, "-c:v", "mjpeg", "-q:v", "10",
                    "-pix_fmt", "yuvj420p", "-f", "mjpeg", str(temp_video)])
        run_ffmpeg([str(FFMPEG), "-hide_banner", "-y", "-i", str(source), "-vn",
                    "-af", audio_filter, "-c:a", "pcm_s16le", "-ar",
                    str(TARGET_AUDIO_RATE), "-ac", "1", "-f", "wav", str(temp_audio)])
        frame_count, largest = inspect_mjpeg(temp_video)
        _, audio_seconds = inspect_wav(temp_audio)
        video_seconds = frame_count / TARGET_FPS
        if abs(video_seconds - audio_seconds) > 0.15:
            raise ValueError(f"A/V duration differs by {abs(video_seconds-audio_seconds):.3f}s")
        temp_video.replace(final_video)
        temp_audio.replace(final_audio)
        print(f"  OK: {frame_count} frames, {video_seconds:.2f}s, largest JPEG "
              f"{largest} bytes; audio {audio_seconds:.2f}s")
    finally:
        temp_video.unlink(missing_ok=True)
        temp_audio.unlink(missing_ok=True)


def main() -> int:
    INPUT_DIR.mkdir(exist_ok=True)
    OUTPUT_DIR.mkdir(exist_ok=True)
    sources = sorted(p for p in INPUT_DIR.iterdir()
                     if p.is_file() and p.suffix.lower() in VIDEO_EXTENSIONS)
    if not sources:
        print(f"No supported videos found in {INPUT_DIR}")
        return 0
    print(f"Converting {len(sources)} file(s) to {OUTPUT_DIR}")
    failures = 0
    used_names: dict[str, Path] = {}
    for source in sources:
        output_name = safe_name(source.stem).lower()
        if output_name in used_names:
            failures += 1
            print(f"  ERROR: {source.name} and {used_names[output_name].name} "
                  f"would overwrite the same output name")
            continue
        used_names[output_name] = source
        try:
            convert(source)
        except Exception as error:
            failures += 1
            print(f"  ERROR: {error}")
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
