# Scattering Analysis GUI

This project provides a graphical user interface (GUI) for the analysis of X-ray and neutron scattering data. 
It is designed to assist researchers in visualizing, processing, and interpreting scattering datasets in an accessible and user-friendly environment.

## Features
- Load and visualize scattering data
- Basic data preprocessing and cleaning tools
- Interactive plots and analysis features
- Support for multiple data formats
- Extendable design for future scientific workflows

## Technologies Used
- C++ for core functionality
- wxWidgets for cross-platform GUI development
- Integration with scientific libraries for advanced analysis

## Getting Started

### Prerequisites
- C++17 or later
- wxWidgets library installed and configured
- CMake (for build management)

### Build Instructions
1. Clone the repository:
   ```bash
   git clone https://github.com/Ermithe06/Scattering-Analysis-GUI.git
   cd Scattering-Analysis-GUI
   ```

2. Create a build directory and run CMake:
   ```bash
   mkdir build && cd build
   cmake ..
   make
   ```

3. Run the executable:
   ```bash
   ./ScatteringAnalysisGUI
   ```

## Usage
- Launch the application and use the **File** menu to load scattering data files.
- Navigate through the visualization panel to explore plots.
- Apply preprocessing tools as needed before performing analysis.
