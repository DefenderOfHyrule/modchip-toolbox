# Modchip Toolbox (formerly hwfly_toolbox)

Modchip Toolbox is a fork of hwfly_toolbox. The main goal of this project is to add the functionality of the previously closed source picofly_toolbox payload to hwfly_toolbox and "rebrand" the entirety of the project to a new name, to indicate that it's a tool that can be used with modchips in general, instead of only one type/make of modchip.

This project has taken multiple years to complete, but i'm proud to present this now.

The way this project was made possible, was by doing the following:

- Reverse engineering the original picofly_toolbox API used to flash firmware to the RP2040,
- Using [usk](https://github.com/DefenderOfHyrule/usk) as documentation, to verify pinouts and addresses where data needs to be written to.

## Features

This payload contains fully reverse engineered functionality of the original picofly_toolbox, the existing hwfly_toolbox functionality + some extra QoL features. 

This functionality being the following:

- View installed firmware info,
- Update the firmware via update.bin on the root of your SD card,
- Roll back to the previously installed firmware version,
- Back up and restore sdloader,
- Reset training data,
- Original hwfly_toolbox functionality in its own separate sub menu,
- Horizontally rendered UI instead of vertical (portrait),
- Navigation using the face buttons of joycons/the Switch lite (in addition to volume buttons + power),
- Up to date hekate BDK (6.5.1).

## Building/Compiling Modchip Toolbox

There is no complicated process of compiling Modchip Toolbox.

1. You clone the project with `git clone https://github.com/DefenderOfHyrule/modchip-toolbox`
2. You `cd` into the project root
3. You run `make` from terminal/cmd

The final payload will be stored in `/output/modchip_toolbox.bin`.

## Credits

Besides my own efforts, credit also goes to [auggeythecat](https://github.com/auggeythecat) for a providing some thought while I was brainstorming :P.
