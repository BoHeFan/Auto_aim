# AutoAim - Real-time AI Aim Assistant

A high-performance, real-time AI vision system built with modern C++.

## Features

- **Low-latency Screen Capture**: DXGI Desktop Duplication API
- **AI Object Detection**: ONNX Runtime with TensorRT/CUDA/OpenVINO backends
- **Multi-object Tracking**: Kalman Filter + Hungarian algorithm
- **Smooth Aim Control**: Fuzzy PID controller with driver-level input
- **Configurable UI**: cvui-based control panel

## Requirements

- Windows 10/11
- Visual Studio 2022 or MSVC Build Tools
- CMake 3.20+
- CUDA 11.x+ (for GPU/TensorRT)
- TensorRT 8.x+ (optional, for best performance)

## Dependencies

| Library | Version | Purpose |
|---------|---------|---------|
| OpenCV | 4.10+ | Image processing |
| ONNX Runtime | 1.19+ | ML inference |
| IbInputSimulator | - | Driver-level input |

## Quick Start

```powershell
# Clone repository
git clone https://github.com/yourusername/AutoAim.git
cd AutoAim

# Download dependencies
./setup_deps.ps1

# Build
./build_release.ps1

# Run (requires Administrator)
./build/Release/AutoAim.exe
```

## Project Structure

```
AutoAim/
├── src/
│   ├── core/           # Core logic (config, detection, tracker)
│   ├── capture/        # Screen capture (DXGI)
│   ├── input/          # Mouse control (FuzzyPID)
│   ├── ui/             # Control panel (cvui)
│   ├── utils/          # Utilities (constants, key names)
│   ├── third_party/    # External headers
│   └── main.cpp        # Entry point
├── tests/              # Unit tests
├── config/             # Configuration files
├── CMakeLists.txt      # Build configuration
└── README.md
```

## CLI Options

```
Usage: AutoAim [options]

Options:
  --model <path>       Path to ONNX model file
  --device <0-3>       Inference device (0=CPU, 1=CUDA, 2=TensorRT, 3=OpenVINO)
  --crop-size <n>      Crop region size (default: 640)
  --confidence <f>     Detection confidence threshold (0.0-1.0)
  --no-vis             Disable visualization window
  --help, -h           Show this help message
```

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      AimAssistant                           │
│  ┌─────────────┐  ┌──────────────┐  ┌───────────────────┐  │
│  │   Config    │  │ ControlPanel │  │     Logger        │  │
│  └─────────────┘  └──────────────┘  └───────────────────┘  │
│                                                             │
│  ┌─────────────────────────────────────────────────────┐   │
│  │                    Main Loop                         │   │
│  │  ┌───────────────┐  ┌───────────────┐  ┌──────────┐ │   │
│  │  │ScreenCapturer │→ │ObjectDetector │→ │ Tracker  │ │   │
│  │  │   (DXGI)      │  │ (ONNX/TensorRT)│  │(Kalman)  │ │   │
│  │  └───────────────┘  └───────────────┘  └──────────┘ │   │
│  │                           ↓                          │   │
│  │  ┌───────────────────────────────────────────────┐  │   │
│  │  │            MouseController (FuzzyPID)          │  │   │
│  │  └───────────────────────────────────────────────┘  │   │
│  └─────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

## Building from Source

### Prerequisites

1. Install Visual Studio 2022 with C++ workload
2. Install CMake 3.20+
3. Download dependencies to `deps/`:
   - OpenCV 4.10
   - ONNX Runtime 1.19 (GPU version)

### Build Steps

```powershell
# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --config Release --parallel
```

## Acknowledgments

- [IbInputSimulator](https://github.com/Chaoses-Ib/IbInputSimulator) - Driver-level input simulation
- [ONNX Runtime](https://github.com/microsoft/onnxruntime) - ML inference
- [OpenCV](https://opencv.org/) - Computer vision library
- [cvui](https://github.com/Dovyski/cvui) - Immediate-mode GUI

## Disclaimer

This project is for **educational and technical research purposes only**. The code demonstrates computer vision, machine learning inference, and low-level Windows API integration techniques. Users are responsible for ensuring compliance with all applicable laws and terms of service.
