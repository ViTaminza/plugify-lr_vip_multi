# lr_vip_multi (Plugify)

[LR][VIP] Multi: registers the VIP feature 'LrBoost' (VIP_BOOL, TOGGLABLE; the per-group value of groups.ini is a decimal) and multiplies the experience of a kill by that value for VIPs. Ported from the Pisex Levels Ranks module LR_VIP_Multi. Requires the levels_ranks and vip cores; no exported methods.

Plugify port (fork) of **LR_VIP_Multi ([LR][VIP] Multi)** by Pisex (Metamod (C++)).
Original: https://github.com/Pisex/cs2-lvl_ranks_modules
The code was rewritten for Plugify / s2sdk; behaviour follows the original.

## Requirements
- [Plugify](https://github.com/untrustedmodders/plugify) with [s2sdk](https://github.com/untrustedmodders/plugify-plugin-s2sdk)
- Plugify dependencies: s2sdk, levels_ranks, vip, hudmenu (optional)

## Installation
Download `lr_vip_multi-1.0.0.zip` from Releases and extract it into `game/csgo/`.

## Building
Requires Docker. `./build.sh` builds in the Steam Runtime 3 (sniper) SDK image; the package
is written to `build/package/lr_vip_multi`.

`plugins/levels_ranks`, `plugins/vip` carry only the API headers of the plugins this one uses.

## License
GPL-3.0, see LICENSE.

