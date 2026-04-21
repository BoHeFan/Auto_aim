# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2024-01-01

### Added
- Modular project structure with separate components
- DXGI Desktop Duplication screen capture
- ONNX Runtime inference with TensorRT/CUDA/OpenVINO backends
- YOLOv5/YOLOv8/End2End model support
- Kalman Filter + Hungarian tracking
- Fuzzy PID mouse controller
- cvui-based control panel
- IbInputSimulator driver integration
- Command-line argument parsing
- Named constants (no magic numbers)

### Changed
- Refactored monolithic main.cpp into modular components
- Replaced magic numbers with named constants
- Improved error handling throughout

### Fixed
- Memory allocation in hot loop (pre-allocated buffers)
- String conversion overhead (cached wstring)
- Model file validation before loading
