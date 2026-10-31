[English](CONTRIBUTING.md) · [Russian](CONTRIBUTING_RU.md)

# Contributing

1. Describe the intended change and acceptance criteria in an issue or pull request.
2. Keep GUI and SDK bridge changes separate; document protocol changes in docs/PROTOCOL.md.
3. Build the GUI without SDK and the backend with a supported SDK; see docs/BUILD.md.
4. Add behavioral tests for states and failures using the fake backend. CTest must
   not require a real camera or credentials.
5. Update CHANGELOG.md, architecture decisions and actual results in docs/TESTING.md.
6. Commit logical changes using gui:, backend:, package:, docs: or test: prefixes.
7. Check DEB installation in a clean Ubuntu image without the source tree or SDK.

Use C++17, Qt 6, two-space indentation, snake_case and RAII. English is the source
language; keep Russian translations in the `_RU` files and catalog.

Build directories stay outside Git. Do not commit Sony SDK, runtime libraries,
credentials, settings or camera photographs as fixtures. A stable public release
requires hardware acceptance testing: connection, capture, Live View, RAW+JPEG,
network interruption and reconnection.
