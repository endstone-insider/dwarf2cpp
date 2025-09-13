# dwarf2cpp

[![Build Status](https://github.com/endstone-insider/dwarf2cpp/actions/workflows/build.yml/badge.svg)](https://github.com/yourusername/dwarf2cpp/actions)
![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)
![Python Version](https://img.shields.io/badge/python-3.10%2B-blue.svg)

Generate C++ headers from DWARF Debugging Information Format (DWARF), targeting Minecraft: Bedrock Edition binaries.

> [!WARNING]
> This tool requires binaries that include DWARF debug information in order to generate headers. Such binaries are not
> publicly available for Minecraft: Bedrock Edition. You must first have access to debug-enabled binaries. This
> tool cannot generate any useful output without them.

---

## Installation

dwarf2cpp is a standard Python package.

### Requirements

* **Python 3.9+**
* **C++ Compiler**
  Since dwarf2cpp uses a **pybind11 binding for LLVM's DWARF DebugInfo module**, you need a working C++ toolchain to
  build it:

    * On Windows: **MSVC (Visual Studio Build Tools)**
    * On Linux: **GCC (g++)**
    * On macOS: **Apple Clang (Xcode Command Line Tools)**
* **CMake** (required to configure and build the native extension)
* **LLVM with DWARF DebugInfo libraries** available (either pre-installed or fetched by the build process, depending on
  your setup).

Make sure your compiler and CMake are available on your system `PATH` before installation.

### Build from Sources

```
git clone https://github.com/yourusername/dwarf2cpp.git
cd dwarf2cpp
pip install .
```

After installation, the tool can be invoked using `python -m dwarf2cpp`.

### Prebuilt Wheels

For convenience, **prebuilt wheels** are available from the GitHub Actions pages of this repository.
These wheels include the required C++ extension, so you do not need to have LLVM or a compiler toolchain installed
locally.

1. Visit the **Actions** tab on GitHub.
2. Select the latest successful build for your platform (Windows, Linux, or macOS).
3. Download the wheel artifact and install it with:

   ```
   pip install dwarf2cpp-<version>-<platform>.whl
   ```

This option is recommended if you simply want to use `dwarf2cpp` without dealing with compiler or LLVM setup.

## Usage

```
Usage: python -m dwarf2cpp [OPTIONS] PATH

Options:
  --base-dir TEXT         Base directory for compilation.  [required]
  -o, --output-path PATH  Output directory for generated files. Defaults to
                          'out' inside the input file's directory.
  --help                  Show this message and exit.
```

The `PATH` argument should point to a binary (e.g., `libminecraftpe.so` or `bedrock_server`) that contains DWARF debug
information.

* `--base-dir` should point to the root directory used during compilation. This helps resolve relative include paths
  when reconstructing headers.
* `--output-path` lets you control where generated headers will be stored. If not provided, the tool creates an `out/`
  folder next to the input file.

## Examples

### Extract from Android

```
python -m dwarf2cpp path/to/libminecraftpe.so --base-dir D:/a/_work/1/s
```

### Extract from Linux Server

```
python -m dwarf2cpp path/to/bedrock_server --base-dir /mnt/vss/_work/1/s
```

## Motivation / Purpose

Minecraft: Bedrock Edition does not provide public headers for its binaries, making modding more difficult.
This project exists to bridge that gap by reconstructing usable C++ headers directly from DWARF debug information.

Typical use cases include:

* Analysing the internals of Minecraft: Bedrock Edition.
* Supporting plugin frameworks such as **[Endstone](https://github.com/EndstoneMC/endstone)**.
* Research on automated source reconstruction from DWARF.

## Limitations

* Generated headers may not always compile out-of-the-box. Manual fixes may be necessary.
* Templates, inline functions, and macros cannot always be reconstructed faithfully.
* Only works with binaries compiled with DWARF debug info. Stripped or release binaries will not work.
* Tested mainly on Android (`libminecraftpe.so`) and Linux (`bedrock_server`) builds.

## Acknowledgements

This project makes use of the following open source technologies:

* [LLVM Project](https://llvm.org/) - DWARF DebugInfo parser.
* [pybind11](https://pybind11.readthedocs.io/) - C++/Python bindings.
* [click](https://click.palletsprojects.com/) - Command-line interface framework.

## Security / Legal Disclaimer

> [!IMPORTANT]
> This tool is provided for research and educational purposes only.
> It is **not affiliated with Mojang or Microsoft**.

> [!WARNING]
> Do not redistribute or publish headers generated from proprietary binaries without proper rights.
> Respect the terms of service and licensing agreements for any binaries you analyse.

## Contributing

Contributions are welcome! If you encounter issues or have suggestions for improvements, please open an issue or a pull
request on GitHub.
When contributing code, please ensure that your changes are well-documented and tested.

## License

This project is distributed under the MIT License.
See the [LICENSE] file for more details.
