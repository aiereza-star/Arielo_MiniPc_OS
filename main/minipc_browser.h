#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Estado del mini-navegador
typedef enum {
    BROWSER_IDLE = 0,     // esperando URL
    BROWSER_LOADING,      // descargando
    BROWSER_RENDERING,    // parseando
    BROWSER_DONE,         // contenido listo para mostrar
    BROWSER_ERROR,        // fallo (ver browser_status_text)
} browser_state_t;

// Inicializa el navegador (buffers). Llamar una vez.
void minipc_browser_init(void);

// Lanza la descarga + parseo de 'url'. Bloqueante (corre la descarga).
// Al volver, el estado queda en BROWSER_DONE o BROWSER_ERROR.
void minipc_browser_load(const char *url);

// Pone estado LOADING + mensaje, para pintar ANTES de la descarga bloqueante.
void minipc_browser_set_loading(const char *url);

browser_state_t minipc_browser_state(void);

// Texto de estado legible (para la barra: "Descargando...", "Error: ...", etc.)
const char *minipc_browser_status_text(void);

// Acceso al texto ya parseado (limpio, sin tags). Lineas separadas por '\n'.
const char *minipc_browser_text(void);

// Numero total de lineas del texto parseado (para el scroll).
int minipc_browser_line_count(void);

// Devuelve el puntero al inicio de la linea 'n' y su longitud (sin '\n').
// Para que el visor pinte linea a linea con scroll.
const char *minipc_browser_get_line(int n, int *out_len);

#ifdef __cplusplus
}
#endif


// 09B: API opcional para una futura pantalla LINKS.
// El visor actual puede ignorarla.
int minipc_browser_link_count(void);
const char *minipc_browser_get_link_url(int n);
const char *minipc_browser_get_link_text(int n);

// Formularios GET simples 10D
int minipc_browser_form_available(void);
const char *minipc_browser_form_action(void);
const char *minipc_browser_form_input_name(void);


// 09C: API opcional de navegacion.
// El visor actual puede conectarla a botones HOME/RELOAD/BACK/FORWARD.
const char *minipc_browser_current_url(void);
const char *minipc_browser_home_url(void);
void minipc_browser_set_home(const char *url);
void minipc_browser_home(void);
void minipc_browser_reload(void);
int minipc_browser_can_back(void);
int minipc_browser_can_forward(void);
void minipc_browser_back(void);
void minipc_browser_forward(void);
int minipc_browser_history_count(void);
int minipc_browser_history_index(void);
const char *minipc_browser_history_url(int n);

// Salta directamente a la posicion n del historial (para el listado HIST).
int minipc_browser_history_goto(int n);

// ---------------- Favoritos ----------------
// Anade la URL actual a favoritos. Devuelve: 0=OK, 1=ya estaba, -1=lista llena.
int minipc_browser_fav_add(const char *url);
int minipc_browser_fav_remove(int n);
int minipc_browser_fav_count(void);
const char *minipc_browser_fav_url(int n);
int minipc_browser_fav_is_current(void);
