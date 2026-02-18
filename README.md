![logo](pin.svg)

# pin

Pin image/screenshot on your linux desktop. Just a simple frameless always-on-top window showing image.

Primarily for KDE [Spectacle](https://apps.kde.org/spectacle/), a screenshot app that currently doesn't have builtin pin feature.

https://github.com/user-attachments/assets/8ae41134-435e-46e2-a792-3e9e04a41aeb

## Features

- Pin image (png, jpg and bmp format)
- Scroll mouse wheel to resize
- Hold ctrl and scroll mouse wheel to change opacity
- Drag to move the window
- Middle click to close the window
- Right click to reset size and opacity

You can pin screenshot from spectacle using the `export` button

You can pin existing image from context menu using `open with`

You can pin image from the command line

```sh
pin a.png
```

## Install

Clone this repo and

```sh
sudo make release_cpu install
```

You may need to adjust the installation path in the Makefile.

Only tested on archlinux with KDE plasma (wayland).

## cpu vs gpu

There's two version can be built (check the [Makefile](Makefile)).

The cpu version has much lower ram usage (~10MB) compare to the gpu version (~160MB).

## Thanks
* [RGFW](https://github.com/ColleagueRiley/RGFW) used to initialize OpenGL

* [LearnOpenGL CN](https://learnopengl-cn.github.io/) a greate OpenGL tutorial

* [stb](https://github.com/nothings/stb) used to load image and resize image

* Tsoding's [video](https://www.youtube.com/watch?v=764fnfEb1_c) for showing how to use Xlib

* Tsoding's another [video](https://www.youtube.com/watch?v=0rUBhqR6ckw) for introducing RGFW

* [gf](https://github.com/nakst/gf) greate GDB frontend
