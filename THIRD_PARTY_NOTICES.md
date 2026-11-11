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

## 2. Note on Reference Projects

Architectural specifications, calling convention models, and hardware behavior references were studied from:
* [psprecomp](https://github.com/sp00nznet/psprecomp) (MIT License)
* [uOFW](https://github.com/uOFW/uOFW) (MIT License / Reverse Engineering documentation)
* [PSP Developer Wiki](https://www.psdevwiki.com/psp/)

All runtime components, memory managers, SFO parser, and lifter-neutral contracts in repsp were implemented independently to adhere to repsp's layer and typing rules.
