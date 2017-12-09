#!/bin/sh

xdg-desktop-menu uninstall ferret.desktop
rm -f `cat build/install_manifest.txt`
