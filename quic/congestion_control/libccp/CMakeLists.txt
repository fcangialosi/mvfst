
# add obj files
add_library(
    libccp STATIC
    ccp.c
    ccp_priv.c
    machine.c
    serialize.c
)

install(
  TARGETS libccp
  EXPORT mvfst-exports
  DESTINATION lib
)
