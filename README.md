# Scattering Analysis GUI

## Overview

**Scattering Analysis GUI** is a desktop-based scientific visualization and analysis tool designed for **X-ray and neutron scattering data**, with additional support for **image-based analysis workflows**.  
The application provides an interactive, extensible environment for exploring scattering patterns, radial averages, histograms, and regions of interest (ROIs) using a modern C++/wxWidgets GUI.

This project is aimed at **researchers, students, and developers** working in experimental physics, materials science, and computational imaging.

---

## Key Features

### Data & Image Handling
- Load and visualize grayscale image data (including legacy raw formats)
- Interactive image viewing with zoom, pan, and pixel inspection
- Undo history for image operations

### Analysis Tools
- Circular averaging (nearest-neighbor baseline algorithm)
- Radial average sweeps (average vs. radius)
- Histogram generation and visualization
- CSV export of radial profiles
- Results panel for numerical output

### Interaction & Annotation
- Region of Interest (ROI) selection and management
- Crop, copy, paste, and blend operations
- Stack viewer for multi-image datasets

### Extensibility
- Plugin system for image filters (.dll / .so)
- Modular architecture for future scientific workflows

---

## Technologies Used

- **C++17**
- **wxWidgets**
- **CMake**
- **STL (Standard Template Library)**

---

## Getting Started

### Prerequisites
- C++17 or later
- wxWidgets installed and configured
- CMake 3.15+
- GCC / Clang / MSVC (Visual Studio 2022 supported)

---

### Build Instructions

```bash
git clone https://github.com/Ermithe06/Scattering-Analysis-GUI.git
cd Scattering-Analysis-GUI
mkdir build
cd build
cmake ..
cmake --build .
```

Run the executable:

```bash
./ScatteringAnalysisGUI
```

(On Windows, run the generated `.exe` from the build directory.)

---

## Usage

1. Launch the application
2. Load scattering data or image files using the File Browser
3. Explore images using zoom, pan, and ROI tools
4. Run circular averaging or radial sweep analysis
5. View numerical results and plots
6. Export results to CSV for publication or further analysis

---

## Scientific Focus

This project emphasizes **clear, reproducible baseline algorithms** to support methodological research and comparison with advanced techniques such as weighted averaging and subpixel interpolation.

---

## Author

**Ermithe Tilusca**
