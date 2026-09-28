# ipodn3g-multiboot
A bootloader for the iPod 2g, flashed with mks5lboot.exe


To build inside WSL:
 run:
   git clone https://github.com/SpriteDragon7/ipodn3g-multiboot.git nano3_bootloader
 run: 
   F=bootloader/ipod-s5l87xx-menu.c; grep -q '^extern void bss_init(void);$' "$F" || sed -i '/^extern int line;$/i extern void bss_init(void);' "$F"; grep -q '^    bss_init();$' "$F" || sed -i '/^    system_init();$/i\    bss_init();' "$F"
 then:
   bash scripts/build_wsl.sh
