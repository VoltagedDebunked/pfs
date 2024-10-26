# PFS - Platformer from scratch

Hello,

Today, i've wanted to make something cool, like a platformer. But, with a twist. You see, instead of using a game development tool like RayLib or Unity, i will use plain FrameBuffer and C to create a platformer game. This way, you will learn how to create a game from scratch.

**NOTE: This game is Linux only as it uses `<linux/fb.h>` to control framebuffer. Porting this to other platforms like Windows might cause bugs or stupid stuff that is not intended.**

This is a very basic example of how to create a platformer game. You can expand it to make it more complex.

First, let's install some required packages:

1. Install GCC:

   For Ubuntu/Debian:

   ```bash
   sudo apt-get install gcc
   ```

   For Arch Linux/Manjaro:

   ```bash
   sudo pacman -S gcc
   ```

   For Fedora:

   ```bash
   sudo dnf install gcc
   ```

2. Build the game:

   ```bash
   make
   ```

This will create an executable named `pfs`.

## Running the game

1st, compile the game:

   ```bash
   make
   ```

2nd, enter TTY mode  by pressing Ctrl+Alt+F1 or F2, then login with your user and password.

CD into the game directory, where you cloned the repo.
Then, run the game:

   ```bash
  ./pfs
   ```

   Now you can play the game!

## Licensing

[GPL-3.0](LICENSE)
