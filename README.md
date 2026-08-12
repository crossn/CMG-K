# CMG-K

CMG-Kは、RMG-Kをベースに日本語UIとネット対戦機能を整備した、N64エミュレータのcommunity forkです。RMG-K本家そのものでも公式版でもありません。現在の実装はRMG-K v0.9.11をベースにしています。

## ベータ版について

CMG-Kは現在、最初の一般試験配布に向けたベータ段階です。不具合が残っている可能性がありますので、フィードバックを歓迎いたします。大切な設定や録画データは、必要に応じてバックアップしてください。

## ダウンロード

ダウンロードは [GitHub Releases](https://github.com/crossn/CMG-K/releases) から行ってください。

ベータ版では、まず **Windows Portable ZIP** をおすすめします。

- インストール不要です
- 既存環境と分けて試しやすい構成です

**Windows Setup** 版も提供します。Linux AppImageは現在のworkflowではCI artifactとして生成されますが、Windows版と同じ一般配布物としてReleaseへ掲載する方針ではありません。

バージョンは `v0.1.0-beta.YYYYMMDD` の形式です。同日に再リリースする場合は末尾に番号が付くことがあります。

## はじめ方

1. ReleasesからWindows Portable ZIPを取得します。
2. 任意のフォルダへ展開します。
3. `CMG-K.exe`を起動します。
4. ROMブラウザーから、手元のROMを登録します。

ROMは同梱されません。ROMの入手方法については案内していません。

## 主な特徴

- 日本語UI（Qt翻訳）
- Kaillera Server Mode
- CP932を扱う日本語チャット対応
- P2Pネット対戦
- Rollback Lobby（GekkoNetベース）
- In-Game Chat
- Kailleraセッションの録画・再生（`.krec`）
- Raphnet N64 Adapter
- GCC Adapter（RMG-Input-GCA）
- RMG-Input
- Windows Portable / Setup向けUpdater

### スクリーンショット

既存のスクリーンショットは、各機能の動作例として残しています。画像内に旧RMG-Kの表示が含まれる場合がありますが、画像自体は新規作成せず、今後CMG-K向けに差し替え可能な構成としています。

<p align="center">
<img width="962" height="860" alt="CMG-K user interface" src="https://github.com/user-attachments/assets/adc7a1b3-0c4f-4d5d-9f88-79622e87f6ee" />
</p>

### P2P

- Connect codeによるNAT traversal
- 保存されるconnect code
- Autoまたは1～9フレームのFrame delay
- Live ping表示
- 公開waiting-games browser
- Ready sync

<img width="676" height="613" alt="P2P settings" src="https://github.com/user-attachments/assets/6cf1cfcb-891f-457e-af8d-e348c99c2ffd" />
<img width="822" height="573" alt="P2P netplay" src="https://github.com/user-attachments/assets/13915a82-54b5-44ce-bb1a-f0aac5304220" />

### Kaillera Server Mode

- サーバーブラウザー、ping、地域情報
- お気に入りサーバーとカスタムサーバー
- IPコピー、Ping、Traceroute
- Autoまたは手動のFrame delay
- ゲーム間の接続維持

<p align="center">
<img width="825" height="593" alt="Kaillera server browser" src="https://github.com/user-attachments/assets/d5145a6c-4bc2-4f52-901b-7da7f0f0ae8d" />
<img width="961" height="620" alt="Kaillera netplay" src="https://github.com/user-attachments/assets/549ba26f-03d7-4950-a3cb-7a68cc9e6c98" />
<img width="283" height="38" alt="Kaillera status" src="https://github.com/user-attachments/assets/6616186e-ab5d-4598-8b2c-4916d4a54dc4" />
</p>

### チャットとリプレイ

ゲーム中のチャットをOSDに表示でき、Enterキーでチャットを入力できます。Kailleraセッションは`.krec`へ録画でき、停止・再開・フレーム送りに対応した再生機能があります。

<img width="702" height="482" alt="Replay browser" src="https://github.com/user-attachments/assets/99a90662-7cae-400f-b5bf-0831750b8375" />

### 入力デバイス

Raphnet N64 Adapter、GCC Adapter、RMG-Inputをサポートします。RMG-Inputでは、xinputデバイス、軸ごとの範囲・デッドゾーン設定を利用できます。

<img width="462" height="604" alt="Raphnet N64 Adapter" src="https://github.com/user-attachments/assets/c8579f1e-3e0e-4f2d-a1cc-a9e6bf2bdcd9" />
<img width="582" height="754" alt="GCC Adapter" src="https://github.com/user-attachments/assets/54ca80cb-10d7-402a-8c0e-15a0378d5a5f" />
<img width="919" height="793" alt="RMG-Input" src="https://github.com/user-attachments/assets/6192636d-4bcb-4104-b08a-696dcb416e7d" />

## English

### What is CMG-K?

CMG-K is a community fork of RMG-K, an N64 emulator/front-end based on mupen64plus. It adds and maintains Japanese UI and netplay-related features for its target community. CMG-K is not the official RMG-K build. **Based on RMG-K v0.9.11.**

### Beta notice

CMG-K is currently in the early public testing stage. Bugs may remain, and feedback is welcome. Back up important settings and recordings when appropriate.

### Downloads

Download releases from [GitHub Releases](https://github.com/crossn/CMG-K/releases). For beta testing, **Windows Portable ZIP** is recommended because it requires no installation and is easy to keep separate from an existing setup. A **Windows Setup** build is also provided.

Linux AppImages are currently produced as CI artifacts; the current workflow does not publish them as general GitHub Release assets alongside the Windows packages.

Beta versions use the format `v0.1.0-beta.YYYYMMDD`; a same-day re-release may add a numeric suffix.

### Getting started

1. Download the Windows Portable ZIP from Releases.
2. Extract it to a folder of your choice.
3. Start `CMG-K.exe`.
4. Add your own ROMs through the ROM browser.

ROMs are not included. This project does not provide instructions for obtaining ROMs.

### Main features

- Japanese Qt UI
- Kaillera Server Mode and CP932-aware Japanese chat
- Direct P2P netplay
- Rollback Lobby using the GekkoNet rollback path
- In-game chat
- Kaillera recording and playback with `.krec`
- Raphnet N64 Adapter, GCC Adapter, and RMG-Input support
- Updater support for Windows Portable and Setup builds

The existing screenshots above are retained as feature examples. Some may show the former RMG-K presentation and can be replaced with CMG-K screenshots later.

## Building

### Linux

*Portable Debian/Ubuntu:*

```bash
sudo apt-get -y install cmake libusb-1.0-0-dev libhidapi-dev libsamplerate0-dev libspeex-dev libminizip-dev libsdl3-dev libfreetype6-dev libgl1-mesa-dev libglu1-mesa-dev pkg-config zlib1g-dev binutils-dev libspeexdsp-dev qt6-base-dev qt6-websockets-dev libqt6svg6-dev libvulkan-dev build-essential nasm git zip ninja-build
./Source/Script/Build.sh Release
```

*Portable Fedora:*

```bash
sudo dnf install libusb1-devel hidapi-devel libsamplerate-devel minizip-compat-devel SDL3-devel freetype-devel mesa-libGL-devel mesa-libGLU-devel pkgconfig zlib-ng-devel binutils-devel speexdsp-devel qt6-qtbase-devel qt6-qtsvg-devel qt6-qtwebsockets-devel vulkan-devel gcc-c++ nasm git ninja-build
./Source/Script/Build.sh Release
```

*Portable Arch Linux:*

```bash
sudo pacman -S --needed make cmake gcc libusb hidapi freetype2 libpng qt6 sdl3 libsamplerate nasm minizip pkgconf vulkan-headers git
./Source/Script/Build.sh Release
```

*Portable OpenSUSE Tumbleweed:*

```bash
sudo zypper install SDL3-devel cmake freetype2-devel gcc gcc-c++ libusb-1_0-devel libhidapi-devel libhidapi-hidraw0 libpng16-devel libsamplerate-devel make nasm ninja pkgconf-pkg-config speex-devel vulkan-devel zlib-devel qt6-tools-devel qt6-opengl-devel qt6-widgets-devel qt6-svg-devel minizip-devel git
./Source/Script/Build.sh Release
```

Built executables are placed in `Bin/Release`.

For a non-portable installation:

```bash
export src_dir="$(pwd)"
export build_dir="$(pwd)/build"
mkdir -p "$build_dir"
cmake -S "$src_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE="Release" -DPORTABLE_INSTALL="OFF" -DCMAKE_INSTALL_PREFIX="/usr" -G "Ninja"
cmake --build "$build_dir"
cmake --install "$build_dir" --prefix="/usr"
```

### Windows

Install [MSYS2](https://www.msys2.org/) with the UCRT64 environment, then install the required packages:

```bash
pacman -S --needed make mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-libusb mingw-w64-ucrt-x86_64-hidapi mingw-w64-ucrt-x86_64-freetype mingw-w64-ucrt-x86_64-libpng mingw-w64-ucrt-x86_64-qt6 mingw-w64-ucrt-x86_64-sdl3 mingw-w64-ucrt-x86_64-speexdsp mingw-w64-ucrt-x86_64-libsamplerate mingw-w64-ucrt-x86_64-nasm mingw-w64-ucrt-x86_64-minizip mingw-w64-ucrt-x86_64-vulkan-headers git
./Source/Script/Build.sh Release
```

Built executables are placed in `Bin/Release`.

## Credits and upstream

CMG-K is built on the work of the RMG-K / Rosalie's Mupen GUI project and the mupen64plus ecosystem. It also includes the relevant n02 Kaillera client and input projects used by this repository. Please see the source tree and upstream projects for their contributors and licenses. CMG-K is an independent community fork and is not an official RMG-K release.

## License

CMG-K and the applicable Rosalie's Mupen GUI code are licensed under the [GNU General Public License v3.0](https://www.gnu.org/licenses/gpl-3.0.en.html). Third-party components may have their own licenses; see their source and license files.
