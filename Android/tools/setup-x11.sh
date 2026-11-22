#!/bin/sh
# Fetches Termux:X11 into third_party/termux-x11 at the commit pocket-foundry pins, and applies
# third_party/patches/termux-x11 (pocket-foundry's 0001-0018, plus our 0019 (Windows host build), 0020 (show the cursor when the game moves the pointer) and 0021 (clear the X server CPU mask)).
# Safe to rerun: a tree whose patches are already applied is left alone. Runs in Git Bash on Windows.
set -e
cd "$(dirname "$0")/.."
x11=third_party/termux-x11
commit=e3a1657b781f7a880160d7ce5cdd48b866e062dd
if [ ! -d "$x11/.git" ]; then
    git -c core.autocrlf=false clone --no-checkout https://github.com/termux/termux-x11 "$x11"
    git -C "$x11" config core.autocrlf false
    git -C "$x11" checkout -q "$commit"
fi
# Symlinks need Developer Mode on Windows; the only ones (pixman's CI files) aren't built, so take them as text files.
git -C "$x11" -c core.symlinks=false -c core.autocrlf=false submodule update --init --recursive --depth 1
git -C "$x11" submodule foreach --quiet --recursive 'git config core.symlinks false; git config core.autocrlf false'
# Patches with CRLF line endings (a checkout with core.autocrlf=true) don't apply.
sed -i 's/\r$//' third_party/patches/termux-x11/*.patch
applied=$(git -C "$x11" rev-parse --absolute-git-dir)/utandroid-patches
if [ -d "$applied" ] && diff -r "$applied" third_party/patches/termux-x11 > /dev/null; then
    echo "Termux:X11's patches are applied"
    exit
fi
# Back to the pinned commit before applying a changed set (patches overlap, so they can't go on one by one again).
git -C "$x11" checkout -q -- .
rm -rf "$applied"
for p in third_party/patches/termux-x11/*.patch; do
    git -C "$x11" apply "../../$p"
    echo "applied: $(basename "$p")"
done
mkdir -p "$applied"
cp third_party/patches/termux-x11/*.patch "$applied/"
