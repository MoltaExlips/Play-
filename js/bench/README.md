# Play!.js benchmark

Measures how much CPU the browser build spends per emulated frame, using open-source
PS2SDK samples as test programs (no commercial games needed).

## Setup

1. Build the samples with the [ps2dev toolchain](https://github.com/ps2dev/ps2dev/releases):

   ```sh
   export PS2DEV=~/ps2dev PS2SDK=$PS2DEV/ps2sdk
   export PATH=$PATH:$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin:$PS2SDK/bin
   for s in cube teapot vu1; do make -C $PS2SDK/samples/draw/$s; done
   ```

2. Serve a page that loads `Play.js` from a cross-origin-isolated origin and exposes:
   a `#status` element containing "ready" once `initVm` has run, a file input `#file` that boots
   the chosen ELF, an `#fps` element showing the `getFrames()` count each second, and the
   `#outputCanvas` canvas.

3. `npm install playwright && npx playwright install chromium`

## Run

```sh
node bench.mjs <page-origin> <path/to/sample.elf> <seconds> [screenshot.png]
THREADS=1 node bench.mjs ...   # also print per-thread CPU inside the renderer process
```

Prints the average frame rate and CPU milliseconds per frame for the renderer process
(the emulator: main thread plus workers) and the GPU process. In headless Chrome without
a GPU the GPU-process number is SwiftShader software rendering and isn't meaningful.

## Results (headless Chrome, 8-core x86-64, 15 s after an 8 s warm-up, two runs each)

Renderer CPU per frame, lower is better. All builds run the samples at ~59 fps.

| Sample | Upstream | + SIMD / wasm exceptions / LTO | + GS calls on main thread | + direct JIT helper imports |
|--------|---------:|-------------------------------:|--------------------------:|----------------------------:|
| cube   | 3.63, 3.68 ms | 3.39, 3.62 ms | 3.45, 3.38 ms | 3.31, 3.34 ms |
| teapot | 2.96, 2.98 ms | 2.86, 2.87 ms | 2.91, 2.92 ms | 2.79, 2.80 ms |
| vu1    | 6.50, 6.31 ms | 6.41, 6.37 ms | 5.64, 5.69 ms | 5.70, 5.69 ms |

Each column includes the changes to its left. The last two columns were measured in the
same session (main-thread GS alone: cube 3.38, 3.48 / teapot 2.90, 2.89 / vu1 5.75, 5.74).

The teapot sample shows a black screen in every build, including upstream.
