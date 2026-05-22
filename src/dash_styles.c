// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2022 Ryzee119

#include "lithiumx.h"

lv_style_t dash_background_style;
lv_style_t menu_table_style;
lv_style_t menu_table_highlight_style;
lv_style_t menu_table_cell_style;
lv_style_t object_style;
lv_style_t titleview_style;
lv_style_t titleview_image_container_style;
lv_style_t titleview_image_text_style;
lv_style_t titleview_header_footer_style;
lv_color_t dash_base_theme_color;

void dash_styles_init(lv_color_t theme_colour)
{
    dash_base_theme_color = theme_colour;

    // Bepaal de achtergrondkleur: aangepaste kleur heeft voorrang boven de
    // theme gradient. background_colour == -1 betekent "niet ingesteld".
    bool has_custom_bg = DASH_BG_COLOUR_IS_SET(dash_settings.background_colour);
    lv_color_t bg_colour = has_custom_bg
        ? lv_color_make(DASH_BG_COLOUR_R(dash_settings.background_colour),
                        DASH_BG_COLOUR_G(dash_settings.background_colour),
                        DASH_BG_COLOUR_B(dash_settings.background_colour))
        : lv_color_make(15, 15, 15);

    // Set the style for the background
    lv_style_init(&dash_background_style);
    lv_style_set_border_width(&dash_background_style, 0);
    lv_style_set_radius(&dash_background_style, 0);
    lv_style_set_bg_color(&dash_background_style, bg_colour);
    if (has_custom_bg)
    {
        // Effen kleur: geen gradient
        lv_style_set_bg_grad_dir(&dash_background_style, LV_GRAD_DIR_NONE);
    }
    else
    {
        // Standaard gradient van donkergrijs naar theme colour
        lv_style_set_bg_grad_color(&dash_background_style, theme_colour);
        lv_style_set_bg_grad_dir(&dash_background_style, LV_GRAD_DIR_VER);
    }

    // Set the style for the main menu container
    lv_style_init(&menu_table_style);
    lv_style_set_bg_color(&menu_table_style, lv_color_make(15, 15, 15));
    lv_style_set_bg_opa(&menu_table_style, 240);
    lv_style_set_border_width(&menu_table_style, 1);
    lv_style_set_border_color(&menu_table_style, lv_color_make(255, 255, 255));
    lv_style_set_pad_all(&menu_table_style, 0);
    lv_style_set_radius(&menu_table_style, 0);
    lv_style_set_text_color(&menu_table_style, lv_color_white());
    lv_style_set_text_font(&menu_table_style, &lv_font_montserrat_20);
    lv_style_set_outline_width(&menu_table_style, 0);
    lv_style_set_text_line_space(&menu_table_style, 10);

    // Set the style for the main menu item cells.
    lv_style_init(&menu_table_cell_style);
    lv_style_set_border_width(&menu_table_cell_style, 1);
    lv_style_set_border_color(&menu_table_cell_style, lv_color_make(255, 255, 255));
    lv_style_set_bg_opa(&menu_table_cell_style, 0);
    lv_style_set_text_color(&menu_table_cell_style, lv_color_white());
    lv_style_set_text_font(&menu_table_cell_style, &lv_font_montserrat_20);
    lv_style_set_pad_top(&menu_table_cell_style, 10);
    lv_style_set_pad_bottom(&menu_table_cell_style, 10);
    lv_style_set_radius(&menu_table_cell_style, 0);
    lv_style_set_outline_width(&menu_table_cell_style, 0);

    // Set the style for the main menu container
    lv_style_init(&object_style);
    lv_style_set_bg_color(&object_style, lv_color_make(15, 15, 15));
    lv_style_set_bg_opa(&object_style, 240);
    lv_style_set_border_width(&object_style, 0);
    lv_style_set_pad_all(&object_style, 0);
    lv_style_set_radius(&object_style, 0);
    lv_style_set_text_color(&object_style, lv_color_white());
    lv_style_set_text_font(&object_style, &lv_font_montserrat_20);
    lv_style_set_outline_width(&object_style, 0);
    lv_style_set_text_line_space(&object_style, 10);

    // Set the style for the main menu item cells when they are selected
    lv_style_init(&menu_table_highlight_style);
    lv_style_set_bg_color(&menu_table_highlight_style, theme_colour);

    // Create a style for the container that holds all the images
    // Set padding between images, background colour behind the images etc
    lv_style_init(&titleview_style);
    lv_style_set_radius(&titleview_style, 0);
    lv_style_set_border_width(&titleview_style, 0);
    lv_style_set_bg_color(&titleview_style, lv_color_make(34, 34, 34));
    lv_style_set_pad_all(&titleview_style, 0);
    lv_style_set_pad_row(&titleview_style, 0);
    lv_style_set_pad_column(&titleview_style, 0);

    // Create a style for the image containers that contain an image. We basically want no borders or padding
    // as we want this container to be invisible
    lv_style_init(&titleview_image_container_style);
    lv_style_set_radius(&titleview_image_container_style, 0);
    lv_style_set_bg_color(&titleview_image_container_style, lv_color_make(34, 34, 34));
    lv_style_set_border_color(&titleview_image_container_style, lv_color_darken(theme_colour, 100));
    lv_style_set_pad_all(&titleview_image_container_style, 0);
    lv_style_set_border_width(&titleview_image_container_style, 1);

    // Create a style for the text that appears on the thumbnail art when no artwork is found
    lv_style_init(&titleview_image_text_style);
    lv_style_set_align(&titleview_image_text_style, LV_ALIGN_CENTER);
    lv_style_set_text_font(&titleview_image_text_style, &lv_font_montserrat_20);
    lv_style_set_text_color(&titleview_image_text_style, lv_color_white());
    lv_style_set_text_align(&titleview_image_text_style, LV_TEXT_ALIGN_CENTER);

    // Create a style for the page header and footer text
    lv_style_init(&titleview_header_footer_style);
    lv_style_set_bg_color(&titleview_header_footer_style, theme_colour);
    lv_style_set_text_color(&titleview_header_footer_style, lv_color_white());
    lv_style_set_text_font(&titleview_header_footer_style, &lv_font_montserrat_26);

    lv_obj_mark_layout_as_dirty(lv_scr_act());
}

void dash_styles_deinit(void)
{
    lv_style_reset(&dash_background_style);
    lv_style_reset(&menu_table_style);
    lv_style_reset(&menu_table_highlight_style);
    lv_style_reset(&menu_table_cell_style);
    lv_style_reset(&object_style);
    lv_style_reset(&titleview_style);
    lv_style_reset(&titleview_image_container_style);
    lv_style_reset(&titleview_image_text_style);
    lv_style_reset(&titleview_header_footer_style);
}
