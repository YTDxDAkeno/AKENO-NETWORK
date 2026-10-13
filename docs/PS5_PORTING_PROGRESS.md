# PS5 client port: HTTP backend separation

This branch is a deliberately limited step towards a native PS5 payload, **not a PS5 build**.

The existing Linux C11 client handled HTTP framing, socket connections and
response parsing in `src/client.c`. Now `src/client.c` exclusively handles
API arguments, authentication, JSON parsing and error mapping, while
`src/httpclient_posix.c` owns the request construction and POSIX/OpenSSL
exchange behind `src/httpclient.h`.

This preserves the current Linux transport and allows a future PS5 backend
using **verified**, SDK-supported SCE HTTP/TLS calls. It also prevents
untrusted callers from supplying arbitrary HTTP method tokens.

## What is verified

- Existing C client CI exercises the POSIX backend and device pairing.
- Console compilation, console TLS verification and PS5 hardware operation
  are **not tested** on this branch.
- Realtime WebSockets, credential storage and time/random primitives still
  depend on the Linux platform; this HTTP refactor alone does not port them.
- Native Sony Game Base integration remains unverified and unsupported.

## Next engineering steps

1. Review exact PS5 SDK declarations and linking requirements at a pinned revision.
2. Separate credential storage and platform initialization into dedicated adapters.
3. Implement a separate PS5 HTTP backend with trusted certificate **and host**
   verification; fail closed when unavailable.
4. Build an ELF with a pinned SDK toolchain and publish its checksum.
5. Test on real hardware **only after explicit approval**, recording firmware,
   loader and kstuff versions.
