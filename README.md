# hwgl: run Homeworld Remastered (Windows) on macOS with CrossOver or Wine

A drop-in `opengl32.dll` that makes the Windows version of **Homeworld Remastered Collection** run under CrossOver / Wine on macOS with hardware-accelerated OpenGL.

Tested with GOG Homeworld Remastered 2.1 (build 31) on macOS 27, Apple M3 Max, under:
- **CrossOver 26.2**
- **Apple Game Porting Toolkit Wine 7.7**, via Whisky 2.3.4
- **Because it works with GPTK**, probably also works with Heroic

Compatibility with the Steam version is not guaranteed. Try it out.

## Install
1. Copy `opengl32.dll` into `HomeworldRM\Bin\Release\` next to `HomeworldRM.exe`.
2. Copy the `.bat` files from `launchers/` into the game's root folder (next to `HWRStart.exe`).
3. Start the game with one of the `.bat` files. In CrossOver, use *Run Command* on a `.bat` file
   and tick *Save command as a launcher* so you have a one-click shortcut.

Each `.bat` file also tells Wine to load the shim, by writing a per-game DLL override (`opengl32` = native, builtin, for `HomeworldRM.exe` only) to the bottle's registry. If you launch the game some other way, such as by using the launcher, import `homeworldrm-override.reg` once instead.
To uninstall, delete `opengl32.dll` from the game folder. The registry override can stay; without the DLL it does nothing.

### Whisky / Game Porting Toolkit
Same files, same steps. If you would rather not use the `.bat` files, import `homeworldrm-override.reg` once into the bottle. Then pin `HomeworldRM.exe` and put one of these into its arguments in Whisky's program settings:

| Mode | Arguments |
|---|---|
| Homeworld 1 Remastered | `-dlccampaign HW1Campaign.big -campaign HomeworldClassic -moviepath DataHW1Campaign` |
| Homeworld 2 Remastered | `-dlccampaign HW2Campaign.big -campaign Ascension -moviepath DataHW2Campaign` |
| Multiplayer | none |

These are the same arguments the `.bat` files and the official launcher use.

## Diagnosis
macOS offers only two kinds of OpenGL context: legacy **2.1**, or **forward-compatible core 3.2-4.1**.
Homeworld Remastered assumes what Windows drivers provide:
1. It requests a **3.3 compatibility-profile** context. macOS has no such thing, so Wine's Mac driver refuses, and the game hangs or crashes.
2. It looks up its OpenGL functions while a temporary **legacy bootstrap context** is current, before it creates the 3.3 one. On Windows that bootstrap context is a full 4.x compatibility context. On macOS it is GL 2.1, so every 3.x function comes back NULL and the game crashes once it calls one.

Both show up in Wine's own OpenGL log (`--debugmsg +wgl`): the rejected context attributes, and the function lookups that happen between the two context creations.

## What the shim does
It forwards all 361 `opengl32` exports to Wine's own `opengl32.dll` but changes three things:

| Function | Change |
|---|---|
| `wglCreateContextAttribsARB` (via `wglGetProcAddress`) | Any 3.x request becomes forward-compatible core 3.2+ (fixes 1). |
| `wglCreateContext` | Returns a forward-compatible core context in place of the legacy one, so the game's function lookups and capability checks see the same 4.1 core context it will render with (fixes 2). |
| `glGetString(GL_EXTENSIONS)` | Core contexts have no extension string; when the driver returns NULL, the shim builds one from `glGetStringi`. |

When it runs with `--debugmsg +debugstr`, the shim logs its actions as `hwgl: ...` lines.

## Known issues

- Install the game to a short path such as the default `C:\GOG Games\Homeworld Remastered`. With a long install path (reproduced at ~150 characters, well below Windows' 260-character limit), the game overflows one of its own buffers while loading the campaigns and crashes in `msvcr110.dll`. This happens with and without the shim, under both CrossOver and GPTK. It is untested on Windows.
- With display scaling, the window may start at the wrong size (the intro video fills a quarter of the screen). Click the window once and it resizes correctly. This also happens without the shim.
- With more than one monitor connected, the game has display problems. This has not been investigated yet; disconnecting the extra monitor works around it.

## Build
Requires LLVM (`clang`, `llvm-dlltool`), `lld` and Python 3. No Windows SDK or MinGW is needed.

```sh
brew install llvm lld   # Python 3 ships with the Xcode command line tools
./build.sh              # -> build/opengl32.dll and the installable dist/hwgl-homeworldrm.zip
```

All of these files are needed to build:

- `hwgl.c`: the shim logic, written as freestanding C (no CRT).
- `gen_stubs.py`: run by `build.sh`. It reads `opengl32.exports` and generates `stubs.S` (a
  `jmp [real]` forwarding stub per export) and `opengl32.def` (the DLL's export list).
- `opengl32.exports`: the names of the 361 functions exported by Wine's 32-bit `opengl32.dll`,
  i.e. the standard Windows OpenGL 1.1 + WGL API. It was read from the export table of the
  `opengl32.dll` that ships with CrossOver (`lib/wine/i386-windows/opengl32.dll`); Wine's
  `dlls/opengl32/opengl32.spec` lists the same functions.
- `build.sh`: compiles and links everything, then packages the DLL, launchers, `.reg` file,
  README and LICENSE into `dist/hwgl-homeworldrm.zip`.

## License
MIT, see `LICENSE`.

This was built with the help of AI.
