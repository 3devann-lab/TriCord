# TriCord
![License](https://img.shields.io/badge/license-GPLv3-green)
![Platform](https://img.shields.io/badge/platform-Nintendo%203DS-red)
![Downloads](https://img.shields.io/github/downloads/2b-zipper/TriCord/total?style=flat&color=blue)
[![Discord](https://img.shields.io/badge/Discord-%235865F2.svg?style=flat&logo=discord&logoColor=white)](https://discord.gg/quYy9fK8tJ)

Discord client for Nintendo 3DS.

## Disclaimer

> **This project is developed for educational purposes only.**
> This is an unofficial Discord client and is not affiliated with or endorsed by Discord Inc.

This software is provided **"as is"**, and you use it at your own risk. The use of this application is entirely the user's own responsibility. The developers assume no responsibility for:
* Any damages, data loss, or account-related issues.
* Violations of Discord's **Terms of Service (ToS)** resulting from the use of this software.

## Screenshots
![login](screenshots/1.png)
![serverlist](screenshots/2.png)
![hamburger](screenshots/3.png)
![chat](screenshots/4.png)

## Features
- QR code and email/password authentication
- Text messaging with emoji, attachments, embeds, reactions, and replies
- **Send photos, videos and files straight from the SD card**, with a preview, caption and spoiler toggle
- **Send your favorite GIFs** (the ones you starred in Discord), picked from animated previews
- **Watch videos** (MP4/MOV/WebM) from attachments, GIF embeds, links, or the SD card, with sound and seeking. H.264 uses the New 3DS hardware decoder when it can
- **Take a photo** with the inner or outer camera and send it
- Full-screen image viewer with zoom and pan
- Save attachments to the SD card (`sdmc:/3ds/TriCord/downloads`)
- Multi-account support
- Server, channel, and forum thread navigation
- Customizable [themes](THEME_FORMAT.md) and internationalization (EN/JA)

## Custom Themes
TriCord supports custom themes. You can create your own theme by following the **[Theme Format Specification](THEME_FORMAT.md)**.
Check out the [TriCord-Themes](https://github.com/2b-zipper/TriCord-Themes) repository for sample themes.

## Installation
1. Download the latest release (CIA or 3DSX). The newest build from GitHub Actions is always at the root of this repository as [`TriCord.cia`](TriCord.cia)
2. Install via FBI (CIA) or place in `/3ds/` (3DSX)
3. Launch and log in with QR code or email/password

### Login Methods

**QR Code** — Scan using the Discord mobile app.

**Email / Password** — Enter your credentials directly on the bottom screen.

**token.txt** — If you cannot use the above methods, you can log in using your Discord token:
1. Create a file named `token.txt` at `sdmc:/3ds/TriCord/token.txt`
2. Paste your Discord token inside (plain text, no extra whitespace)
3. Launch TriCord — it will automatically read the token, log in, and delete the file

> ⚠️ Never share your Discord token with anyone. Treat it like a password.

### ⚠️ Security Notice
The account information is stored in `/3ds/TriCord/accounts`. **Although this file is encrypted, never share it with others.** Sharing this file may allow unauthorized access to your Discord account.

## Building from Source

### Prerequisites
- [devkitPro](https://devkitpro.org/wiki/Getting_Started) with devkitARM and `3ds-libjpeg-turbo`
- `makerom` and `bannertool` on your `PATH` for the CIA (`scripts/install_cia_tools.sh` installs both)

### Build

```bash
git clone https://github.com/2b-zipper/TriCord.git
cd TriCord
scripts/build_ffmpeg.sh   # optional: video playback; downloads and cross-compiles a trimmed FFmpeg
make -j$(nproc)
```

Output: `TriCord.3dsx`, `TriCord.cia`, `TriCord.elf`. Use `make 3dsx` to skip the CIA when you don't have `makerom` and `bannertool`.

Without `scripts/build_ffmpeg.sh`, TriCord still builds; the video player then says playback isn't included.

### GitHub Actions
Every push builds the CIA and 3DSX in the `devkitpro/devkitarm` container (`.github/workflows/build.yml`), uploads both as a build artifact, and commits `TriCord.cia` to the root of the branch that was pushed.

## Libraries

### devkitPro Libraries (via devkitPro pacman)
- [libctru](https://github.com/devkitPro/libctru) (zlib License)
- [citro3d](https://github.com/devkitPro/citro3d) (zlib License)
- [citro2d](https://github.com/devkitPro/citro2d) (zlib License)
- [libcurl](https://curl.se/libcurl/) (curl License)
- [mbedtls](https://github.com/Mbed-TLS/mbedtls) (Apache-2.0 OR GPL-2.0-or-later)
- [zlib](https://zlib.net/) (zlib License)

### Bundled Libraries (included in `library/`)
- [RapidJSON](https://github.com/Tencent/rapidjson) (MIT License)
- [stb_image](https://github.com/nothings/stb) (Public Domain / MIT License)
- [qrcodegen](https://github.com/nayuki/QR-Code-generator) (MIT License)
- [mlspp](https://github.com/cisco/mlspp) (BSD 2-Clause License)
- [libdave](https://github.com/discord/libdave) (MIT License)
- [WebRTC AECM](https://github.com/webrtc-mirror/webrtc) (BSD 3-Clause License)
- [libopus](https://opus-codec.org/) (BSD 3-Clause License)
- [ThorVG](https://github.com/thorvg/thorvg) (MIT License)

### Built at Compile Time
- [FFmpeg](https://ffmpeg.org/) (LGPL-2.1-or-later), by `scripts/build_ffmpeg.sh`
- [libjpeg-turbo](https://libjpeg-turbo.org/) (BSD-style / IJG License), via devkitPro pacman

### Additional Resources
- CA bundle: [cacert-2025-12-02.pem](https://curl.se/docs/caextract.html) (Mozilla's CA certificate bundle)
- [Twemoji](https://github.com/jdecked/twemoji) (CC-BY 4.0 / MIT License)

## Usage

**DM/Server/Channel List**
- D-Pad: Navigate | A: Select | B: Back

**Message View**
- D-Pad: Scroll | B: Back | X: Open Menu | Y: Text input
- R (or the + button): Attach a photo/video from the SD card, a favorite GIF, or a camera photo
- A on a video: Play it | X menu: View Image, Play Video, Save to SD Card

**Video Player**
- A: Play/Pause | Left/Right: ±5s | L/R: ±30s | Up/Down: Volume | Touch the bar: Seek
- X: Switch hardware/software decoding (New 3DS, H.264) | Y: Save to SD card | B: Close

**File Browser**
- A: Open folder / choose file | B: Up a folder | Y: Show all files / media only | X: Close

**General**
- Start: Exit the app | Select: Open the hamburger menu | L+R: Toggle Debug Log | R+L: Toggle Stats Overlay

Config: `sdmc:/3ds/TriCord/config.json`

## FAQ

### Is there a possibility of an account ban/suspension?
Because WebSockets are properly implemented, the risk is relatively lower compared to previous Discord clients for the 3DS. However, since it is an unofficial client, I cannot guarantee that the risk is zero. You should assume that there is always a risk of getting banned. As stated in the disclaimer, I take no responsibility for any account bans or suspensions.

### The app crashes while loading guilds
This is caused by your account being in too many servers. Please use a different account with fewer guilds. I plan to improve this as much as possible in the future.

### The app is laggy / crashes frequently
This is due to the 3DS hardware limitations, so there is nothing I can do about it. Especially on the Old 3DS, the RAM is half that of the New 3DS and the CPU is much weaker, making it prone to running slowly. I recommend using a New 3DS for the most comfortable experience. While I might add features exclusive to the New 3DS in the future, rest assured that I will never drop support for the Old 3DS.

### I have a question
Feel free to ask in our [Discord server](https://discord.gg/quYy9fK8tJ).

### I found a bug
Please report it by opening a [GitHub Issue](https://github.com/2b-zipper/TriCord/issues) or by visiting the bug-reports channel in our Discord server.

### I want to add or improve a translation
Please help us translate TriCord into your language on our [Crowdin project](https://crowdin.com/project/tricord). If your language is not listed, you can request it directly by clicking the "Request New Language" button on the Crowdin page.

## License
This project is licensed under the GNU General Public License v3.0 - see the [LICENSE](LICENSE) file for details.

The license texts of TriCord and everything it ships with are in [romfs/licenses](romfs/licenses), which is embedded in the `.3dsx` and `.cia`.

## Credits
- [2b-zipper](https://github.com/2b-zipper) for the main development
- [Str4ky](https://github.com/Str4ky) for the French translation
- [AverageJohtonian](https://github.com/AverageJohtonian) for the Spanish translation
- [RossoDev](https://github.com/RossoDev) for the Italian translation
- [MorrisTheGamer](https://github.com/MorrisTheGamer) for the German translation
- [ReisuErx](https://github.com/ReisuErx) for the Polish translation
- [wiretoscreen](https://github.com/wiretoscreen) for the Brazilian Portuguese translation
- [murloc-tinyfin](https://github.com/murloc-tinyfin) for the Simplified Chinese translation
- [Discord Userdoccers](https://github.com/discord-userdoccers/discord-userdoccers) for the documentation of the Discord API
- And all other contributors!
