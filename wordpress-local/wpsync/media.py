"""Local media handling: hashing (for upload dedup) and light optimization
before anything goes over the wire to WordPress.
"""
from __future__ import annotations

import hashlib
import io
import mimetypes
from pathlib import Path
from typing import Tuple

from PIL import Image

MAX_DIMENSION = 2560
JPEG_QUALITY = 85
_OPTIMIZABLE = {"image/jpeg", "image/png"}


def file_hash(path: Path) -> str:
    h = hashlib.sha256()
    h.update(path.read_bytes())
    return h.hexdigest()


def guess_mime(path: Path) -> str:
    mime, _ = mimetypes.guess_type(str(path))
    return mime or "application/octet-stream"


def prepare_upload(path: Path, optimize: bool = True) -> Tuple[bytes, str]:
    """Return (bytes, mime_type), downsizing large photos so a phone-camera
    original doesn't get uploaded at full 12+ megapixel size unnecessarily.
    Falls back to the raw file untouched if Pillow can't process it.
    """
    mime = guess_mime(path)
    if optimize and mime in _OPTIMIZABLE:
        try:
            with Image.open(path) as im:
                w, h = im.size
                if max(w, h) > MAX_DIMENSION:
                    scale = MAX_DIMENSION / max(w, h)
                    new_size = (max(1, int(w * scale)), max(1, int(h * scale)))
                    if mime == "image/jpeg" and im.mode != "RGB":
                        im = im.convert("RGB")
                    im = im.resize(new_size, Image.LANCZOS)
                    buf = io.BytesIO()
                    if mime == "image/jpeg":
                        im.save(buf, format="JPEG", quality=JPEG_QUALITY, optimize=True)
                    else:
                        im.save(buf, format="PNG", optimize=True)
                    return buf.getvalue(), mime
        except Exception:
            pass
    return path.read_bytes(), mime
