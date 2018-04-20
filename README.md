# pkgj

[![Travis CI Build Status][img_travis]][pkgj_travis] [![Downloads][img_downloads]][pkgj_downloads] [![Release][img_latest]][pkgj_latest] [![License][img_license]][pkgj_license]

pkgj allows to install original pkg files on your Vita.

This homebrew allows to download & unpack pkg file directly on Vita together with your [NoNpDrm][] fake license.

# Features

* **easy** way to see list of available downloads, including searching, filter & sorting.
* **standalone**, no PC required, everything happens directly on Vita.
* **automatic** download and unpack, just choose an item, and it will be installed, including bubble in live area.

Differences with PKGi:
* **queues** multiple downloads.
* **supports** the TSV file format.
* **installs** game updates, DLCs and PSX games.

Current limitations:
* **no support for PSM**.
* **no background downloads** - if application is closed or Vita is put in sleep then download will stop.

# Download

Get latest version as [vpk file here][pkgj_latest].

# Setup instructions

Setup games databases URLs in `ux0:pkgi/config.txt`. The file format is the following:

    url_games http://thesite/games.tsv
    url_updates http://thesite/updates.tsv
    url_dlcs http://thesite/dlcs.tsv
    url_psx_games http://thesite/psxgames.tsv

Then start the application and you are ready to go.

To avoid downloading pkg file over network, you can place it in `ux0:pkgi` folder. Keep the name of file same as in http url,
or rename it with same name as contentid. pkgj will first check if pkg file can be read locally, and only if it is missing
then pkgj will download it from http url.

# Usage

Using application is pretty straight forward. Select item you want to install and press X. To sort/filter/search press triangle.
It will open context menu. Press triangle again to confirm choice(s) you make in menu. Or press O to cancel any changes you did.

Press left or right button to move page up or down.

# Q&A

1. Where to get zRIF string? 

  You must use [NoNpDrm][] plugin to dump existing games you have. Plugin will generate rif file with fake license.
  Then you can use either [web page][zrif_online_converter] or [make_key][pkg_dec] to convert rif file to zRIF string.

2. Where to get pkg URL?

  You can use [PSDLE][] to find pkg URL for games you own. Then either use original URL, or host the file on your own server.

3. Where to remove interrupted/failed downloads to free up the space?

  In `ux0:pkgi` folder - each download will be in separate folder by its title id. Simply delete the folder & resume file.

4. Download speed is too slow!

  Typically you should see speeds ~1-2 MB/s. This is normal for Vita hardware. Of course it also depends on WiFi router you
  have and WiFi signal strength. But sometimes speed will drop down to only few hundred KB/s. This happens for pkg files that
  contains many small files or many folders. Creating a new file or a new folder takes extra time which slows down the download.

# Building

You need to have [Vita SDK][vitasdk] with [libvita2d][] installed.

Run `cmake .` to create debug build, or `cmake -DCMAKE_BUILD_TYPE=Release .` to create optimized release build.

After than run `make` to create vpk file. You can set environment variable `PSVITAIP` (before running cmake) to IP address of
Vita, that will allow to use `make send` for sending eboot.bin file directly to `ux0:app/PKGI00000` folder.

To enable debugging logging pass `-DPKGI_ENABLE_LOGGING=ON` argument to cmake. Then application will send debug messages to
UDP multicast address 239.255.0.100:30000. To receive them you can use [socat][] on your PC:

    $ socat udp4-recv:30000,ip-add-membership=239.255.0.100:0.0.0.0 -

For easer debugging on Windows you can build pkgj in "simulator" mode - use Visual Studio 2017 solution from simulator folder.

# License

This software is released under the 2-clause BSD license.

puff.h and puff.c files are under [zlib][] license.

[NoNpDrm]: https://github.com/TheOfficialFloW/NoNpDrm
[zrif_online_converter]: https://rawgit.com/mmozeiko/pkg2zip/online/zrif.html
[pkg_dec]: https://github.com/weaknespase/PkgDecrypt
[pkg_releases]: https://github.com/blastrock/pkgj/releases
[vitasdk]: https://vitasdk.org/
[libvita2d]: https://github.com/xerpi/libvita2d
[PSDLE]: https://repod.github.io/psdle/
[socat]: http://www.dest-unreach.org/socat/
[zlib]: https://www.zlib.net/zlib_license.html
[pkgj_travis]: https://travis-ci.org/blastrock/pkgj/
[pkgj_downloads]: https://github.com/blastrock/pkgj/releases
[pkgj_latest]: https://github.com/blastrock/pkgj/releases/latest
[pkgj_license]: https://github.com/blastrock/pkgj/blob/master/LICENSE
[img_travis]: https://api.travis-ci.org/blastrock/pkgj.svg?branch=master
[img_downloads]: https://img.shields.io/github/downloads/blastrock/pkgj/total.svg?maxAge=3600
[img_latest]: https://img.shields.io/github/release/blastrock/pkgj.svg?maxAge=3600
[img_license]: https://img.shields.io/github/license/blastrock/pkgj.svg?maxAge=2592000
