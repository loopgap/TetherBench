# Security Policy

TetherBench is an early alpha. The current Gateway is an unauthenticated development prototype and must remain bound to loopback; it is not safe to expose to a LAN or the Internet.

Do not include credentials, private keys, production firmware, or device secrets in issues, commits, test fixtures, or logs. Report suspected vulnerabilities privately to the repository maintainers through a channel listed in the GitHub repository. Do not assume GitHub private vulnerability reporting is enabled until the repository confirms it.

Production device identity, pairing, authorization, transport security, signed firmware, and rollback policy are not implemented by the current prototype. See `docs/architecture/overview.md` for the trust boundaries and `docs/roadmap.md` for the planned security work.
