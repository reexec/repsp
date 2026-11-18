# Third-Party Notices

repsp incorporates or links with third-party open-source software under permissive licenses.

---

## 1. Simple DirectMedia Layer (SDL3)

* Directory: `third_party/SDL`
* Upstream: https://github.com/libsdl-org/SDL
* Version / Pin: `release-3.4.x` (`ee239da42bd043039fddbad0622645f54bf5a640`)
* License: zlib License

```
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
```

---

## 2. psprecomp (GE Interpreter and Software Rasterizer)

* Upstream: https://github.com/sp00nznet/psprecomp
* Commit: `5ca18fa`
* Files adapted: `src/hle/ge.c`, `src/render.c`, `include/psprecomp/render.h`
* License: MIT License

```
Copyright (c) 2026 sp00nznet

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

---

## 3. Note on Reference Projects

Architectural specifications, calling convention models, and hardware behavior references were studied from:
* [psprecomp](https://github.com/sp00nznet/psprecomp) (MIT License)
* [uOFW](https://github.com/uOFW/uOFW) (MIT License / Reverse Engineering documentation)
* [PSP Developer Wiki](https://www.psdevwiki.com/psp/)

All runtime components, memory managers, SFO parser, and lifter-neutral contracts in repsp were implemented independently to adhere to repsp's layer and typing rules.

---

## 4. FFmpeg (Optional Dynamic Codec Plugin `repsp_codec_ffmpeg`)

* Upstream: https://ffmpeg.org/
* License: GNU Lesser General Public License (LGPL) version 2.1 or later (only LGPL shared builds are permitted; GPL builds are explicitly rejected by `scripts/fetch_ffmpeg.py`).
* Integration model: `repsp_codec_ffmpeg` is an **optional** shared-library plugin (`REPSP_CODEC_FFMPEG=OFF` by default) loaded dynamically at runtime via `repsp::codec::LoadCodecPlugin`. Default builds have zero dependency on FFmpeg, and no FFmpeg source or binary is committed to this repository. Only public `libavcodec`/`libavutil` API headers are included by `src/codec_ffmpeg/`.

---

## 5. Dear ImGui

* Directory: `third_party/imgui`
* Upstream: https://github.com/ocornut/imgui
* Version: `v1.93.0` (`f4d9908`)
* License: MIT License

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

---

## 6. Screen Shaders (`src/platform/post_shaders/`, `src/platform/post_shader_*.cpp`, `src/platform/sdl/opengl_post_process.*`)

* **re2DJ** (BSD 3-Clause): the screen shader loader, preset parser, source assembly and GL pass are adapted from re2DJ v0.0.75, and the built-in `crt.glsl` and `scanline.glsl` come from it (changed to read `LogicalSize`). The license text follows this list.
* **AMD FidelityFX Super Resolution 1.0** (MIT, Copyright (c) 2021 Advanced Micro Devices, Inc.): the EASU and RCAS algorithms in `fsr/`, as ported by re2DJ. Notice in each file; [upstream license](https://github.com/GPUOpen-Effects/FidelityFX-FSR/blob/master/license.txt).
* **ScaleFX** by Sp00kyFox (MIT, Copyright (c) 2016 Sp00kyFox): the five passes in `scalefx/`, from libretro glsl-shaders as re2DJ has them. Notice in each file; [upstream](https://github.com/libretro/glsl-shaders/tree/master/edge-smoothing/scalefx).
* **Anime4K** by bloc97 (MIT, Copyright (c) 2019-2021 bloc97): the Upscale CNN x2 (S) weights in `anime4k/`, as converted by re2DJ. Notice in each file; [upstream license](https://github.com/bloc97/Anime4K/blob/master/LICENSE).

```text
BSD 3-Clause License

Copyright (c) 2026, re2DJ contributors

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```
