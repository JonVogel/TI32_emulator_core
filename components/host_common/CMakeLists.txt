# host_common — shared TI-99/4A host layer between the interpreter
# and per-board main.cpp files. See include/ti_host.h for the API.
#
# This component is header-only for the moment (contains only the
# TiHostConfig / TiDisplay interface). Implementation files land here
# as we move code out of the box + sunton main.cpp — line editor,
# tiPrint*, boot screen, sprite core, processInput. Kept in one
# component (rather than several fine-grained ones) so the dependency
# graph stays flat: interpreter -> host_common; per-host main -> both.
idf_component_register(
    SRCS
        "src/ti_host_color.cpp"
        "src/ti_host_logo.cpp"
        "src/ti_host_screen.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        # interpreter provides tiPrintChar/tiSetChar/etc. weak hooks
        # via ti_platform.h that host_common will override with strong
        # implementations backed by TiDisplay.
        interpreter
        # font provides the 8x8 ROM font used by the (future) drawCell
        # implementation living in host_common.
        font
        # arduino gives us Serial (for the paste-buffer editor path),
        # millis(), delay(), etc. — same layer both hosts already use.
        arduino
)
