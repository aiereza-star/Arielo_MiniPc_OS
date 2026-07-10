# Register the 'main' component with the build system.
idf_component_register(
    # --- Source Files ---
    SRCS
        "main.c"
        "minipc_sd_02b.c"
        "minipc_wifi_02c.c"
        "my_console_io.c"
        "cmd_testgfx.c"
        "cmd_desktop_07A_GUI_SHELL.c"
        "minipc_usbsel_02d.c"
        "usb_enum_debug_02d.c"
        "minipc_bootpixel_03d.c"
        "usb_hid_keyboard_02d.c"
        "minipc_usb_msc_02d.c"
        "minipc_i2cscan.c"
        "minipc_touch_gt911.c"
        "minipc_browser.c"
        "minipc_ntp.c"
        "cmd_tbtest_lexbor_source.c"
        "cmd_tbrender_lexbor_10aa.c"
        "cmd_tbview_lexbor_10ab.c"
        "cmd_tbbrowser_lexbor_10ac.c"
        "cmd_tbbrowser_lexbor_10ad.c"
        "cmd_tbbrowser_lexbor_10ae.c"
        "cmd_tbbrowser_gui_10ah.c"

    # --- Dependencies ---
    PRIV_REQUIRES
        spi_flash
        driver
        fatfs
        esp_lcd
        breezybox
        breezy_bt
        breezy_rgb_lcd
        heap
        esp_hid
        bt
        nvs_flash
        hal
        esp_driver_usb_serial_jtag
        fatfs
        sdmmc
        esp_driver_spi
        esp_driver_i2c
        esp_wifi
        esp_netif
        esp_http_client
        esp-tls
        lwip
        esp_event
        nvs_flash
        esp_driver_uart
        vfs
        usb
        usb_host_hid
        usb_host_msc
        lexbor_full



    # --- Include Directories ---
    INCLUDE_DIRS
        "."
)
