# RMG-Gekkonet (Rosalie's Mupen GUI - Gekkonet)

Rosalie's Mupen GUI is a free and open-source mupen64plus front-end written in C++.

This fork is focused around netplay with the Gekkonet protocol. Original based on the RMG-K fork

<p align="center"><a href='https://ko-fi.com/J3J21OOC74' target='_blank'><img height='36' style='border:0px;height:36px;' src='https://storage.ko-fi.com/cdn/kofi6.png?v=6' border='0' alt='Buy Me a Coffee at ko-fi.com' /></a></p>

<p align="center">
<img width="962" height="860" alt="image" src="https://github.com/user-attachments/assets/adc7a1b3-0c4f-4d5d-9f88-79622e87f6ee" />
</p>


## The Kaillera Part:
We've ported the old Kaillera client over to QT6 to give the netplay experience more of a modern look and feel, with the same netcode we all know and "love"
### Server mode
Select a Kaillera server from the list, create a room and play with your friends!
<img width="594" height="590" alt="image" src="https://github.com/user-attachments/assets/7bbd49fe-439d-45fa-9b27-0fc2a0e83b72" />
<img width="952" height="686" alt="image" src="https://github.com/user-attachments/assets/42480ec8-11d5-44ba-b820-89bf3df0cdec" />
<img width="959" height="687" alt="image" src="https://github.com/user-attachments/assets/397fd566-285e-4296-b073-91f22906198e" />

### Peer to Peer (P2P)
Want to play someone without involving a server (and without having to port forward?) Select P2P and click host to open up a lobby then have your friend connect to you by entering your connect code! The host can also select the frame delay for the two of you if you think your connection is better than the Kaillera ping calculation.
<img width="594" height="590" alt="image" src="https://github.com/user-attachments/assets/71b234b6-58dc-463b-81fd-7023715382d6" />
<img width="559" height="514" alt="image" src="https://github.com/user-attachments/assets/cd8708f0-4c54-4f2e-aa7e-3b826bc66975" />

</p>

#### Frame Delay (Previously Ping Spoofing)
- This allows the users to set their own frame delay
- Auto, Server will assign frame delay based on ping.
- 1 - 9 frames = sets your frame delay by spoofing your ping toward the middle of the frame window (every 16ms)
- Notifies lobby and game room of frame delay spoofing
  <img width="229" height="49" alt="image" src="https://github.com/user-attachments/assets/37e79e01-f4e9-4118-982e-df138f8d1a07" />

  <img width="283" height="38" alt="image" src="https://github.com/user-attachments/assets/6616186e-ab5d-4598-8b2c-4916d4a54dc4" />

#### In Game Chat
- On-Screen Chat Display: No more needing to alt-tab to see what your opponent is saying in chat. This can be customized or disabled in Settings -> OSD
- Press Enter to Chat: You can now chat while in-game without switching windows! This can be rebound in Hotkeys->System if you'd like to use a different key. Press ESC to cancel
<img width="317" height="105" alt="image" src="https://github.com/user-attachments/assets/11dd1ddc-7a36-45fc-bdbc-39a88f34106c" />

#### Replays system
- Playback overhaul: Reworked the krec playback screen, fixing numerous bugs and making it easier to use. You can now see the players from the replay list!
<img width="745" height="474" alt="image" src="https://github.com/user-attachments/assets/448e8185-5037-4526-8e84-e1a83f39cf84" />




## Input

### Raphnet N64 Adapter support
- We've also added range and button detection for raphnet adapters
<img width="419" height="431" alt="image" src="https://github.com/user-attachments/assets/aa99fdec-3e7f-49ba-b8ca-329a5b09cbce" />

### GCC Adapter Support
- OEM Nintendo Gamecube adapter and Input Integrity Lossless adapter tested and working
 <img width="575" height="730" alt="image" src="https://github.com/user-attachments/assets/726c3416-87d3-4ddc-b4be-91896f253b68" />


### RMG-Input (pronounced Nrage)
RMG-Input was changed so it now uses independent per-axis scaling similar to the Ownasaurus [USBtoN64v2](https://github.com/Ownasaurus/USBtoN64v2) adapter and N-Rage input plugin:
- Should support most xinput devices
- Configurable range slider (0-100%) with default 66% to match N-Rage
- Linear scale: 100% = 127 (protocol max)
- Per-axis deadzone handling instead of circular deadzone
- 
<img width="973" height="856" alt="image" src="https://github.com/user-attachments/assets/6e834d3e-fa92-47b2-9d23-6bb81531357f" />




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
