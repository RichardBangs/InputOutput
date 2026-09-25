# Third-party notices

InputOutput uses the Windows operating system APIs and statically linked components from the LLVM-MinGW toolchain (release 20260616).

- LLVM, libc++, libc++abi, compiler-rt, and libunwind: Apache License 2.0 with LLVM exceptions; see `licenses/LLVM-LICENSE.txt`.
- MinGW-w64 headers, import libraries, and runtime support: see `licenses/MINGW-LICENSE.txt` for the component notices.

The complete notices are included in the distribution. No third-party audio-switching executable or runtime package is needed.

Source and license references:

- https://github.com/mstorsjo/llvm-mingw/tree/20260616
- https://github.com/llvm/llvm-project/blob/main/LICENSE.TXT
- https://github.com/mingw-w64/mingw-w64/blob/master/COPYING
