# Contributing

Carve is a clean-slate rewrite under [Apache-2.0](LICENSE). Contributions are
welcome.

## Ground rules

- Be respectful. The [Code of Conduct](CODE_OF_CONDUCT.md) applies.
- No code copied from other projects unless their license is compatible and the
  attribution is preserved.

## Sign-off

All commits must carry a [Developer Certificate of Origin](https://developercertificate.org/)
sign-off:

```bash
git commit -s -m "your message"
```

This adds `Signed-off-by: Your Name <email>` to the commit and confirms that
you have the right to contribute the change under Apache-2.0.

## Code style

Code-style and structural rules live in [RULES.md](RULES.md). C++ follows the
[Google style guide](https://google.github.io/styleguide/cppguide.html) with
deviations encoded in [.clang-format](.clang-format) and
[.clang-tidy](.clang-tidy). Build with `--config=clang` to use the bundled
toolchain.

## Pre-commit

Install the local quality gates once, then they run on every commit:

```bash
pre-commit install
pre-commit run -a   # run against the whole tree
```

## Tests

```bash
bazel test //...
```

For sanitizer runs:

```bash
bazel test //... --config=asan
bazel test //... --config=tsan
bazel test //... --config=ubsan
```
