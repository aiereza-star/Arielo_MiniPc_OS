# 10AL - TactileBrowser NET HTTP + NAVBAR

Base: `10AK_OK_TBROWSER_GUI_VISUAL_RENDER_POLISH`.

Primer escalon de red:

- HTTP GET basico con `esp_http_client`.
- Boton tactil `URL` en la barra inferior.
- Tecla `u` para escribir URL/ruta.
- Links `http://...` navegables.
- Resolucion basica de enlaces relativos en paginas HTTP.
- Error HTML interno si no hay WiFi, falla HTTP o se intenta HTTPS.

Limitaciones intencionadas:

- HTTPS queda para un escalon posterior.
- Sin CSS, imagenes, JS ni formularios.
- Descarga maxima HTTP: 64 KiB.

Pruebas:

```text
tbbgui net
tbbgui url http://neverssl.com/
tbbgui http://neverssl.com/
```

Desde escritorio: WEB -> boton `URL` o tecla `u`.
