# Crash capture: the scribus-debug gdb launcher, its desktop entry, and the
# log-retention timer. All of it is opt-in — the normal `scribus` launcher is
# untouched, and nothing here runs unless the operator picks "Scribus (Crash
# Capture)" or runs scribus-debug by hand.

# Absolute paths are baked in at configure time because a systemd unit cannot
# expand shell variables and the desktop entry needs a path that resolves
# without relying on the session's PATH.
configure_file(scribus-debug.in                 ${CMAKE_CURRENT_BINARY_DIR}/scribus-debug                 @ONLY)
configure_file(scribus-debug.desktop.in         ${CMAKE_CURRENT_BINARY_DIR}/scribus-debug.desktop         @ONLY)
configure_file(scribus-housekeeping.service.in  ${CMAKE_CURRENT_BINARY_DIR}/scribus-housekeeping.service  @ONLY)
configure_file(postinst.in                      ${CMAKE_CURRENT_BINARY_DIR}/postinst                      @ONLY)
configure_file(postrm.in                        ${CMAKE_CURRENT_BINARY_DIR}/postrm                        @ONLY)

install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/scribus-debug
	DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Kept out of bin/ on purpose: it is called by the systemd unit, not by the
# operator, and PATH should offer one crash-capture command, not two.
install(PROGRAMS housekeeping.sh
	DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/scribus-debug
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/scribus-debug.desktop
	DESTINATION ${DESKTOPDIR}
)

# /usr/local/lib/systemd/user is in systemd's default user unit search path, so
# a prefix install is found without touching /usr.
install(FILES
	${CMAKE_CURRENT_BINARY_DIR}/scribus-housekeeping.service
	scribus-housekeeping.timer
	DESTINATION lib/systemd/user
)

install(FILES RETENTION.md
	DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/scribus-crashcapture
)

# scribus-debug runs the binary through a symlink named "scribus" so that
# argv[0] keeps the preferences profile at ~/.config/scribus. gdb passes the
# path it is handed as argv[0], so "exec -a" is not available to us here.
# Created during install (not as a source file) so it lands in DESTDIR and is
# packaged by CPack like any other file.
install(CODE "
	set(_cclibexec \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBEXECDIR}/scribus-debug\")
	file(MAKE_DIRECTORY \"\${_cclibexec}\")
	# Removed first: file(CREATE_LINK) is an error when the name already
	# exists, which it does on every reinstall over a previous one.
	file(REMOVE \"\${_cclibexec}/scribus\")
	file(CREATE_LINK \"\${CMAKE_INSTALL_PREFIX}/bin/scribus.bin\" \"\${_cclibexec}/scribus\" SYMBOLIC)
")
