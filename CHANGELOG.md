# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.0.5] - 2022-06-16
### Added
- Add support for function and task
- Add support for fork and join
- Add support for range select
- Add file-related system tasks

### Changed
- slang remote updated
- Sensitivity list extraction for alwasy_comb is improved
- Errors out if subroutine not found
- Switch to reproc

### Fixed
- Fix builtin integer tracking

## [0.0.4] - 2022-03-20
### Added
- Add preliminary DPI support.
- Add preliminary VPI support.
- Add unpacked array support
- Unified versioning in the system

### Changed
- slang remote update
- Changed $finish scheduling
- Rename repo to `fsim` to avoid conflicting names from Xilinx's simulator

## [0.0.3] - 2021-12-31
### Added
- Add more unary operators support
- Add schedule delay based on edge
- Add macOS support

### Fixed
- Fix macro expansion when slice is called
- Fix comb process codegen when the parent doesn't have any

### Changed
- Use raw string value to properly escape display strings
- Namespaces are added to the codegen to avoid naming conflicts
- Linux wheel size optimization
- Event-loop logic rewrite

## [0.0.2] - 2021-12-15
### Fixed
- Add more library search path for glibc

## [0.0.1] - 2021-12-15
### Added
- Add naming conflict detection for system variables
- Basic four-state and two-state logic simulation
- Code generation for processes
- Code generation for instances
- NBA and delay scheduling
- Add incremental builds
