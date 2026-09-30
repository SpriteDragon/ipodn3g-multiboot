# ipodn3g-multiboot

<img src="https://hostrepo.tailmuncher.org/n3g/preview.jpg" alt="ipodn3g-multiboot preview" width="300" />

A bootloader for the iPod 2g, flashed with `mks5lboot.exe`.

## To build inside WSL:

Run:
```bash
git clone [https://github.com/SpriteDragon/ipodn3g-multiboot.git](https://github.com/SpriteDragon/ipodn3g-multiboot.git) nano3_bootloader

```

Run:

```bash
F=bootloader/ipod-s5l87xx-menu.c; grep -q '^extern void bss_init(void);$' "$F" || sed -i '/^extern int line;$/i extern void bss_init(void);' "$F"; grep -q '^    bss_init();$' "$F" || sed -i '/^    system_init();$/i\    bss_init();' "$F"

```

Then:

```bash
bash scripts/build_wsl.sh

```

Download the n3g rockbox firmware [here](https://hostrepo.tailmuncher.org/n3g/.rockbox.zip) to put it on the device

## Then In regular cmd (admin)

run this, make sure the compiled `bootloader-ipodnano3g.ipod` file is in out/ folder and the `mks5lboot.exe` is in the same root folder:

```cmd
mks5lboot.exe --bl-inst out/bootloader-ipodnano3g.ipod

```

When in USB mode, if it says the drive must be formatted, do it.
