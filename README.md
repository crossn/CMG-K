# RMG-K (Rosalie's Mupen GUI - Kaillera)

Rosalie's Mupen GUI is a free and open-source mupen64plus front-end written in C++.

This fork is focused around netplay with the Kaillera protocol.

<p align="center"><a href='https://ko-fi.com/J3J21OOC74' target='_blank'><img height='36' style='border:0px;height:36px;' src='https://storage.ko-fi.com/cdn/kofi6.png?v=6' border='0' alt='Buy Me a Coffee at ko-fi.com' /></a></p>

<p align="center">
<img width="717" height="706" alt="image" src="https://github.com/user-attachments/assets/3cc981da-b9c1-4ed3-b9aa-ca081aa801a4" />
</p>


## The Kaillera Part:
### N02 Client
<p align="center">
<img width="494" height="329" alt="image" src="https://github.com/user-attachments/assets/47adbed6-640b-4238-9927-c2114aa48c7d" />
</p>

#### Frame Delay (Previously Ping Spoofing)
- This allows the users to set their own frame delay
- Auto, Server will assign frame delay based on ping.
- 1 - 9 frames = sets your frame delay by spoofing your ping toward the middle of the frame window (every 16ms)
- Notifies lobby and game room of frame delay spoofing

#### Drop Actually Works!
- Clicking Drop will stop emulation (if player 1 clicks drop, it will drop for all others in the room)
- After all players drop, the room owner is able to restart emulation for everyone

#### In Game Chat
- On-Screen Chat Display: No more needing to alt-tab to see what your opponent is saying in chat. This can be customized or disabled in Settings -> OSD
- Press Enter to Chat: You can now chat while in-game without switching windows! This can be rebound in Hotkeys->System if you'd like to use a different key. Press ESC to cancel

#### Replays system
- Playback overhaul: Reworked the krec playback screen, fixing numerous bugs and making it easier to use. You can now see the players from the replay list!

  <img width="604" height="198" alt="image" src="https://github.com/user-attachments/assets/c2b52fb1-6bb4-43a2-b36e-90145a166116" />

#### Static Netplay Settings
FOR NOW, the following settings are hard coded when a user starts a netplay session:

| Setting | Local Play | Kaillera Netplay |
|---------|------------|------------------|
| RandomizeInterrupt | true (random) | false (deterministic) |
| CPU_Emulator | User's choice (default 2) | 2 (dynarec) |
| CountPerOp | ROM database / overlay | 0 (use ROM database) |
| CountPerOpDenomPot | 0 | 0 |
| SiDmaDuration | -1 | -1 |
| DisableExtraMem | User's choice | false (8MB enabled) |
| DisableSaveFileLoading | false (loads saves) | true (fresh saves) |
| RSP_Plugin | User's choice | Hacktarux/Azimer HLE RSP Plugin |

## Input

### Raphnet N64 Adapter support
- Yes, your N64 controller works :D
### GCC Adapter Support
- OEM Nintendo Gamecube adapter and Input Integrity Lossless adapter tested and working
  <img width="582" height="650" alt="image" src="https://github.com/user-attachments/assets/193d6069-c917-43fc-a557-9b897da273fd" />

### RMG-Input (pronounced Nrage)
RMG-Input was changed so it now uses independent per-axis scaling similar to the Ownasaurus [USBtoN64v2](https://github.com/Ownasaurus/USBtoN64v2) adapter and N-Rage input plugin:
- Should support most xinput devices
- Configurable range slider (0-100%) with default 66% to match N-Rage
- Linear scale: 100% = 127 (protocol max)
- Per-axis deadzone handling instead of circular deadzone
  <img width="1059" height="736" align="center" alt="Screenshot 2026-01-16 181813" src="https://github.com/user-attachments/assets/eacacb9b-f828-4486-a0f0-a8b539c8951f" />




## Building

#### Linux
* Portable Debian/Ubuntu

  ```bash
  sudo apt-get -y install cmake libusb-1.0-0-dev libhidapi-dev libsamplerate0-dev libspeex-dev libminizip-dev libsdl3-dev libfreetype6-dev libgl1-mesa-dev libglu1-mesa-dev pkg-config zlib1g-dev binutils-dev libspeexdsp-dev qt6-base-dev qt6-websockets-dev libqt6svg6-dev libvulkan-dev build-essential nasm git zip ninja-build
  ./Source/Script/Build.sh Release
  ```
  
* Portable Fedora
  ```bash
  sudo dnf install libusb1-devel hidapi-devel libsamplerate-devel minizip-compat-devel SDL3-devel freetype-devel mesa-libGL-devel mesa-libGLU-devel pkgconfig zlib-ng-devel binutils-devel speexdsp-devel qt6-qtbase-devel qt6-qtsvg-devel qt6-qtwebsockets-devel vulkan-devel gcc-c++ nasm git ninja-build
  ./Source/Script/Build.sh Release
  ```

* Portable Arch Linux
  ```bash
  sudo pacman -S --needed make cmake gcc libusb hidapi freetype2 libpng qt6 sdl3 libsamplerate nasm minizip pkgconf vulkan-headers git
  ./Source/Script/Build.sh Release
  ```

* Portable OpenSUSE Tumbleweed
  ```bash
  sudo zypper install SDL3-devel cmake freetype2-devel gcc gcc-c++ libusb-1_0-devel libhidapi-devel libhidapi-hidraw0 libpng16-devel libsamplerate-devel make nasm ninja pkgconf-pkg-config speex-devel vulkan-devel zlib-devel qt6-tools-devel qt6-opengl-devel qt6-widgets-devel qt6-svg-devel minizip-devel git
  ./Source/Script/Build.sh Release
  ```

When it's done building, executables can be found in `Bin/Release`

* Installation/Packaging
```bash
export src_dir="$(pwd)"
export build_dir="$(pwd)/build"
mkdir -p "$build_dir"
cmake -S "$src_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE="Release" -DPORTABLE_INSTALL="OFF" -DCMAKE_INSTALL_PREFIX="/usr" -G "Ninja"
cmake --build "$build_dir"
cmake --install "$build_dir" --prefix="/usr"
```

#### Windows
* Download & Install [MSYS2](https://www.msys2.org/) (UCRT64)
```bash
pacman -S --needed make mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-libusb mingw-w64-ucrt-x86_64-hidapi mingw-w64-ucrt-x86_64-freetype mingw-w64-ucrt-x86_64-libpng mingw-w64-ucrt-x86_64-qt6 mingw-w64-ucrt-x86_64-sdl3 mingw-w64-ucrt-x86_64-speexdsp mingw-w64-ucrt-x86_64-libsamplerate mingw-w64-ucrt-x86_64-libusb mingw-w64-ucrt-x86_64-nasm mingw-w64-ucrt-x86_64-minizip mingw-w64-ucrt-x86_64-vulkan-headers git
./Source/Script/Build.sh Release
```

When it's done building, executables can be found in `Bin/Release`


## License

Rosalie's Mupen GUI is licensed under the [GNU General Public License v3.0](https://www.gnu.org/licenses/gpl-3.0.en.html).
