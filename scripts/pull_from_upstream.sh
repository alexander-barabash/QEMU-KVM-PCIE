#!/bin/sh
git clone https://github.com/MentorEmbedded/qemu-esd.git
git remote add --mirror=fetch upstream git://git.qemu.org/qemu.git
git pull --rebase upstream master
#git push -u origin master

