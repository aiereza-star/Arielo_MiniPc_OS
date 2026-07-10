# Lexbor FULL source component for Arielo MiniPC OS test
# Fuente: TactileBrowser_FULL/tactilebrowser_core/lexbor/source
# Nota: prueba integrada en firmware normal, no ELF externo.

file(GLOB_RECURSE LEXBOR_SOURCES
    "source/lexbor/core/*.c"
    "source/lexbor/css/*.c"
    "source/lexbor/css/syntax/*.c"
    "source/lexbor/dom/*.c"
    "source/lexbor/encoding/*.c"
    "source/lexbor/engine/*.c"
    "source/lexbor/html/*.c"
    "source/lexbor/ns/*.c"
    "source/lexbor/punycode/*.c"
    "source/lexbor/selectors/*.c"
    "source/lexbor/style/*.c"
    "source/lexbor/tag/*.c"
    "source/lexbor/unicode/*.c"
    "source/lexbor/url/*.c"
    "source/lexbor/utils/*.c"
    "source/lexbor/ports/posix/lexbor/core/*.c"
)

# ESP32-S3: fs.c usa dirent/ficheros POSIX y no hace falta para este test.
list(FILTER LEXBOR_SOURCES EXCLUDE REGEX ".*/ports/posix/lexbor/core/fs\\.c$")

idf_component_register(
    SRCS ${LEXBOR_SOURCES}
    INCLUDE_DIRS "source"
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC LEXBOR_STATIC)

# Lexbor tiene bastantes fuentes C de terceros; relajamos warnings para que el
# test no se bloquee por avisos no críticos del repo externo.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-maybe-uninitialized
    -Wno-missing-field-initializers
)

target_link_libraries(${COMPONENT_LIB} PRIVATE m)
