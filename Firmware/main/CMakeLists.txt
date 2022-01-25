idf_component_register(SRCS "main.c" "crypto/ed25519-donna/ed25519.c" "crypto/sha2.c" "crypto/memzero.c"
                    INCLUDE_DIRS "." "crypto" "crypto/ed25519-donna")
