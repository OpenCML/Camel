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
        data = list(f.read(size))
        if limit is not None:
            data = data[:limit]
        return data


def _read_images(path: str, limit: int | None = None) -> list:
    with gzip.open(path, "rb") as f:
        magic, size = struct.unpack(">II", f.read(8))
        assert magic == 2051
        nrows, ncols = struct.unpack(">II", f.read(8))
        flat = f.read(size * nrows * ncols)
        images = [
            [b / 255.0 for b in flat[i : i + nrows * ncols]]
            for i in range(0, len(flat), nrows * ncols)
        ]
        if limit is not None:
            images = images[:limit]
        return images


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

    flat_images = [v for row in images for v in row]
    n, d = len(images), len(images[0]) if images else 0
    return {"images": flat_images, "labels": labels, "shape": [n, d]}
