# pkgj

[![Downloads][img_downloads]][pkgj_downloads] [![Release][img_latest]][pkgj_latest] [![License][img_license]][pkgj_license]

This homebrew allows to download & unpack pkg file directly on Vita together with your [NoNpDrm][] or [NoPsmDrm][] fake license.
for PSP can be played using [Adrenaline][] or directly from livearea using [NoPspEmuDrm][] 
# Features

* **works on** all PS Vita models, including PSTV.
* **easy** way to see list of available downloads, including searching, filter & sorting.
* **standalone**, no PC required, everything happens directly on Vita.
* **automatic** download and unpack, just choose an item, and it will be installed, including bubble in live area.
* **background downloads**, now supports native bgdl function, so you can do whatever you want on the console while content is downloading.
* **queues** multiple downloads.
* **supports** the TSV file format.
* **installs** Game Updates, DLCs, Demos, Themes, PSM, PSP games, PSP DLCs, and PSX games.

# Download

Get latest version as [vpk file here][pkgj_latest].

# Usage

Make sure unsafe mode is enabled in Henkaku settings.

Using application is pretty straight forward. Select item you want to install and press X and follow the instructions. To sort/filter/search press triangle.
It will open context menu. Press triangle again to confirm choice(s) you make in menu. Or press O to cancel any changes you did.

Press left or right button to move page up or down.

# Configuration

pkgj is shipped with valid default URLs. If you wish to change some settings, they can be configured through `ux0:pkgj/config.txt` or `ur0:pkgj/config.txt`.

| Option | Description |
| --- | --- |
| `url_games <URL>` | The URL of the PS Vita game list |
| `url_psv_demos <URL>` | The URL of the PS Vita demo list |
| `url_dlcs <URL>` | The URL of the PS Vita DLC list |
| `url_psv_themes <URL>` | The URL of the PS Vita Theme list |
| `url_psm_games <URL>` | The URL of the PS Mobile list (see Q&A) |
| `url_psp_games <URL>` | The URL of the PSP game list |
| `url_psp_dlcs <URL>` | The URL of the PSP DLC list |
| `url_psx_games <URL>` | The URL of the PSX game list |
| `url_comppack <URL>` | The URL of the PS Vita compatibility pack list |
| `install_psp_as_pbp 1` | Install PSP games as EBOOT.EBP files instead of ISO files (see Q&A) |
| `install_psp_psx_location uma0:` | Install PSP and PSX games on `uma0:` |
| `no_version_check 1` | Do not check for update when starting PKGj |

# Q&A

1. Where to remove interrupted/failed downloads to free up the space(Only PSV Updates/PSX/PSP games)?

    In case of PSV content: Simply remove queued download in your livearea. If that doesn't work for any reason, you can always delete folder within `ux0:bgdl/t/` - each download will be in separate folder by the order in which they were queued.

    For everything else: `ux0:pkgj` folder - each download will be in separate folder by its title id. Simply delete the folder & resume file.

2. Download speed is too slow!

    Typically you should see speeds ~1-2 MB/s. This is normal for Vita hardware. Of course it also depends on WiFi router you have and WiFi signal strength. But sometimes speed will drop down to only few hundred KB/s. This happens for pkg files that contains many small files or many folders. Creating a new file or a new folder takes extra time which slows down the download.
3. Cant background download for PSP games and dont appear on LiveArea
    
    To background download PSP Games, and to launch directly from livearea outside Adrenaline
    you will need the [NoPspEmuDrm][] plugin.
    
    also doing this will make them download as EBOOT files instead of ISO files.
4. I want to install PSP games as EBOOT file.

    Installing PSP games as EBOOT files is possible. It allows to install games faster and make them take less space. However, you will need to install the [npdrm_free][] plugin to make them work.

    To install PSP games as EBOOT files, just add the following line to your config:
    ```
    install_psp_as_pbp 1
    ```

    If you want to switch back to the other mode, simply remove the line. Writing 0 is not sufficient.

5. I can't play PSP games, it says "The game could not be started (80010087)".

    You need to install the [npdrm_free][] plugin in VSH, or install games as ISO.

6. The PSM Games don't work.

    If you followed the instructions for [NoPsmDrm][], you can try to activate your account for psm games using [NoPsmDrm Fixer](https://github.com/Yoti/psv_npdrmfix).

7. Can't download Updates or DLCs on my PSTV

    This error is caused by AntiBlackList. To fix it, completely undo then uninstall AntiBlackList and install [DolcePolce](https://silica.codes/Li/dolcepolce) plugin instead.

8. How do I use compatibility packs?

    Compatiblity packs are deprecated and disabled by default. It is recommended to use [reF00D](https://github.com/dots-tb/reF00D) or [0syscall6](https://github.com/SKGleba/0syscall6). If you would still like to use compatiblity packs, set `url_comppack` to `https://gitlab.com/nopaystation_repos/nps_compati_packs/raw/master/` in the config file. Firmwares 3.65 or lower require a workaround for TLS. The compatibility pack list has not been updated since Oct 2019.

# Building

pkgj uses conan and cmake to build. The setup is a bit tedious, so the
recommended way is to run ci/ci.sh. It will create a Python virtualenv with
conan, setup the configuration for cross-compilation, register some recipes,
and then run cmake and build pkgj for your vita and pkgj_cli for testing.

Prerequisites:

*  Debian packages (or their equivalents):

  - build-essential
  - git-core
  - make
  - cmake
  - python3-pip
  - pipenv (pip3 install --user pipenv)
  - ninja-build

pkgj will be built in ci/build, you can rebuild it anytime you want by running
ninja in that same directory.

You can set environment variable `PSVITAIP` (before running cmake) to IP address of
Vita, that will allow to use `make send` for sending eboot.bin file directly to `ux0:app/PKGJ00000` folder.

To enable debugging logging pass `-DPKGI_ENABLE_LOGGING=ON` argument to cmake. Then application will send debug messages to
UDP multicast address 239.255.0.100:30000. To receive them you can use [socat][] on your PC:

    $ socat udp4-recv:30000,ip-add-membership=239.255.0.100:0.0.0.0 -

# Publishing a release (for maintainers)

Pushing a tag in the form `v0.56` will create a new release and build
`pkgj.vpk`.

If you want to build a beta, you can push a tag in the form `v0.56-beta1` which
will create a pre-release. Such a release will not be picked up by the auto
update.

# License

This software is released under the 2-clause BSD license.

puff.h and puff.c files are under [zlib][] license.

[NoNpDrm]: https://github.com/TheOfficialFloW/NoNpDrm/releases
[npdrm_free]: https://github.com/kyleatlast/npdrm_free/releases
[NoPsmDrm]: https://github.com/frangarcj/NoPsmDrm/
[NoPspEmuDrm]: https://github.com/LiEnby/NoPspEmuDrm
[Adrenaline]: https://github.com/TheOfficialFloW/Adrenaline
[zrif_online_converter]: https://rawgit.com/mmozeiko/pkg2zip/online/zrif.html
[pkg_dec]: https://github.com/weaknespase/PkgDecrypt
[pkg_releases]: https://github.com/blastrock/pkgj/releases
[vitasdk]: https://vitasdk.org/
[libvita2d]: https://github.com/xerpi/libvita2d
[PSDLE]: https://repod.github.io/psdle/
[socat]: http://www.dest-unreach.org/socat/
[zlib]: https://www.zlib.net/zlib_license.html
[pkgj_downloads]: https://github.com/blastrock/pkgj/releases
[pkgj_latest]: https://github.com/blastrock/pkgj/releases/latest
[pkgj_license]: https://github.com/blastrock/pkgj/blob/master/LICENSE
[img_downloads]: https://img.shields.io/github/downloads/blastrock/pkgj/total.svg?maxAge=3600
[img_latest]: https://img.shields.io/github/release/blastrock/pkgj.svg?maxAge=3600
[img_license]: https://img.shields.io/github/license/blastrock/pkgj.svg?maxAge=2592000

# Donating

Bitcoin: 128vikqd3AyNEXEiU5uSJvCrRq1e3kRX6n

Monero: 45sCwEFcPD9ZfwD2UKt6gcG3vChFrMmJHUmVVBUWwPFoPsjmkzvN7i9DKn4pUkyif5axgbnYNqU3NCqugudjTWqdFv5uKQV
