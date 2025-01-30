from setuptools import setup, find_packages
from setuptools.command.install import install
import os

class InstallBinary(install):
    def run(self):
        install.run(self)
        # 将二进制文件复制到安装目录
        binary_path = os.path.join(self.install_lib, 'camel-lang', 'your_binary_file')
        os.chmod(binary_path, 0o755)  # 确保二进制文件有可执行权限

setup(
    name='camel-lang',
    version='0.1',
    packages=find_packages(),
    package_data={
        'camel-lang': ['your_binary_file'],  # 包含二进制文件
    },
    include_package_data=True,
    install_requires=[
        # 你的依赖项
    ],
    cmdclass={
        'install': InstallBinary,
    },
    entry_points={
        'console_scripts': [
            'my_command=camel-lang:main',  # 如果你的二进制文件是一个命令行工具
        ],
    },
    author='Zhenjie Wei',
    author_email='kogler_s@outlook.com',
    description='Camel: A Next-Generation Language for AI and High-Performance Computing',
    long_description=open('README.md').read(),
    long_description_content_type='text/markdown',
    url='https://github.com/OpenCML/Camel',
    classifiers=[
        'Programming Language :: Python :: 3',
        'License :: OSI Approved :: MIT License',
        'Operating System :: OS Independent',
    ],
    python_requires='>=3.6',
)
