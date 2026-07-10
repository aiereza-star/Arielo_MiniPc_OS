Arielo MiniPC OS
ESP32-S3 Waveshare 7" Mini Computer
Autor: Ariel (EA7JTP)
________________________________________
Introducción
Arielo MiniPC OS es un sistema operativo ligero para la placa Waveshare ESP32-S3 Touch LCD 7", concebido para demostrar hasta dónde puede llegar un ESP32-S3 cuando se diseña una arquitectura modular, eficiente y respetuosa con los recursos del hardware.
El objetivo nunca fue construir una simple demostración gráfica, sino un pequeño ordenador funcional con aplicaciones independientes, almacenamiento externo, USB Host, red, navegador web y una interfaz gráfica propia.
Todo ello ejecutándose íntegramente sobre un ESP32-S3.
________________________________________
Filosofía del proyecto
Durante el desarrollo se tomó una decisión que marcó completamente la arquitectura del sistema:
El sistema operativo debía permanecer pequeño, robusto y estable.
Las nuevas funcionalidades no debían integrarse dentro del firmware principal, sino ejecutarse como aplicaciones ELF independientes cargadas desde:
•	memoria interna (/root/bin) 
•	tarjeta microSD 
•	memoria USB 
Gracias a esta filosofía:
•	cada aplicación evoluciona independientemente; 
•	el firmware principal apenas necesita modificaciones; 
•	los riesgos de introducir regresiones son mínimos; 
•	el sistema permanece ligero y fácilmente ampliable. 
________________________________________
Hardware
Plataforma principal
•	Waveshare ESP32-S3 Touch LCD 7" 
•	ESP32-S3 Dual Core 240 MHz 
•	16 MB Flash 
•	8 MB PSRAM 
•	Pantalla 800x480 RGB 
•	Touch GT911 
________________________________________
Almacenamiento
•	LittleFS interno 
•	microSD 
•	USB Mass Storage Hot-Plug 
________________________________________
USB Host
Soporte completo para:
•	HUB USB 
•	teclado USB HID 
•	ratón USB HID 
•	memorias USB 
con conexión y desconexión en caliente.
________________________________________
Características principales
Escritorio gráfico
•	interfaz propia 
•	iconos 
•	ventanas 
•	cursor USB 
•	soporte táctil 
________________________________________
Gestor de archivos
•	navegación por: 
•	LittleFS 
•	microSD 
•	USB 
Funciones:
•	copiar 
•	mover 
•	borrar 
•	crear carpetas 
•	comprimir TAR.GZ 
•	descomprimir 
•	progreso de copia 
________________________________________
Aplicaciones ELF
Arquitectura modular.
Aplicaciones actuales:
•	Calculator 
•	Notepad 
•	Sheet 
•	Mini Paint 
•	Image Viewer 
•	Log Viewer 
•	SYSINFO 
•	Pong 
•	Snake 
•	Tetris 
•	Breakout 
•	TactileBrowser 
Todas ejecutándose como programas independientes.
________________________________________
TactileBrowser
Navegador web ligero basado en Lexbor.
Funciones:
•	HTTP 
•	HTTPS 
•	render HTML 
•	navegación táctil 
•	navegación mediante teclado 
•	historial 
•	favoritos 
•	scroll 
•	enlaces 
•	formularios GET 
•	formularios POST 
•	cookies 
•	sesiones 
•	GeoIP 
•	soporte UTF-8 
•	teclado español completo 
•	render Style Lite 
•	arquitectura RAM-safe 
________________________________________
Style Lite
En lugar de implementar CSS completo, TactileBrowser interpreta únicamente los estilos más importantes:
•	color 
•	font-weight 
•	<b> 
•	<strong> 
•	<font color> 
Esto proporciona una mejora visual muy importante con un consumo mínimo de memoria.
________________________________________
Arquitectura RAM-safe
Una de las características más importantes del proyecto.
Todos los buffers grandes se reservan únicamente durante su utilización.
Posteriormente son liberados.
Esto permitió la convivencia estable de:
•	USB Host 
•	HUB 
•	HID 
•	MSC 
•	WiFi 
•	Navegador 
sin provocar errores ESP_ERR_NO_MEM.
________________________________________
Conectividad
•	WiFi STA 
•	HTTP Server 
•	NTP 
•	HTTPS 
•	USB Host 
________________________________________
Sistema de archivos
LittleFS
    │
    ├── /root
    ├── /root/bin
    ├── configuración
    └── navegador

microSD

USB
________________________________________
Filosofía de desarrollo
Durante todo el proyecto se siguió una regla muy estricta:
Cada versión únicamente se consideraba válida cuando demostraba ser completamente estable.
Una vez validada:
•	se guardaba una copia 
•	se colocaba un "candado" 
•	nunca volvía a modificarse 
Las nuevas mejoras siempre partían de una base estable.
Este método permitió desarrollar el sistema sin perder nunca funcionalidades previamente conquistadas.
________________________________________
Objetivo alcanzado
Arielo MiniPC OS demuestra que un ESP32-S3 puede comportarse como un pequeño ordenador multitarea capaz de ejecutar aplicaciones independientes, gestionar almacenamiento externo, utilizar dispositivos USB Host y navegar por Internet mediante un navegador HTML propio.
Todo ello respetando las limitaciones del hardware y manteniendo una arquitectura ligera, modular y fácilmente ampliable.
________________________________________
Créditos
Proyecto desarrollado por
Ariel (EA7JTP)
Con la asistencia técnica de (OpenAI) durante el diseño, depuración, optimización y documentación del proyecto.
________________________________________
Licencia
Proyecto de carácter personal y experimental.
Desarrollado con fines educativos, de investigación y aprendizaje.
________________________________________
