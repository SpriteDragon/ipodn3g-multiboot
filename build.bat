@echo off
arm-none-eabi-gcc -c main.c -o main.o -mcpu=arm926ej-s -nostdlib
arm-none-eabi-ld -T linker.ld main.o -o bootmenu.elf
arm-none-eabi-objcopy -O binary bootmenu.elf custom_bootloader.bin
echo Compilation Finished! Produced custom_bootloader.bin
pause
