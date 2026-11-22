# Contributing to WatchTop

Thank you for your interest in contributing to WatchTop!

Contributions of all kinds are welcome, including bug fixes, improvements to process scanning, inotify monitoring, the terminal dashboard, testing, and documentation.

## Getting Started

Before making changes:

1. Fork the repository.
2. Clone your fork.
3. Create a branch for your changes.

```bash
git clone https://github.com/YOUR_USERNAME/watchtop.git
cd watchtop
git checkout -b feature/your-change
```

## Development Requirements

WatchTop uses:

- C++20
- CMake
- FTXUI
- Linux inotify and `/proc` interfaces

Make sure you have the required compiler and build dependencies installed.

## Building

Configure and build the project:

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
```

## Testing

Run the test suite:

```bash
python3 tests/run_tests.py
```

Please make sure your changes do not introduce new test failures.

For changes involving threads, memory management, or low-level system operations, run the relevant sanitizer checks when possible.

## Code Guidelines

- Follow the existing C++ style and project structure.
- Keep changes focused and avoid unrelated modifications.
- Handle errors and unexpected system states carefully.
- Avoid introducing unnecessary dependencies.
- Add or update tests when changing existing behavior.

## Pull Requests

When submitting a pull request:

- Explain the problem your change addresses.
- Describe what you changed.
- Include the tests you performed.
- Reference the related issue, if applicable.

Keep pull requests focused and easy to review.

## Issues

Use [GitHub Issues](https://github.com/Abdev314/watchtop/issues) to report bugs or suggest features.

For questions, feedback, and early ideas, use [GitHub Discussions](https://github.com/Abdev314/watchtop/discussions).

Thank you for helping improve WatchTop!