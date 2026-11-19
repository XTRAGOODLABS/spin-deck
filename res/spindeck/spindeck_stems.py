"""Spin Deck by XTRAGOODLABS: Make Stems.

Splits a song into drums, bass, other and vocals with Demucs (free, MIT licence),
then packs the song and its four parts into one ".stem.mp4" file that Spin Deck
(and other DJ apps) can play with a volume and mute for each part.

    python spindeck_stems.py SONG [--out FOLDER]

Prints lines Spin Deck reads:  STATUS <text>,  DONE <path>,  ERROR <text>.
"""
import argparse
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile

STEMS = [  # the order DJ apps expect inside a stem file
    ("drums", "Drums", "#009E73"),
    ("bass", "Bass", "#D55E00"),
    ("other", "Other", "#CC79A7"),
    ("vocals", "Vocals", "#56B4E9"),
]


def say(kind, text):
    print(kind + " " + text, flush=True)


def ffmpeg_exe():
    try:
        import imageio_ffmpeg
        return imageio_ffmpeg.get_ffmpeg_exe()
    except Exception:
        found = shutil.which("ffmpeg")
        if not found:
            raise RuntimeError("ffmpeg was not found")
        return found


def manifest():
    return {
        "version": 1,
        "mastering_dsp": {
            "compressor": {"enabled": False},
            "limiter": {"enabled": False},
        },
        "stems": [{"name": name, "color": color} for _, name, color in STEMS],
    }


def _boxes(data, start, end):
    """Top-level MP4 boxes in data[start:end] as (offset, header_size, total_size, type)."""
    out = []
    pos = start
    while pos + 8 <= end:
        size, kind = struct.unpack(">I4s", data[pos:pos + 8])
        header = 8
        if size == 1:
            size = struct.unpack(">Q", data[pos + 8:pos + 16])[0]
            header = 16
        elif size == 0:
            size = end - pos
        if size < header:
            break
        out.append((pos, header, size, kind))
        pos += size
    return out


def _grow(data, offset, header, by):
    """Add `by` bytes to the size field of the box at offset."""
    if header == 16:
        size = struct.unpack(">Q", data[offset + 8:offset + 16])[0]
        data[offset + 8:offset + 16] = struct.pack(">Q", size + by)
    else:
        size = struct.unpack(">I", data[offset:offset + 4])[0]
        data[offset:offset + 4] = struct.pack(">I", size + by)


def add_stem_manifest(path):
    """Put the stem manifest in moov/udta/stem. The moov box must be the last box in
    the file (ffmpeg's default), so the audio data does not move."""
    data = bytearray(open(path, "rb").read())
    top = _boxes(data, 0, len(data))
    moov = [b for b in top if b[3] == b"moov"]
    if not moov:
        raise RuntimeError("no moov box")
    m_off, m_head, m_size, _ = moov[0]
    if m_off + m_size != len(data):
        raise RuntimeError("moov is not at the end of the file")
    payload = json.dumps(manifest()).encode("utf-8")
    stem_box = struct.pack(">I4s", 8 + len(payload), b"stem") + payload
    children = _boxes(data, m_off + m_head, m_off + m_size)
    udta = [b for b in children if b[3] == b"udta"]
    if udta:
        u_off, u_head, u_size, _ = udta[0]
        insert_at = u_off + u_size
        data[insert_at:insert_at] = stem_box
        _grow(data, u_off, u_head, len(stem_box))
        _grow(data, m_off, m_head, len(stem_box))
    else:
        new_udta = struct.pack(">I4s", 8 + len(stem_box), b"udta") + stem_box
        data[m_off + m_size:m_off + m_size] = new_udta
        _grow(data, m_off, m_head, len(new_udta))
    with open(path, "wb") as f:
        f.write(data)


def pack(song, parts, out_path, ffmpeg):
    """song + the four part files -> one stem file with five audio tracks."""
    cmd = [ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-i", song]
    for key, _, _ in STEMS:
        cmd += ["-i", parts[key]]
    cmd += ["-map", "0:a:0"]
    for i in range(len(STEMS)):
        cmd += ["-map", f"{i + 1}:a:0"]
    cmd += ["-map_metadata", "0", "-c:a", "aac", "-b:a", "256k", "-ar", "44100", "-ac", "2"]
    for i in range(len(STEMS) + 1):
        cmd += [f"-disposition:a:{i}", "default" if i == 0 else "0"]
    cmd += ["-f", "mp4", out_path]
    subprocess.run(cmd, check=True)
    add_stem_manifest(out_path)


def separate(song, work, ffmpeg):
    """Run Demucs. Returns {"drums": path, ...}."""
    # Demucs reads plain WAV files without any extra tools, so convert first.
    wav = os.path.join(work, "input.wav")
    subprocess.run([ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-i", song,
                    "-vn", "-ar", "44100", "-ac", "2", wav], check=True)
    os.environ.setdefault("PYTORCH_ENABLE_MPS_FALLBACK", "1")
    import torch
    from demucs.separate import main as demucs_main
    devices = ["cpu"]
    if getattr(torch.backends, "mps", None) and torch.backends.mps.is_available():
        devices = ["mps", "cpu"]  # try the Mac's graphics chip first, then the main chip
    last = None
    for device in devices:
        try:
            say("STATUS", "splitting on " + ("graphics chip" if device == "mps" else "main chip"))
            demucs_main(["-n", "htdemucs", "-d", device, "-o", work,
                         "--filename", "{stem}.{ext}", wav])
            last = None
            break
        except SystemExit as e:  # Demucs exits instead of raising on some errors
            last = RuntimeError("the splitter stopped (code %s)" % e.code)
        except Exception as e:
            last = e
        log_error("splitting on " + device)
    if last:
        raise last
    folder = os.path.join(work, "htdemucs")
    parts = {key: os.path.join(folder, key + ".wav") for key, _, _ in STEMS}
    missing = [k for k, p in parts.items() if not os.path.exists(p)]
    if missing:
        raise RuntimeError("Demucs did not make: " + ", ".join(missing))
    return parts


def log_error(where):
    """Keep the full error for troubleshooting, next to the stem splitter."""
    import traceback
    folder = os.path.join(os.path.expanduser("~/Library/Application Support/Spin Deck/stems"))
    try:
        os.makedirs(folder, exist_ok=True)
        with open(os.path.join(folder, "last-error.txt"), "a") as f:
            f.write("---- %s (%s)\n" % (where, sys.argv[1:]))
            f.write(traceback.format_exc())
    except Exception:
        pass


def output_path(song, out_folder):
    base = os.path.splitext(os.path.basename(song))[0]
    folder = out_folder or os.path.dirname(os.path.abspath(song))
    if out_folder:
        os.makedirs(folder, exist_ok=True)
    if not os.access(folder, os.W_OK):
        folder = os.path.join(os.path.expanduser("~/Music"), "Spin Deck Stems")
    os.makedirs(folder, exist_ok=True)
    return os.path.join(folder, base + ".stem.mp4")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("song")
    ap.add_argument("--out", help="folder for the stem file (default: next to the song)")
    ap.add_argument("--parts", help="folder that already has drums/bass/other/vocals.wav (skip Demucs)")
    args = ap.parse_args()
    song = os.path.abspath(args.song)
    if not os.path.exists(song):
        say("ERROR", "song not found: " + song)
        return 1
    try:
        ffmpeg = ffmpeg_exe()
        target = output_path(song, args.out)
        with tempfile.TemporaryDirectory(prefix="spindeck-stems-") as work:
            if args.parts:
                parts = {k: os.path.join(args.parts, k + ".wav") for k, _, _ in STEMS}
            else:
                say("STATUS", "splitting")
                parts = separate(song, work, ffmpeg)
            say("STATUS", "packing")
            tmp_out = os.path.join(work, "out.stem.mp4")
            pack(song, parts, tmp_out, ffmpeg)
            shutil.move(tmp_out, target)
        say("DONE", target)
        return 0
    except Exception as e:  # report and stop; Spin Deck shows the message
        log_error("making stems")
        say("ERROR", (type(e).__name__ + ": " + str(e)).replace("\n", " ")[:200])
        return 1


if __name__ == "__main__":
    sys.exit(main())
