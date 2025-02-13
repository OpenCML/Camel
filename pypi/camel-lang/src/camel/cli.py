import sys
import os
import platform
import subprocess
from pathlib import Path
from typing import Optional


def get_binary_path():
    system = platform.system().lower()
    bin_name = "camel.exe" if system == "windows" else "camel"
    install_dir = Path(os.path.dirname(__file__)) / ".cmlenv" / "bin"
    return install_dir / bin_name


# Windows 特殊处理
if platform.system() == "Windows":
    import msvcrt
    import ctypes
else:
    import termios
    import tty


class TerminalResetter:
    """跨平台终端状态恢复器"""

    def __enter__(self):
        # self.original_mode = self._save_terminal()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        # self._restore_terminal(self.original_mode)
        # # Windows 额外清理
        # if platform.system() == "Windows":
        #     self._clean_windows_console()
        pass

    def _save_terminal(self) -> Optional[bytes]:
        """保存终端原始状态"""
        if platform.system() == "Windows":
            # 获取控制台模式
            kernel32 = ctypes.windll.kernel32
            mode = ctypes.c_uint32()
            handle = kernel32.GetStdHandle(-10)  # STD_INPUT_HANDLE
            kernel32.GetConsoleMode(handle, ctypes.byref(mode))
            return mode.value
        else:
            # Unix 保存 termios 设置
            return termios.tcgetattr(sys.stdin)

    def _restore_terminal(self, original: Optional[bytes]):
        """恢复终端状态"""
        if platform.system() == "Windows" and original is not None:
            kernel32 = ctypes.windll.kernel32
            handle = kernel32.GetStdHandle(-10)
            kernel32.SetConsoleMode(handle, original)
        elif original is not None:
            termios.tcsetattr(sys.stdin, termios.TCSADRAIN, original)

    def _clean_windows_console(self):
        """Windows 控制台特殊清理"""
        os.system("cls")  # 清屏
        # 重置颜色
        ctypes.windll.kernel32.SetConsoleTextAttribute(
            ctypes.windll.kernel32.GetStdHandle(-11), 7  # 灰色
        )


def camel():
    with TerminalResetter():
        bin_path = get_binary_path()
        if not bin_path.exists():
            print("Error: Camel binary not found!")
            sys.exit(1)

        # 启动子进程
        proc = subprocess.Popen(
            [str(bin_path)] + sys.argv[1:],
            stdin=sys.stdin,
            stdout=sys.stdout,
            stderr=sys.stderr,
            # 跨平台信号处理
            creationflags=(
                subprocess.CREATE_NEW_PROCESS_GROUP if platform.system() == "Windows"
                else subprocess.CREATE_NEW_SESSION
            )
        )

        try:
            proc.wait()
        except KeyboardInterrupt:
            if platform.system() == "Windows":
                # Windows 使用特殊信号
                proc.send_signal(subprocess.signal.CTRL_C_EVENT)
            else:
                proc.send_signal(subprocess.signal.SIGINT)
            proc.wait()

def cpm():
    print("Hello! Camel Package Manager!")