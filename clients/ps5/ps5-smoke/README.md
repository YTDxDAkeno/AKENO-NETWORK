# AKENO PS5 diagnostic payload — NOT the client

This deliberately small ELF validates that a **known PS5 payload toolchain**
can build a loader-compatible binary. If manually launched on a compatible,
already-jailbroken PS5, it requests one native system notification and exits.

**It does not connect to AKENO NETWORK, perform pairing, display a controller
UI, hook the Game Base, or modify PSN or the system software.**

The call and its structure are based on the documented
[`ps5-payload-sdk` hello_world sample](https://github.com/ps5-payload-dev/sdk/blob/f5d40f02770e9b0001dd9e18056eca70b3da8b40/samples/hello_world/main.c).

## Reproducible build

The GitHub Actions workflow `.github/workflows/ps5-smoke.yml` installs a
**pinned** SDK at commit `f5d40f02770e9b0001dd9e18056eca70b3da8b40`
and builds the ELF using `prospero.mk`. The resulting Actions artifact includes
the ELF and its SHA-256 checksum. The ELF is not shipped through production
AKENO NETWORK releases.

For manual build on a supported Linux host:

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
make -C clients/ps5/ps5-smoke
sha256sum clients/ps5/ps5-smoke/akeno-ps5-diagnostic.elf
```

## Safety and limitations

- **Do not run this payload without explicit approval**, even though its
  functionality is intentionally small; unstable loaders or firmware can crash.
- No real-hardware test on firmware 12.20 or 13.60 has been performed.
- Compilation and linkage demonstrate a toolchain result, not firmware support.
- There is no automatic console deployment or exploit in this project.
- A complete PS5 client still requires verified HTTPS/TLS behavior, secure
  storage, UI/controller support, firmware testing, and reliable server access.
- SDK source and components have their own licenses; review applicable terms
  before redistributing combined binaries.

This is an engineering bring-up checkpoint, not an AKENO NETWORK feature release.
