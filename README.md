# Custom Toolbox

[![latest packaged version(s)](https://repology.org/badge/latest-versions/custom-toolbox.svg)](https://repology.org/project/custom-toolbox/versions)
[![build result](https://build.opensuse.org/projects/home:mx-packaging/packages/custom-toolbox/badge.svg?type=default)](https://software.opensuse.org//download.html?project=home%3Amx-packaging&package=custom-toolbox)

A Qt-based application for creating custom launcher toolboxes for Linux. Custom Toolbox allows users to create personalized collections of applications organized by categories, providing quick access to frequently used tools and utilities.

## Features

- **Custom Launcher Creation**: Create personalized toolboxes with your favorite applications
- **Category Organization**: Group applications into logical categories for easy navigation
- **Flexible Configuration**: Text-based configuration files for easy customization
- **Root Application Support**: Launch applications with elevated privileges when needed
- **Terminal Application Support**: Run terminal-based applications in a terminal window
- **Internationalization**: Multi-language support with extensive translations
- **Theme Integration**: Integrates with system themes and icon sets

## Screenshots
<img width="660" height="634" alt="Screenshot_2025-08-07_18-21-17" src="https://github.com/user-attachments/assets/27600694-4fea-4b87-accf-3a720443e965" />

## Installation

### From Package Manager
Custom Toolbox is available in MX Linux repositories:
```bash
sudo apt update
sudo apt install custom-toolbox
```

### Building from Source

#### Prerequisites
- Qt6 development libraries (Core, Gui, Widgets, LinguistTools)
- C++20 compatible compiler (GCC 14+ or Clang 15+)
- CMake 3.16+
- Ninja build system (recommended)

**Ubuntu/Debian:**
```bash
sudo apt install qt6-base-dev qt6-tools-dev cmake ninja-build build-essential
```

**Fedora:**
```bash
sudo dnf install qt6-qtbase-devel qt6-qttools-devel cmake ninja-build gcc-c++
```

**Arch/Manjaro:**
```bash
sudo pacman -S --needed base-devel cmake ninja qt6-base qt6-tools
makepkg -si  # builds using the included PKGBUILD
```

**For Clang (optional):**
```bash
sudo apt install clang  # Ubuntu/Debian
sudo dnf install clang  # Fedora
sudo pacman -S clang    # Arch/Manjaro
```

#### Build with CMake + Ninja (Recommended)
```bash
git clone https://github.com/MX-Linux/custom-toolbox.git
cd custom-toolbox
./build.sh
sudo cmake --install build
```

#### Alternative Build Methods

**Using build script with options:**
```bash
./build.sh              # Release build
./build.sh --debug      # Debug build
./build.sh --clang      # Build with clang
./build.sh --clean      # Clean rebuild
```

**Direct CMake commands:**
```bash
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```



#### Build Options and Compiler Support

**Compiler Selection:**
- **GCC (default)**: Optimized builds with LTO support
- **Clang**: Alternative compiler with different optimizations
  ```bash
  ./build.sh --clang
  # OR
  cmake -B build -DUSE_CLANG=ON
  ```

**Build Types:**
- **Release**: Optimized build with `-O3`, LTO, and `NDEBUG`
- **Debug**: Debug symbols, no optimization
  ```bash
  ./build.sh --debug
  # OR
  cmake -B build -DCMAKE_BUILD_TYPE=Debug
  ```

**Features:**
- **Ninja Build System**: Fast parallel builds
- **Colored Output**: Automatic colored diagnostics
- **Compile Commands**: `compile_commands.json` for IDE support
- **Automatic Version**: Derived from `debian/changelog` or `PROJECT_VERSION_OVERRIDE` (useful for Arch PKGBUILD)

## Usage

### Basic Usage
1. Launch Custom Toolbox from the applications menu or run `custom-toolbox` in terminal
2. The application will scan `/etc/custom-toolbox/` for `.list` configuration files
3. Click on any application button to launch it
4. Use the category tabs to navigate between different application groups

### Configuration Files

Custom Toolbox reads configuration files from `/etc/custom-toolbox/` with the `.list` extension. These are simple text files that define the launcher's structure.

#### Basic Configuration File Format
```ini
# Comments start with #
Name=My Custom Launcher
Comment=A collection of my favorite tools

Category=Development
code
gedit
git-gui

Category=System Tools
htop
gparted root
gnome-terminal terminal

Category=Graphics
gimp
inkscape
blender
```

#### Configuration File Structure

**Header Section:**
- `Name=` - The title displayed in the launcher window
- `Comment=` - Optional description of the launcher

**Category Sections:**
- `Category=CategoryName` - Defines a new category tab
- Following lines list application names (one per line)
- Applications are launched using their desktop file names or executable names

**Application Flags:**
- `appname root` - Launch application with root privileges
- `appname terminal` - Launch application in a terminal window
- `appname root terminal` - Launch with both root privileges and in terminal

#### Example Configuration Files

**Development Tools (`/etc/custom-toolbox/development.list`):**
```ini
Name=Development Tools
Comment=Programming and development applications

Category=Editors
code
atom
vim terminal
emacs

Category=Version Control
git-gui
gitk
meld

Category=Databases
mysql-workbench
pgadmin4
```

**System Administration (`/etc/custom-toolbox/sysadmin.list`):**
```ini
Name=System Administration
Comment=System maintenance and administration tools

Category=Monitoring
htop terminal
iotop root terminal
nethogs root terminal

Category=Disk Management
gparted root
baobab
gnome-disk-utility

Category=Network
wireshark root
nmap terminal
iptables root terminal
```

**Media Tools (`/etc/custom-toolbox/media.list`):**
```ini
Name=Media Tools
Comment=Audio, video, and graphics applications

Category=Graphics
gimp
inkscape
krita
blender

Category=Audio
audacity
ardour
lmms

Category=Video
kdenlive
openshot
vlc
```

### Application Configuration

The application behavior can be customized using `/etc/custom-toolbox/custom-toolbox.conf`:

```ini
# Hide GUI or minimize it when launching an app
hideGUI=false

# Set GUI minimum size (pixels)
min_height=500
min_width=0

# Set icon size (pixels)
icon_size=40

# Set the GUI editor used in the program
# Use an absolute path here
gui_editor=/usr/bin/featherpad
```

#### Configuration Options

- **hideGUI**: Hide or minimize the launcher window when starting an application
- **min_height/min_width**: Set minimum window dimensions
- **icon_size**: Size of application icons in pixels
- **gui_editor**: Default text editor for editing configuration files

### Creating Custom Launchers

1. **Create a new `.list` file** in `/etc/custom-toolbox/`:
   ```bash
   sudo nano /etc/custom-toolbox/my-tools.list
   ```

2. **Define the launcher structure**:
   ```ini
   Name=My Personal Tools
   Comment=My frequently used applications

   Category=Internet
   firefox
   thunderbird

   Category=Office
   libreoffice-writer
   libreoffice-calc
   ```

3. **Restart Custom Toolbox** to load the new configuration

### Finding Application Names

To find the correct application names to use in configuration files:

```bash
# List all desktop files
ls /usr/share/applications/

# Search for specific applications
find /usr/share/applications/ -name "*firefox*"

# Check if an executable exists
which gimp
```

## Development

### Project Structure
```
custom-toolbox/
├── src/                    # Source code
│   ├── main.cpp           # Application entry point
│   ├── mainwindow.cpp/h   # Main window implementation
│   ├── flatbutton.cpp/h   # Custom button widget
│   ├── about.cpp/h        # About dialog
│   ├── common.h           # Common definitions
│   ├── version.h          # Version information
│   └── mainwindow.ui      # UI layout file
├── translations/          # Translation files
├── icons/                 # Application icons
├── help/                  # Help documentation
├── CMakeLists.txt         # CMake build configuration
├── build.sh               # Build script with options
├── custom-toolbox.conf    # Default configuration
├── example.list           # Example launcher configuration
└── README.md             # This file
```

### Code Standards
- **Language**: C++20 with strict compiler warnings (`-Wpedantic -Werror`)
- **Framework**: Qt6 (Core, Gui, Widgets)
- **Build System**: CMake with Ninja generator
- **Compilers**: GCC 14+ (with LTO) or Clang 15+ (default GCC)
- **Optimizations**: `-O3` for Release, LTO for GCC builds
- **Warnings**: All warnings as errors, pedantic mode enabled
- **Style**: `snake_case` variables, `PascalCase` classes, minimal comments

### Contributing
1. Fork the repository
2. Create a feature branch
3. Make your changes following the code standards
4. Test with both GCC and Clang compilers
5. Run the application to verify functionality
6. Submit a pull request

### Testing
```bash
# Test with different compilers
./build.sh              # GCC Release
./build.sh --clang      # Clang Release
./build.sh --debug      # GCC Debug
./build.sh --clang --debug  # Clang Debug

# Run the application
./build/custom-toolbox

# Clean builds
./build.sh --clean
```

### Development Tools
```bash
# Generate compile_commands.json for IDEs
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Check version info
./build/custom-toolbox --version
./build/custom-toolbox --help
```

### Debug Mode
Run with debug output:
```bash
QT_LOGGING_RULES="*=true" custom-toolbox
```

## License

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

## Authors

- **Adrian** - Main developer
- **MX Linux Team** - <http://mxlinux.org>

## Support

- **Bug Reports**: [GitHub Issues](https://github.com/MX-Linux/custom-toolbox/issues)
- **MX Linux Forum**: [https://forum.mxlinux.org](https://forum.mxlinux.org)
- **Documentation**: [MX Linux Wiki](https://mxlinux.org/wiki/)
