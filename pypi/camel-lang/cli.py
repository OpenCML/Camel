import sys
import os
import platform
from pathlib import Path

def get_binary_path():
    system = platform.system().lower()
    bin_name = "camel.exe" if system == "windows" else "camel"
    install_dir = Path(os.path.dirname(__file__)).parent.parent.parent / "bin"
    return install_dir / bin_name

def main():
    bin_path = get_binary_path()
    if not bin_path.exists():
        print("Error: Camel binary not found!")
        sys.exit(1)
    os.execv(str(bin_path), sys.argv)
