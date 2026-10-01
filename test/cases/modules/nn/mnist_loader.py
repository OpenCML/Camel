"""
MNIST loader for Camel - download, parse IDX, return dict.
returns {"images": float[], "labels": int[], "shape": [n, 784]}
"""

import gzip
import os
import struct
import urllib.error
import urllib.request

URLS = [
    "https://storage.googleapis.com/cvdf-datasets/mnist",
    "http://yann.lecun.com/exdb/mnist",
]
FILES = {
    "train_images": "train-images-idx3-ubyte.gz",
    "train_labels": "train-labels-idx1-ubyte.gz",
    "test_images": "t10k-images-idx3-ubyte.gz",
    "test_labels": "t10k-labels-idx1-ubyte.gz",
}


def _maybe_download(filename: str, data_dir: str) -> str:
    path = os.path.join(data_dir, filename)
    if os.path.exists(path):
        return path
    os.makedirs(data_dir, exist_ok=True)
    last_err = None
    for base_url in URLS:
        url = f"{base_url}/{filename}"
        try:
            urllib.request.urlretrieve(url, path)
            return path
        except (urllib.error.HTTPError, urllib.error.URLError) as e:
            last_err = e
            continue
    raise RuntimeError(f"Could not download {filename}") from last_err


def _read_labels(path: str, limit: int | None = None) -> list:
    with gzip.open(path, "rb") as f:
        magic, size = struct.unpack(">II", f.read(8))
        assert magic == 2049
        count = size if limit is None else min(size, limit)
        return list(f.read(count))


def _read_images(path: str, limit: int | None = None) -> list:
    # Reads only the requested images: decoding the whole training set dominates a small load.
    with gzip.open(path, "rb") as f:
        magic, size = struct.unpack(">II", f.read(8))
        assert magic == 2051
        nrows, ncols = struct.unpack(">II", f.read(8))
        pixels = nrows * ncols
        count = size if limit is None else min(size, limit)
        flat = f.read(count * pixels)
        return [[b / 255.0 for b in flat[i : i + pixels]] for i in range(0, len(flat), pixels)]


def load_mnist(limit: int = 1000, train: bool = True, data_dir: str = "tmp") -> dict:
    """Load MNIST, return {images: float[], labels: int[], shape: [n, 784]}."""
    if train:
        img_file = _maybe_download(FILES["train_images"], data_dir)
        lbl_file = _maybe_download(FILES["train_labels"], data_dir)
    else:
        img_file = _maybe_download(FILES["test_images"], data_dir)
        lbl_file = _maybe_download(FILES["test_labels"], data_dir)

    images = _read_images(img_file, limit)
    labels = _read_labels(lbl_file, limit)
    assert len(images) == len(labels)

    flat_images = [float(v) for row in images for v in row]
    n, d = len(images), len(images[0]) if images else 0
    assert len(flat_images) == n * d, f"Size mismatch: {len(flat_images)} != {n}*{d}"
    print(f"Loaded {len(flat_images)} images, shape: {n}x{d}")
    return {"images": flat_images, "labels": [int(l) for l in labels], "shape": [int(n), int(d)]}


def ensure(data_dir: str = "tmp") -> str | None:
    """Downloads the training set into `data_dir` if needed. None on success, else why not."""
    try:
        for key in ("train_images", "train_labels"):
            _maybe_download(FILES[key], data_dir)
    except RuntimeError as e:
        cause = f": {e.__cause__}" if e.__cause__ else ""
        return f"{e}{cause}"
    return None


if __name__ == "__main__":
    # Test precondition: `python mnist_loader.py --ensure [data_dir]` fetches the training set into
    # the cache and exits 0, or exits 1 with the reason (e.g. no network).
    import sys

    if len(sys.argv) >= 2 and sys.argv[1] == "--ensure":
        reason = ensure(sys.argv[2] if len(sys.argv) > 2 else "tmp")
        if reason:
            print(reason)
        sys.exit(1 if reason else 0)
    sys.exit(f"usage: {sys.argv[0]} --ensure [data_dir]")
