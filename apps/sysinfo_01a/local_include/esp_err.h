#pragma once
/*
 * Stub minimo de esp_err.h para compilar apps ELF standalone (fuera del
 * arbol de ESP-IDF). Solo se necesita el tipo esp_err_t para que
 * bt_keyboard.h compile; las funciones que lo usan no se llaman desde
 * snake.c (solo usamos bt_keyboard_is_pressed / bt_keyboard_get_modifiers).
 */
typedef int esp_err_t;
