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
* **installs** game updates, DLCs, PSM, PSP and PSX games.

Current limitations:
* **no background downloads** - if application is closed or Vita is put in sleep then download will stop.

# Download

Get latest version as [vpk file here][pkgj_latest].

# Setup instructions

Setup games databases URLs in `ux0:pkgi/config.txt`. The file format is the following:

    url_games http://thesite/games.tsv
    url_updates http://thesite/updates.tsv
    url_dlcs http://thesite/dlcs.tsv
    url_psx_games http://thesite/psxgames.tsv
    url_psp_games http://thesite/pspgames.tsv
    url_psm_games http://thesite/psmgames.tsv
    url_comppack http://thesite/comppack/

**Attention:** The PS Vita has an imcomplete HTTPS support and most sites will not
work, prefer HTTP in these cases.

The `url_comppack` URL must point to the folder containing the `entries.txt`
file.

Make sure unsafe mode is enabled in Henkaku settings.

Then start the application and you are ready to go.

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

4. I want to install PSP games as EBOOT file.

  Installing PSP games as EBOOT files is possible. It allows to install games
  faster and make them take less space. However, you will need to install
  the [npdrm_free][] plugin to make them work.

  To install PSP games as EBOOT files, just add the following line to your
  config:

    install_psp_as_pbp 1

  If you want to switch back to the other mode, simply remove the line. Writing
  0 is not sufficient.

5. I can't play PSP games, it says "The game could not be started (80010087)".

  You need to install the [npdrm_free][] plugin in VSH, or install games as ISO.

6. I want to install PSP and PSX games on another partition.

  You can change the partitions these games are installed to with the following
  configuration line:

    install_psp_psx_location uma0:

  The default value is `ux0:`

7. I want to play PSM Games!

  You need to install the [NoPsmDrm][] plugin and follow the setup instructions.
  After installing a PSM game, you'll need to refresh your livearea. You can do
  that by booting into the recovery menu and selecting `Rebuild Database`.
  This will also reset your livearea layout. To enable psm downloads and
  prove that you read this readme, add "psm_disclaimer_yes_i_read_the_readme"
  and the name of the needed plugin to the config file.

8. The PSM Games don't work.

  If you followed the instructions for [NoPsmDrm][], you can try to activate
  your account for psm games using [NoPsmDrm
  Fixer][https://github.com/Yoti/psv_npdrmfix].

9. I don't want to loose my livearea layout/This is too much effort, there
   should be a better way.

  Warning: This method may **format** your memory card, if you're not careful.

  Well yes, there is. You can trigger a database refresh by removing `ux0:id.dat`
  and rebooting.

  But: When using a Slim or a PS TV and an official memory card, you'll be asked if you
  "want to transfer the data on the internal memory card to the removable memory card".
  If you press "Yes" here, your memory card will be **formatted**. Just press
  "No". In case you're using a Fat, SD2VITA or USB storage, there's no risk.

10. I'VE ADDED THE PSM URL TO THE CONFIG AND THERES NO MENU OPTION!!

  Please read this Q&A carefully

# Building

pkgj uses conan and cmake to build. The setup is a bit tedious, so the
recommended way is to run ci/ci.sh. It will create a Python virtualenv with
conan, setup the configuration for cross-compilation, register some recipes,
and then run cmake and build pkgj for your vita and pkgj_cli for testing.

pkgj will be built in ci/build, you can rebuild it anytime you want by running
ninja in that same directory.

You can set environment variable `PSVITAIP` (before running cmake) to IP address of
Vita, that will allow to use `make send` for sending eboot.bin file directly to `ux0:app/PKGI00000` folder.

To enable debugging logging pass `-DPKGI_ENABLE_LOGGING=ON` argument to cmake. Then application will send debug messages to
UDP multicast address 239.255.0.100:30000. To receive them you can use [socat][] on your PC:

    $ socat udp4-recv:30000,ip-add-membership=239.255.0.100:0.0.0.0 -

# License

This software is released under the 2-clause BSD license.

puff.h and puff.c files are under [zlib][] license.

[NoNpDrm]: https://github.com/TheOfficialFloW/NoNpDrm/releases
[npdrm_free]: https://github.com/kyleatlast/npdrm_free/releases
[NoPsmDrm]: https://github.com/frangarcj/NoPsmDrm/
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

# Donating

Bitcoin: 128vikqd3AyNEXEiU5uSJvCrRq1e3kRX6n
Monero: 45sCwEFcPD9ZfwD2UKt6gcG3vChFrMmJHUmVVBUWwPFoPsjmkzvN7i9DKn4pUkyif5axgbnYNqU3NCqugudjTWqdFv5uKQV
