# Security Policy

## Our Threat Model

WebZero is designed for serving **static content** on private/embedded networks.
It is **not** designed to be a general-purpose internet-facing server handling
untrusted dynamic input at scale.

Known non-goals:
- TLS (no OpenSSL dependency — use a reverse proxy like nginx for TLS termination)
- Authentication / authorization
- Protection against sophisticated DoS (rate limiting is a platform layer concern)

## Reporting Vulnerabilities

Please report security issues privately via GitHub's "Private vulnerability reporting" feature.

Do **not** open a public issue for security vulnerabilities.

We will respond within 7 days and issue a fix within 30 days for critical issues.

## What Counts as a Vulnerability

- Memory safety issues in the C server (buffer overflows, use-after-free)
- Bundle parser issues that allow arbitrary code execution on the host
- VM interpreter issues allowing escape from the bytecode sandbox
- Path traversal allowing access to files outside the bundle

## What Does Not Count

- Denial of service via connection storms (the server intentionally sheds with 503)
- Lack of TLS (by design — use a reverse proxy)
- Missing HTTP/2 or HTTP/3 support (planned stretch goals)
