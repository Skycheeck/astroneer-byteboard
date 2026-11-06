# Third-party notices

## Dear ImGui

The overlay DLL is built with [Dear ImGui](https://github.com/ocornut/imgui) 1.92.9b, included as a git
submodule in `overlay/third_party/imgui`. Any binary that contains it must carry its licence:

```
The MIT License (MIT)

Copyright (c) 2014-2026 Omar Cornut

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## zlib

The overlay DLL decompresses the game's pak with the inflate part of [zlib](https://github.com/madler/zlib)
1.3.1, included as a git submodule in `overlay/third_party/zlib`, under the zlib License:

```
(C) 1995-2022 Jean-loup Gailly and Mark Adler

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.

Jean-loup Gailly        Mark Adler
jloup@gzip.org          madler@alumni.caltech.edu
```

## RE-UE4SS

The Lua mod runs inside [RE-UE4SS](https://github.com/UE4SS-RE/RE-UE4SS) (MIT licence). UE4SS is not part of
this repository or the release archive; you install it into the game yourself.

## Fonts

The overlay reads Segoe UI and Bahnschrift from the Windows fonts folder at run time. No font files are
included in this repository.

## ASTRONEER

ASTRONEER is a trademark of System Era Softworks. This project is unofficial and is not affiliated with or
endorsed by System Era Softworks. Neither the repository nor the release archive contains game files: the
mod reads item data and icons from your own installed copy of the game into its `data` folder (in the
repository also `tools/extract.mjs` into the git-ignored `data/` and `mod/Byteboard/data/`). Do not publish
those files.
