# Handmade Voxel Game

This repository contains the code for a C++ 3D voxel game. The project is inspired by Casey Muratori's [Handmade Hero](https://hero.handmade.network/) series, whose goal is to write a complete video game _from scratch_ without relying on any third party dependencies or the C/C++ standard library.

The Windows platform layer uses:
- Win32 API
- GameInput
- D3D12

All of which should be available on a stock Windows 10+ install.

## Why ?

Because it's fun ! Writing a complete codebase is also a great learning exercise.

## How to build:

Running the `build.py` file should just work, provided you have `clang.exe` on your PATH. You can then just run `build/win32_game.exe`.

## Short-term TO-DO list:

### Engine:
- [x] Game code hot-reloading
- [x] Keyboard & controller input processing (using the Windows 10+ GameInput lib)
- [x] Generating `compile_commands.json` inside the build script
- [ ] Swapchain resizing
- [ ] Sound output (WASAPI ?)
- [ ] Save states (like an emulator)
- [ ] Looped input recording & playback
- [ ] "Topmost" window mode (useful to keep the window on top of the editor while hot-reloading)
- [x] Basic matrix math library
- [ ] High level renderer API
- [ ] Logging
- [ ] UI & (bitmap) text rendering
- [ ] Thread pool ?? For async asset streaming

### Game:
- [ ] Chunk data streaming system
- [ ] Procedular heightmap with perlin noise
- [ ] Greedy meshing
- [ ] Asynchronous GPU transfer for the meshes
