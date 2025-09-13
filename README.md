# dwarf2cpp

## Installation

```shell
pip install .
```

## Usage

```
Usage: python -m dwarf2cpp [OPTIONS] PATH

Options:
  --base-dir TEXT         Base directory for compilation.  [required]
  -o, --output-path PATH  Output directory for generated files. Defaults to
                          'out' inside the input file's directory.
  --help                  Show this message and exit.
```

### Android

```shell
python -m dwarf2cpp path/to/libminecraftpe.so --base-dir D:/a/_work/1/s
```

### Linux

```shell
python -m dwarf2cpp path/to/bedrock_server --base-dir /mnt/vss/_work/1/s
```

## Known issues

- [ ] constructor parameter names are not synced from definition
- [ ] function const qualifier is missing
- [ ] template parameters are missing
- [ ] accessibility is ignored
- [ ] in `concurrentqueue.h`, `moodycamel::details::identity<char elements[1536]>::type;` should be
  `moodycamel::details::identity<char>::type elements[1536];`
- [ ] in `row_common.cc`, we have duplicate `alignas(32) const YuvConstants kYuvI601Constants;` on the same line.
- [ ] in `ByteOrder.h`, we have
   ```c++
   namespace NetherNet {
      using HostToNetwork16;
      using HostToNetwork32;
      ; using HostToNetwork64;
      using NetworkToHost16;
      using NetworkToHost32;
      ; using NetworkToHost64;
   } // namespace NetherNet
   ```