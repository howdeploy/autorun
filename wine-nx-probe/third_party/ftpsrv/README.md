# ftpsrv

Selected MIT-licensed library files from
<https://github.com/ITotalJustice/ftpsrv>, commit
`99253bdd62fac99f251f1bf25043afdf3f4b38e7` (version 1.2.2).
Copyright 2024 TotalJustice. Original source headers are preserved.

Autorun supplies its own VFS rooted at `switch/wine`, builds at most eight
sessions and uses the POSIX socket adapter over libnx. Local changes to
`ftpsrv.c` retain the control connection's peer address, restrict active data
connections to that peer on an unprivileged port, and reject passive data
connections from a different peer.

The frontend requires a named account and never installs a command logger,
so FTP passwords are not copied into runtime diagnostic logs.
