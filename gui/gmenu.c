/*  Copyright (c) 2015 James Laird-Wah
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, you can obtain one at http://mozilla.org/MPL/2.0/. */

#include <iapetus.h>
#include <string.h>
#include <stdio.h>
#include "satiator.h"
#include "gmenu.h"

static font_struct main_font;
static char *version;
int asprintf(char **strp, const char *fmt, ...);

static int update_accel(int *accel) {
    int v = (*accel)++;
    if (!v)
        return 1;

    v -= 30;

    if (v < 0)
        return 0;

    if (v > 0x20 && (v & 1) == 0)
        return 1;

    if ((v & 0x3) == 0)
        return 1;

    return 0;
}

struct {
    int acc_l, acc_r, acc_u, acc_d;
} pad_state;


static int update_button_bit(int bit, int *accel) {
    int pressed = 0;

    if (per[0].but_push & bit)
        pressed = update_accel(accel);
    else
        *accel = 0;

    if (pressed)
        return bit;
    return 0;
}

static int pad_poll_buttons(void) {
    int out = 0;

    out |= update_button_bit(PAD_UP, &pad_state.acc_u);
    out |= update_button_bit(PAD_DOWN, &pad_state.acc_d);
    out |= update_button_bit(PAD_L, &pad_state.acc_l);
    out |= update_button_bit(PAD_R, &pad_state.acc_r);

    return out;
}

void menu_init(void) {
    screen_settings_struct settings;
    // Setup a screen for us draw on
    settings.is_bitmap = TRUE;
    settings.bitmap_size = BG_BITMAP512x256;
    settings.transparent_bit = 0;
    settings.color = BG_256COLOR;
    settings.special_priority = 0;
    settings.special_color_calc = 0;
    settings.extra_palette_num = 0;
    settings.map_offset = 0;
    settings.rotation_mode = 0;
    settings.parameter_addr = 0x25E60000;
    vdp_rbg0_init(&settings);

    // Use the default palette
    vdp_set_default_palette();

    // Setup the default 8x16 1BPP font
    main_font.data = font_8x8;
    main_font.width = 8;
    main_font.height = 8;
    main_font.bpp = 1;
    main_font.out = (u8 *)0x25E00000;
    vdp_set_font(SCREEN_RBG0, &main_font, 1);

    // Display everything
    vdp_disp_on();

    char fw_version[32];
    s_get_fw_version(fw_version, sizeof(fw_version));
    char *space = strchr(fw_version, ' ');
    if (space)
        *space = '\0';

    asprintf(&version, "BETA FW%s MNU%s", fw_version, VERSION);
    space = strchr(version, ' ');
    if (space)
        space = strchr(space+1, ' ');
    if (space)
        space = strchr(space+1, ' ');
    if (space)
        *space = '\0';
}

#define SCROLL_OFF 3

int menu_picklist(file_ent *entries, int n_entries, char *caption, font_struct *font) {
    int width, height;
    int old_transparent;

    if (!font)
        font = &main_font;

    vdp_get_scr_width_height(&width, &height);

    gui_window_init();

    old_transparent = font->transparent;
    font->transparent = 1;

    int selected = 0;
    int scrollbase = 0;
    int draw_base_y = 32;   // ?
    int n_rows = (height - draw_base_y - 32) / font->height;
    int n_cols = (width - 64) / font->width;

    gui_window_draw(8, 8, width-16, height-16, TRUE, 0, RGB16(26, 26, 25) | 0x8000);
    for(;;) {
        vdp_clear_screen(font);
        char namebuf[256];
        s_getcwd(namebuf, sizeof(namebuf));
        vdp_print_text(font, 8+6, 8+4, 0xf, caption);
        vdp_print_text(font, 8+8, height-8, 0xf, version);
        // draw some entries
        int i;
        for (i=0; i<n_rows; i++) {
            int entry = i + scrollbase;
            if (entry >= n_entries)
                break;
            if (entry == selected)
                vdp_print_text(font, 16, draw_base_y + font->height*i, 0x10, ">");

            // truncate name so as not to overrun screen
            char namebuf[n_cols];
            strncpy(namebuf, entries[entry].name, sizeof(namebuf));
            vdp_print_text(font, 32, draw_base_y + font->height*i, 0x10, namebuf);
        }
        if (scrollbase > 0)
            vdp_print_text(font, width*3/4, draw_base_y-4, 0x10, "^");
        if (scrollbase + n_rows < n_entries)
            vdp_print_text(font, width*3/4, draw_base_y+font->height*i-4, 0x10, "v");

        // wait for input
        for(;;) {
            vdp_vsync();
            int buttons = pad_poll_buttons();
            if (buttons & PAD_UP) {
                selected--;
                goto move;
            }
            if (buttons & PAD_DOWN) {
                selected++;
                goto move;
            }
            if (per[0].but_push_once & PAD_A) {
                goto out;
            }
            if (per[0].but_push_once & PAD_B) {
                selected = -1;
                goto out;
            }
            if (buttons & PAD_L) {
                selected -= 20;
                goto move;
            }
            if (buttons & PAD_R) {
                selected += 20;
                goto move;
            }
        };
move:
        if (selected >= n_entries)
            selected = n_entries - 1;
        if (selected < 0)
            selected = 0;
        while (selected - scrollbase < SCROLL_OFF && scrollbase > 0)
            scrollbase--;
        while (scrollbase + n_rows - selected < SCROLL_OFF && scrollbase < n_entries - n_rows)
            scrollbase++;
    }

out:
    font->transparent = old_transparent;
    return selected;
}

void menu_error(const char *title, const char *message) {
    int width, height;
    vdp_get_scr_width_height(&width, &height);
    vdp_clear_screen(&main_font);
    gui_window_draw(8*3, 8*5, width-8*5, height-8*7, TRUE, 0, RGB16(26, 26, 25) | 0x8000);
    vdp_print_text(&main_font, 8*3+6, 8*5+4, 0xf, title);
    vdp_print_text(&main_font, 8*3+14, 8*5+20, 0x10, message);

    for (;;) {
        vdp_vsync();
        if (per[0].but_push_once)
            break;
    }
}
