# RoLiteRPC
A lightweight background program that enables Rich Presence for Roblox Player.<br>
Requires no credentials or installing.

## How to Use
* Download a version from the [releases](https://github.com/EmK530/BloxDump/releases) page.<br>
* Extract the program and DLLs into a folder.<br>
* Then simply run RoLiteRPC and you should have Rich Presence for what you're playing!

## Drawbacks
Because of the goal of being a "no setup" program, for proper detection you have to join games<br>
from the Roblox website for RoLiteRPC to properly detect what you are playing.<br>

Switching games (using in-app or by game teleport) will not be detected properly.<br>
Joining friends will also break detection of what you're playing.

## Visual Studio Setup
In case you want to modify the code for yourself, you'll need to perform the necessary setup.<br>
The solution may not work out of the box (bummer, I know.)<br>

There are two packages that I've installed using `vcpkg`<br>
Find a guide on how to install packages with it and get these:<br>
```
vcpkg install curl:x86-windows
vcpkg install nlohmann-json:x86-windows
```
