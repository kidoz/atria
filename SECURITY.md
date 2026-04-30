# Security policy

Atria parses untrusted network input. Security issues are taken seriously.

## Supported versions

Atria is pre-1.0; only the `main` branch is supported for security fixes.

## Reporting a vulnerability

If you believe you have found a security vulnerability, please **do not** open a public GitHub issue. Instead, use GitHub's [private security advisory](https://github.com/atria-web/atria/security/advisories/new) feature, or contact the maintainers privately.

When reporting, please include:

- a description of the issue,
- a reproduction (request bytes, code snippet, or PoC),
- the impact you observe,
- the affected commit SHA.

## Scope

In scope:

- HTTP request parsing
- routing and path-parameter handling
- JSON parser and serializer
- response serialization
- middleware chain
- platform socket layer

Out of scope (until addressed in roadmap):

- TLS termination
- HTTP/2 or HTTP/3
- Chunked transfer encoding

## Disclosure timeline

We aim to acknowledge reports within 7 days and to ship a fix within 30 days for high-severity issues.
