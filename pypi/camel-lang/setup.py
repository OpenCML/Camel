import sys
import os
from setuptools import setup
from setuptools.command.install import install

BIN_MAPPING = {
    "linux": "bin/linux/camel",
    "darwin": "bin/macos/camel",
    "win32": "bin/windows/camel.exe"
}

class InstallBinary(install):
    def run(self):
        super().run()
        platform = sys.platform
        src_path = BIN_MAPPING.get(platform)
        if not src_path or not os.path.exists(src_path):
            raise RuntimeError(f"Unsupported platform: {platform}")
        
        target_dir = os.path.join(self.install_scripts, "..", "bin")
        os.makedirs(target_dir, exist_ok=True)
        target_path = os.path.join(target_dir, "camel" + (".exe" if platform == "win32" else ""))
        self.copy_file(src_path, target_path)
        os.chmod(target_path, 0o755)

setup(
    cmdclass={
        'install': InstallBinary,
    },
)
