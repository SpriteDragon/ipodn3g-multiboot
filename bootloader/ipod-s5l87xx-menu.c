/***************************************************************************
 *             __________               __   ___.
 *   Open      \______  \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Nano 3G scrolling boot-menu bootloader (Loader2-style).
 *
 * Drop-in REPLACEMENT for bootloader/ipod-s5l87xx.c, selected for
 * IPOD_NANO3G builds by the patched bootloader/SOURCES. It reuses the
 * exact same platform bring-up and load paths as the stock bootloader
 * (load_firmware(), disk/NAND-FTL initialisation, norboot/IM3 helpers)
 * but instead of auto-booting rockbox.ipod it shows a scrollable menu:
 *
 *      Rockbox          -> load BOOTFILE ("rockbox.ipod") from the FTL
 *                          volume and jump to it (identical to stock).
 *      iPod OS (Apple)  -> chain-load the Apple NOR bootloader that
 *                          mks5lboot backed up during a dual-boot
 *                          install (IM3 image behind the primary one),
 *                          with a big WARNING first (see OF note).
 *      Custom payload   -> load a 3rd firmware image from the FAT32
 *                          data partition (configurable paths below),
 *                          either Rockbox-scrambled (.ipod) or raw ARM
 *                          binary, and jump to it. If that image
 *                          RETURNS, the menu reappears -- handy for
 *                          testing throwaway payloads.
 *      Bootloader USB   -> the stock USB mass-storage recovery mode.
 *      Shut down        -> power_off().
 *
 * Controls: wheel scroll (or LEFT/RIGHT) moves the selection,
 * SELECT (or PLAY) runs it, MENU cancels / boots the default instantly.
 * After BM_TIMEOUT_SECS with no input, BM_DEFAULT_ITEM boots.
 *
 * ---- OF (Apple) note -------------------------------------------------
 * Per the mks5lboot README in this tree: handing control to Apple's
 * NOR boot on a Nano 3G whose NAND has been formatted by Rockbox IS
 * DESTRUCTIVE -- Apple's firmware writes to the NAND and wipes the
 * Rockbox FTL. The entry therefore asks for a second confirmation on
 * screen. It is only useful if you restored the Apple OS with iTunes
 * (i.e. the NAND is back in Apple's format), otherwise let it be.
 * ----------------------------------------------------------------------
 *
 * Copyright (C) 2005 by Dave Chapman (stock bootloader this is based on)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF
 * ANY KIND, either express or implied.
 ****************************************************************************/
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "config.h"
#include "inttypes.h"
#include "cpu.h"
#include "system.h"
#include "lcd.h"
#include "../kernel-internal.h"
#include "file_internal.h"
#include "storage.h"
#include "disk.h"
#include "font.h"
#include "backlight.h"
#include "backlight-target.h"
#include "button.h"
#include "panic.h"
#include "power.h"
#include "file.h"
#include "common.h"
#include "rb-loader.h"
#include "loader_strerror.h"
#include "version.h"
#include "powermgmt.h"
#include "usb.h"

#include "s5l87xx.h"
#include "clocking-s5l8702.h"
#include "spi-s5l8702.h"
#include "i2c-s5l8702.h"
#include "gpio-s5l8702.h"
#include "pmu-target.h"
#include "crypto-s5l8702.h"     /* struct Im3Info, IM3HDR_SZ */
#include "norboot-target.h"     /* im3_read(), NORBOOT_OFF, im3_nor_sz() */

/* ------------------------------------------------------------------ */
/* ------------------------- CUSTOMIZE HERE ------------------------- */
/* ------------------------------------------------------------------ */

#define BM_TITLE            "N A N O 3 G   B O O T M E N U"

/* Which item is highlighted at boot and boots on timeout (0-based). */
#define BM_DEFAULT_ITEM     0          /* 0 = Rockbox */

/* Auto-boot countdown in seconds. Set to 0 to always wait for input. */
#define BM_TIMEOUT_SECS     6

/* How long SELECT (or PLAY) must be held continuously to boot the
 * highlighted entry. Releasing early cancels. Set to 0 for an instant
 * press instead of a hold. */
#define BM_HOLD_SECS        5

/* Rockbox entry (leave as-is to match stock behaviour). */
#define BM_RB_NAME          "Rockbox"
#define BM_RB_DESC          "Boot rockbox.ipod (/.rockbox build)"

/* Apple entry. */
#define BM_OF_NAME          "iPod OS (Apple)"
#define BM_OF_DESC          "Chainload Apple's NOR bootloader"

/* Custom firmware entries are defined in the boot_items[] table further
 * down -- search for "ADD YOUR OWN BOOT ENTRIES HERE". Each one is just
 * a label + description + a path on the data volume. A file is jumped to
 * raw if it begins with an ARM branch instruction, otherwise it is loaded
 * through the Rockbox image loader (so a scramble-created .ipod works). */

/* Utility entries. Delete their table lines in boot_items[] to hide. */
#define BM_USB_NAME         "Bootloader USB mode"
#define BM_USB_DESC         "Mass-storage recovery (reinstall files)"
#define BM_PWR_NAME         "Shut down"

/* Colours. */
#define LCD_RBYELLOW        LCD_RGBPACK(255,192,0)
#define LCD_REDORANGE       LCD_RGBPACK(255,70,0)
#define LCD_GREEN           LCD_RGBPACK(0,255,0)
#define LCD_SEL_BG          LCD_RGBPACK(60,140,255)

/* ------------------------------------------------------------------ */
/* --------------------- end of user settings ----------------------- */
/* ------------------------------------------------------------------ */

#define ERR_RB      0
#define ERR_OF      1
#define ERR_STORAGE 2

/* Safety measure - same as the stock bootloader. */
#define MAX_LOADSIZE (8*1024*1024)

extern int line;
extern void bss_init(void);
extern uint32_t _movestart;
extern uint32_t start_loc;

#ifndef ARRAYLEN
#define ARRAYLEN(x) (sizeof(x) / sizeof((x)[0]))
#endif

/* Screen geometry (sysfont is the only font in bootloader builds). */
#define ITEM_PITCH  (SYSFONT_HEIGHT + 12)   /* pixel pitch per row     */
#define MENU_TOP    (6*SYSFONT_HEIGHT + 10) /* first row Y             */
#define ITEM_X      22
#define DESC_Y      (LCD_HEIGHT - 3*SYSFONT_HEIGHT - 8)
#define FOOT_Y      (LCD_HEIGHT - SYSFONT_HEIGHT - 4)

/* Stock S5L8702 Apple NOR-boot handoff. This is intentionally kept
 * byte-for-byte equivalent in behavior to bootloader/ipod-s5l87xx.c:
 * the current IM3 header tells us where mks5lboot backed up Apple's IM3,
 * im3_read() validates/decrypts it into IRAM0, then control branches there. */
static int launch_onb(int clkdiv)
{
    spi_clkdiv(SPI_PORT, clkdiv);

    struct Im3Info *hinfo = (struct Im3Info *)IRAM1_ORIG;
    int rc = im3_read(NORBOOT_OFF + im3_nor_sz(hinfo), hinfo,
                      (void *)IRAM0_ORIG);
    if (rc != 0)
    {
        /* im3_read overwrote the vector table while attempting the load. */
        memcpy((void *)IRAM0_ORIG, &_movestart,
               4 * (&start_loc - &_movestart));
        commit_discard_idcache();
        return rc;
    }

    eint_init();
    commit_discard_idcache();
    asm volatile("mov pc, %0"::"r"(IRAM0_ORIG));
    while (1);
}

static int kernel_launch_onb(void)
{
    disable_irq();
    int rc = launch_onb(3); /* 54 MHz / 4 = 13.5 MHz SPI */
    enable_irq();
    return rc;
}

/* pmu_is_hibernated() is already implemented by the Nano 3G target PMU
 * driver and declared by pmu-target.h. Do not define a local copy. */

/* ------------------------------------------------------------- UI - */

static void ui_center(int y, const char *s)
{
    int w, h;
    lcd_getstringsize((const unsigned char *)s, &w, &h);
    lcd_putsxy((LCD_WIDTH - w) / 2, y, (const unsigned char *)s);
}

static void ui_header(void)
{
    lcd_set_foreground(LCD_RBYELLOW);
    lcd_fillrect(0, 0, LCD_WIDTH, SYSFONT_HEIGHT + 10);
    lcd_set_foreground(LCD_BLACK);
    ui_center(5, BM_TITLE);
    lcd_set_foreground(LCD_WHITE);
}

static void ui_footer(int countdown_secs)
{
    char buf[64];
    lcd_set_foreground(LCD_RBYELLOW);
    if (button_hold())
        snprintf(buf, sizeof(buf), "hold switch is ON");
    else if (countdown_secs >= 0)
        snprintf(buf, sizeof(buf), "auto-boot in %ds - wheel stops it",
                 countdown_secs);
    else
        snprintf(buf, sizeof(buf),
                 "wheel: move   hold centre %ds: boot", BM_HOLD_SECS);
    ui_center(FOOT_Y, buf);
    lcd_set_foreground(LCD_WHITE);
}

/* ------------------------------------------------ USB (stock copy) - */
#ifdef HAVE_BOOTLOADER_USB_MODE
static void usb_mode(void)
{
    int button;
    verbose = true;
    printf("Entering USB mode...");
    powermgmt_init();
    /* The code will ask for the maximum possible value */
    usb_charging_enable(USB_CHARGING_ENABLE);
    usb_init();
    usb_start_monitoring();
    /* Wait until USB is plugged */
    while (usb_detect() != USB_INSERTED)
    {
        printf("Plug USB cable");
        line--;
        sleep(HZ/10);
    }
    while(1)
    {
        button = button_get_w_tmo(HZ/10);
        if (button == SYS_USB_CONNECTED)
            break; /* Hit */
        if (usb_detect() == USB_EXTRACTED)
            break; /* Cable pulled */
        printf("USB: Connecting...");
        line--;
    }
    if (button == SYS_USB_CONNECTED)
    {
        /* Got the message - wait for disconnect */
        printf("Bootloader USB mode");
        usb_acknowledge(SYS_USB_CONNECTED_ACK, button_get_data());
        while(1)
        {
            button = button_get_w_tmo(HZ/2);
            if (button == SYS_USB_DISCONNECTED &&
                usb_detect() == USB_EXTRACTED)
                break;
            if (button == SYS_USB_CONNECTED)
                usb_acknowledge(SYS_USB_CONNECTED_ACK, button_get_data());
        }
    }
    /* We don't want the NAND to be re-attached automatically */
    usb_close();
    printf("USB mode exit ");
}
#endif /* HAVE_BOOTLOADER_USB_MODE */

/* ------------------------------------------------ common helpers -- */

static void wait_key(void)
{
    lcd_set_foreground(LCD_RBYELLOW);
    ui_center(FOOT_Y, "press centre / menu to go back");
    lcd_update();
    while (1)
    {
        int b = button_get(true) & ~(BUTTON_REPEAT|BUTTON_REL);
        if (b == BUTTON_SELECT || b == BUTTON_MENU || b == BUTTON_PLAY)
            break;
    }
}

static void screen_message(const char *title, const char *l1,
                           const char *l2, const char *l3)
{
    lcd_clear_display();
    lcd_set_foreground(LCD_WHITE);
    ui_header();
    if (title) ui_center(2*SYSFONT_HEIGHT + 2, title);
    line = 3;   /* printf() rows start below the title bar */
    lcd_set_foreground(LCD_WHITE);
    if (l1) ui_center(5*SYSFONT_HEIGHT +  4, l1);
    if (l2) ui_center(6*SYSFONT_HEIGHT + 10, l2);
    if (l3) ui_center(7*SYSFONT_HEIGHT + 16, l3);
    lcd_update();
}

/* Runs the image at loadbuffer exactly like the stock main() does.
 * Only returns if the launched code returns by itself. */
static int exec_image(void *loadbuffer)
{
    int rc;
    disable_irq();
    int (*kernel_entry)(void) = (void*)loadbuffer;
    commit_discard_idcache();
    rc = kernel_entry();
    enable_irq();
    return rc;
}

/* ------------------------------------------------------ storage ---- */

static bool storage_ready = false;

static int ensure_storage(void)
{
    if (storage_ready)
        return 0;

    int rc = storage_init();
    if (rc != 0)
        return rc;

    filesystem_init();
    /* One attempt, with the device's real 2048-byte sectors -- exactly
     * what the stock bootloader does. Deliberately NO retry with the
     * 4096-byte Apple multiplier: a Rockbox-formatted volume can mount
     * "successfully" with the wrong sector size and then read garbage,
     * which shows up as a bogus "File not found" for rockbox.ipod. */
    rc = disk_mount_all();
    storage_ready = (rc > 0);
    return storage_ready ? 0 : -1;
}

/* Stock behaviour for the unmounted case: advertise Apple's 4096-byte
 * virtual sectors to the USB host before serving it (the stock loader
 * does this in its "No partition found" path). USB path only. */
static void storage_set_usb_sector_size(void)
{
#if defined(DEFAULT_VIRT_SECTOR_SIZE)
    disk_set_sector_multiplier(IF_MD(0,)
                               DEFAULT_VIRT_SECTOR_SIZE/SECTOR_SIZE);
#endif
}

/* ------------------------------------------------------ entries ---- */

static int boot_rockbox(void)
{
    unsigned char *loadbuffer = (unsigned char *)DRAM_ORIG;

    while (1)
    {
        screen_message(BM_RB_NAME, "Loading Rockbox...", NULL, NULL);

        int rc = ensure_storage();
        if (rc != 0)
        {
            printf("No partition found");
#ifdef HAVE_BOOTLOADER_USB_MODE
            /* Self-recovery: nothing mounted. Serve the volume over USB so
             * the files can be put back, then retry without a reboot. */
            storage_set_usb_sector_size();
            lcd_set_foreground(LCD_RBYELLOW);
            ui_center(FOOT_Y, "copy files to the drive, then unplug");
            lcd_update();
            sleep(HZ*2);
            lcd_clear_display();
            line = 0;
            usb_mode();
            storage_ready = false;   /* the USB session unmounted it */
            continue;
#else
            wait_key();
            return rc;
#endif
        }

        rc = load_firmware(loadbuffer, BOOTFILE, MAX_LOADSIZE);
        if (rc > EFILE_EMPTY)
        {
            printf("Rockbox loaded.");
            lcd_update();
            rc = exec_image(loadbuffer);   /* normally never returns */
            printf("firmware returned %d", rc);
            wait_key();
            return rc;
        }

        printf("Can't load " BOOTFILE ":");
        printf(loader_strerror(rc));

#ifdef HAVE_BOOTLOADER_USB_MODE
        /* rockbox.ipod is missing: expose the volume over USB by itself
         * and retry the load as soon as the cable is pulled. This is the
         * ".rockbox gets reinstalled on boot" behaviour -- the folder
         * itself is far too big for NOR, so it comes over USB once and
         * then boots normally forever after. */
        lcd_set_foreground(LCD_RBYELLOW);
        ui_center(FOOT_Y, "copy .rockbox to the drive, then unplug");
        lcd_update();
        sleep(HZ*2);
        lcd_clear_display();
        line = 0;
        usb_mode();
        storage_ready = false;
#else
        wait_key();
        return rc;
#endif
    }
}

static bool confirm_apple(void)
{
    /* Nothing here is recoverable with a re-flash: Apple's firmware,
     * once booted, WRITES to a Rockbox-formatted NAND and erases it
     * (mks5lboot README). Two-step confirmation is deliberate. */
    lcd_clear_display();
    ui_header();
    lcd_set_foreground(LCD_REDORANGE);
    ui_center(2*SYSFONT_HEIGHT +  4, "W A R N I N G");
    lcd_set_foreground(LCD_WHITE);
    ui_center(4*SYSFONT_HEIGHT +  8, "Apple's boot will look for an Apple-");
    ui_center(5*SYSFONT_HEIGHT +  8, "formatted (Whimory) NAND. If this");
    ui_center(6*SYSFONT_HEIGHT +  8, "device is formatted for Rockbox,");
    ui_center(7*SYSFONT_HEIGHT +  8, "booting iPod OS DESTROYS the");
    ui_center(8*SYSFONT_HEIGHT +  8, "Rockbox installation.");
    lcd_set_foreground(LCD_RBYELLOW);
    ui_center(11*SYSFONT_HEIGHT + 10, "CENTRE: I know, boot it");
    ui_center(12*SYSFONT_HEIGHT + 10, "MENU: cancel (safe)");
    lcd_update();
    while (1)
    {
        int b = button_get(true) & ~(BUTTON_REPEAT|BUTTON_REL);
        if (b == BUTTON_SELECT || b == BUTTON_PLAY)
            return true;
        if (b == BUTTON_MENU || b == BUTTON_LEFT)
            return false;
    }
}

static int boot_apple(void)
{
    if (!confirm_apple())
        return 0;
    screen_message(BM_OF_NAME, "Loading Apple's bootloader...", NULL, NULL);
    printf("Executing OF...");
    int rc = kernel_launch_onb();    /* never returns on success */
    char e[40];
    snprintf(e, sizeof(e), "launch failed, rc=%d", rc);
    screen_message(BM_OF_NAME, e, "no valid Apple backup found in NOR",
                   "(--single installs keep no Apple copy)");
    wait_key();
    return rc;
}

static bool looks_like_arm_code(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return false;
    uint8_t hdr[4] = {0};
    int n = read(fd, hdr, 4);
    close(fd);
    if (n < 4)
        return false;
    /* ARM unconditional branch (EA) or LDR pc,[pc,#..] (E59FF..) */
    return hdr[3] == 0xEA ||
           (hdr[3] == 0xE5 && hdr[2] == 0x9F && (hdr[1] & 0x0F) == 0x0F);
}

/* Generic loader used by every path-based entry in boot_items[].
 * Accepts either a raw ARM image (linked for DRAM_ORIG) or a
 * Rockbox-format image, and jumps to it. Returns only if the payload
 * itself returns, or on error. */
static int boot_file(const char *path, const char *label)
{
    unsigned char *loadbuffer = (unsigned char *)DRAM_ORIG;
    char msg[64];

    screen_message(label, path, "loading...", NULL);

    int rc = ensure_storage();
    if (rc != 0)
    {
        screen_message(label, "storage not mounted",
                       "use Bootloader USB mode to fix", NULL);
        wait_key();
        return rc;
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        screen_message(label, "file not found:", path, NULL);
        wait_key();
        return -1;
    }

    long sz = filesize(fd);
    if (sz <= 0 || sz > MAX_LOADSIZE)
    {
        close(fd);
        snprintf(msg, sizeof(msg), "bad size (%ld bytes, max 8MB)", sz);
        screen_message(label, path, msg, NULL);
        wait_key();
        return -1;
    }

    int loaded = -1;
    if (looks_like_arm_code(path))
    {
        /* raw ARM image linked for 0x08000000 */
        if (read(fd, loadbuffer, sz) == sz)
            loaded = (int)sz;
    }
    close(fd);

    if (loaded <= 0)
    {
        /* Rockbox-format (scrambled) image */
        loaded = load_firmware(loadbuffer, path, MAX_LOADSIZE);
    }

    if (loaded <= 0)
    {
        screen_message(label, path, "not a loadable image", NULL);
        wait_key();
        return -1;
    }

    screen_message(label, path, "image loaded, jumping...", NULL);
    rc = exec_image(loadbuffer);   /* normally never returns */

    snprintf(msg, sizeof(msg), "payload returned rc=%d", rc);
    screen_message(label, path, msg, NULL);
    wait_key();
    return rc;
}

static int boot_usb(void)
{
#ifdef HAVE_BOOTLOADER_USB_MODE
    /* Stock sets the virtual sector size before serving USB (SELECT+RIGHT). */
    storage_set_usb_sector_size();
    lcd_clear_display();
    line = 0;
    usb_mode();
    /* host may have touched the disk; force a re-mount next time */
    storage_ready = false;
#else
    screen_message(BM_USB_NAME, "USB mode not built in", NULL, NULL);
    wait_key();
#endif
    return 0;
}

static int boot_poweroff(void)
{
    screen_message(BM_PWR_NAME, "Shutting down...", NULL, NULL);
    sleep(HZ/2);
    power_off();
    return 0;
}

/* -------------------------------------------------------- menu ----- */

struct boot_item {
    const char *name;   /* row label                                     */
    const char *desc;   /* line shown under the list (NULL = none)       */
    const char *path;   /* file to boot; NULL means "use run() instead"  */
    int (*run)(void);   /* special action; ignored when path is set      */
};

/* =================================================================== *
 *                  ADD YOUR OWN BOOT ENTRIES HERE                     *
 *                                                                     *
 *  Every line is one row in the menu, shown top to bottom.            *
 *                                                                     *
 *  A file entry needs nothing but a path:                             *
 *      { "My firmware", "what it does", "/myfw.bin", NULL },          *
 *                                                                     *
 *  Paths are absolute on the iPod's data volume, e.g. "/custom.bin"   *
 *  or "/payloads/test.ipod". Raw ARM images (linked for 0x08000000)   *
 *  and Rockbox-format .ipod images both work -- detected              *
 *  automatically. Max 8 MB.                                           *
 *                                                                     *
 *  Add as many as you like; delete any line to remove that row.       *
 *  Keep BM_DEFAULT_ITEM pointing at the row you want auto-booted      *
 *  (0 = the first line below).                                        *
 * =================================================================== */
static const struct boot_item boot_items[] = {
    /*  name                   description                          path            action      */
    { BM_RB_NAME,  BM_RB_DESC,                                      NULL,           boot_rockbox  },
    { BM_OF_NAME,  BM_OF_DESC,                                      NULL,           boot_apple    },

    { "Custom payload",  "Raw ARM or .ipod image from the volume",  "/custom.bin",  NULL          },
    { "Custom (.ipod)",  "Rockbox-format image from the volume",    "/custom.ipod", NULL          },
    /* { "Test build",   "experimental rockbox build",              "/test.ipod",   NULL          }, */
    /* { "My firmware",  "whatever you built",                      "/myfw.bin",    NULL          }, */

    { BM_USB_NAME, BM_USB_DESC,                                     NULL,           boot_usb      },
    { BM_PWR_NAME, NULL,                                            NULL,           boot_poweroff },
};
#define ITEM_COUNT ((int)ARRAYLEN(boot_items))

/* Runs whichever entry was selected. */
static int boot_item_run(const struct boot_item *it)
{
    if (it->path)
        return boot_file(it->path, it->name);
    if (it->run)
        return it->run();
    return -1;
}

static void menu_draw(int sel, int countdown, int held, int hold_full)
{
    lcd_clear_display();
    ui_header();

    lcd_set_foreground(LCD_RBYELLOW);
    ui_center(4*SYSFONT_HEIGHT + 4, "choose what to boot:");

    for (int i = 0; i < ITEM_COUNT; i++)
    {
        int y = MENU_TOP + i*ITEM_PITCH;
        if (i == sel)
        {
            lcd_set_foreground(LCD_SEL_BG);
            lcd_fillrect(10, y - 4, LCD_WIDTH - 20, ITEM_PITCH - 2);
            lcd_set_foreground(LCD_BLACK);
            lcd_putsxy(ITEM_X - 10, y, (const unsigned char *)">");
        }
        else
        {
            lcd_set_foreground(LCD_WHITE);
        }
        lcd_putsxy(ITEM_X, y, (const unsigned char *)boot_items[i].name);
    }

    lcd_set_foreground(LCD_WHITE);
    if (boot_items[sel].desc)
        ui_center(DESC_Y, boot_items[sel].desc);

    /* Hold-to-boot progress bar: only while the button is actually down,
     * so the normal screen stays uncluttered. */
    if (held > 0 && hold_full > 0)
    {
        const int bw = LCD_WIDTH - 60;
        const int bh = 9;
        const int bx = 30;
        const int by = DESC_Y + SYSFONT_HEIGHT + 4;
        int fill = (bw - 2) * held / hold_full;
        if (fill > bw - 2) fill = bw - 2;

        lcd_set_foreground(LCD_WHITE);
        lcd_drawrect(bx, by, bw, bh);
        lcd_set_foreground(LCD_GREEN);
        if (fill > 0)
            lcd_fillrect(bx + 1, by + 1, fill, bh - 2);
        lcd_set_foreground(LCD_GREEN);
        ui_center(by + bh + 3, "keep holding to boot...");
        lcd_set_foreground(LCD_WHITE);
    }

    ui_footer(countdown);
    lcd_update();
}

static int menu_run(void)
{
    /* Poll period for the hold detector. The wheel still delivers its
     * events through the button queue; a button *hold* is a level, not an
     * event, so it has to be sampled with button_status(). */
    const int TICK      = HZ/20;                 /* 50 ms */
    const int HOLD_FULL = (BM_HOLD_SECS*HZ)/TICK;  /* polls to confirm */

    int sel = BM_DEFAULT_ITEM;
    if (sel < 0 || sel >= ITEM_COUNT) sel = 0;
    long deadline = current_tick + BM_TIMEOUT_SECS*HZ;
    bool timed = (BM_TIMEOUT_SECS > 0);
    int held = 0;

    /* Don't inherit a SELECT that is still down from a previous screen,
     * or the menu would instantly re-trigger. */
    while (button_status() & (BUTTON_SELECT|BUTTON_PLAY))
        sleep(TICK);

    while (1)
    {
        int remaining = -1;
        if (timed)
        {
            long dt = (deadline - current_tick + HZ - 1) / HZ;
            remaining = dt > 0 ? (int)dt : 0;
        }
        menu_draw(sel, remaining, held, HOLD_FULL);

        /* --- level: SELECT/PLAY held down --------------------------- */
        int status = button_status();
        if (status & (BUTTON_SELECT|BUTTON_PLAY))
        {
            held++;
            timed = false;              /* a hold cancels the countdown */
            if (held >= HOLD_FULL)
            {
                /* Wait for release so the chosen entry doesn't see the
                 * button still down (its own screens poll buttons too). */
                while (button_status() & (BUTTON_SELECT|BUTTON_PLAY))
                    sleep(TICK);
                return sel;
            }
        }
        else
        {
            held = 0;                   /* released early -> cancelled */
        }

        /* --- events: wheel / MENU ----------------------------------- */
        int btn = button_get_w_tmo(TICK);
        int b = btn & ~(BUTTON_REPEAT|BUTTON_REL);
        switch (b)
        {
        case BUTTON_SCROLL_FWD:
        case BUTTON_RIGHT:
            sel = (sel + 1) % ITEM_COUNT;
            timed = false;
            held = 0;
            break;
        case BUTTON_SCROLL_BACK:
        case BUTTON_LEFT:
            sel = (sel + ITEM_COUNT - 1) % ITEM_COUNT;
            timed = false;
            held = 0;
            break;
        case BUTTON_MENU:
            return BM_DEFAULT_ITEM < ITEM_COUNT ? BM_DEFAULT_ITEM : 0;
        default:
            break;
        }

        if (timed && TIME_AFTER(current_tick, deadline))
            return sel;
    }
}

/* -------------------------------------------------------- main ----- */

int main(void)
{
    /* Exact stock S5L8702 bring-up order. DRAM is not safe until after
     * memory_init(), so in particular bss_init() MUST NOT move earlier. */
    usec_timer_init();
    i2c_preinit(0);

    if (pmu_is_hibernated())
        launch_onb(1); /* 27 MHz / 2 = 13.5 MHz SPI */

    system_preinit();
    memory_init();
    bss_init();
    system_init();
    kernel_init();
    i2c_init();
    power_init();
    enable_irq();

    button_init();
    lcd_init();
    lcd_set_foreground(LCD_WHITE);
    lcd_set_background(LCD_BLACK);
    lcd_clear_display();
    font_init();
    lcd_setfont(FONT_SYSFIXED);
    lcd_update();
    sleep(HZ/40);
    backlight_init();
    verbose = true;

    /* Fast-path: SELECT+MENU held at power-on -> skip the menu
     * (keeps re-flash / recovery workflows quick). */
    int held = button_read_device();
    if (held == (BUTTON_SELECT|BUTTON_MENU))
    {
        int rc = boot_rockbox();
        (void)rc;
    }

    while (1)
    {
        lcd_clear_display();
        line = 0;
        int item = menu_run();
        boot_item_run(&boot_items[item]);   /* handles its own screens */
    }
}
