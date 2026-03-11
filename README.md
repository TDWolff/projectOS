# ProjectOS

ProjectOS is a small 64-bit hobby operating system kernel written in C and x86_64 assembly. It boots via **Limine** and targets a **graphical framebuffer** environment (ex: 1024×768×32 requested via Multiboot2).

The project currently includes:

- A freestanding x86_64 kernel (Clang + NASM)
- Basic memory managers (PMM + early VMM scaffolding)
- Interrupt/IDT plumbing and simple drivers (timer/keyboard/mouse)
- A simple graphics layer (framebuffer putpixel + primitives)
- A basic compositor with a tri-buffer model (desktop canvas → composition → frontbuffer)
- A simple window manager (macOS-style frame, draggable title bar, clamped to screen)
- A System UI layer (top bar + dock, RTC-backed clock)
- An initrd-backed file store (for settings/assets)
- A tiny userspace app format (".pexe") and a sample stress-test app

> Note: This is a hobby OS and is not meant to be secure or production-ready.

---

## Prerequisites

You’ll need:

- **clang** (able to target `x86_64-pc-none-elf`)
- **nasm**
- **x86_64-elf-ld** (binutils cross linker)
- (Recommended) **qemu-system-x86_64** to run the ISO

On Apple Silicon (M1/M2/etc.), you’ll still be producing an **x86_64** kernel and running it under emulation.

---

## Build the kernel

From the repo root:

```sh
make
```

This produces:

- `kernel.bin`

---

## Build the sample userspace app

There’s a separate makefile for apps:

```sh
make -f Makefile.apps
```

This produces (and places into `osstorage/`):

- `osstorage/stress_test.pexe`

---

## Build an ISO

The main Makefile includes an `iso` target (Limine-based). It expects Limine to be built as well:

```sh
make iso
```

This will (re)create `iso_root/` and populate it with the kernel and Limine configuration.

> If you’re modifying what goes into the ISO, check `limine.conf.template` and the `iso` recipe in `Makefile`.

---

## Run in QEMU (example)

This repo doesn’t currently ship a single canonical run command in the Makefile, but a typical Limine ISO run looks like:

```sh
qemu-system-x86_64 \
  -cdrom os.iso \
  -m 512M \
  -serial stdio
```

Where `os.iso` is the ISO you generate (your ISO filename depends on how you package the final image; if you add an ISO creation step that outputs a specific name, update this section accordingly).

Optionally you can just run ```make run```, which will execute the above command (assuming you have `qemu-system-x86_64` installed and in your PATH).
---

## Configuration (settings)

ProjectOS loads settings from `osstorage/settings.pset` via the initrd.

Example keys:

- `bg_color=0xFF008080`
- `dock_alpha=120`
- `show_clock=1`
- `show_seconds=1`
- `timezone=-8`
- `clock_pos=right`

---

## Development tips

- Window dragging logic is in `src/drivers/window.c` (`window_handle_mouse`).
- Top bar constants (height, alpha, etc.) live in `src/drivers/systemui.c`.
- Mouse input comes from `src/drivers/mouse.c` and is rendered by the compositor.

---

## License

No license included, this project is open source. Feel free to use the code as you like, I'll continue to provide updates and improvements as I work on the OS. If you find any bugs or have suggestions, please open an issue or submit a pull request!