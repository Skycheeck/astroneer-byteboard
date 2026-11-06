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

## RE-UE4SS

The Lua mod runs inside [RE-UE4SS](https://github.com/UE4SS-RE/RE-UE4SS) (MIT licence). UE4SS is not part of
this repository; you install it into the game yourself.

## Fonts

The overlay reads Segoe UI and Bahnschrift from the Windows fonts folder at run time. No font files are
included in this repository.

## ASTRONEER

ASTRONEER is a trademark of System Era Softworks. This project is unofficial and is not affiliated with or
endorsed by System Era Softworks. The repository contains no game files: `tools/extract.mjs` reads item data
and icons from your own installed copy of the game into git-ignored files (`data/`, `overlay/catalog.tsv`,
`overlay/taxonomy.tsv`, `overlay/icons.rgba`). Do not publish those files.
