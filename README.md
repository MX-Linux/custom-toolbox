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
<img width="657" height="634" alt="image" src="https://github.com/user-attachments/assets/5c4bfc99-7a13-4150-8d50-4b7d017140da" />

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
- C++20 compatible compiler (GCC 10+ or Clang 10+)
- CMake 3.16+ or qmake6

**Ubuntu/Debian:**
```bash
sudo apt install qt6-base-dev qt6-tools-dev cmake build-essential
```

**Fedora:**
```bash
sudo dnf install qt6-qtbase-devel qt6-qttools-devel cmake gcc-c++
```

#### Build with CMake (Recommended)
```bash
git clone https://github.com/MX-Linux/custom-toolbox.git
cd custom-toolbox
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build
```

#### Build with qmake
```bash
git clone https://github.com/MX-Linux/custom-toolbox.git
cd custom-toolbox
qmake6
make -j$(nproc)
sudo make install
```

#### Build Options
- **Use Clang**: `cmake -B build -DUSE_CLANG=ON`
- **Debug Build**: `cmake -B build -DCMAKE_BUILD_TYPE=Debug`

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
├── custom-toolbox.pro     # qmake build configuration
├── custom-toolbox.conf    # Default configuration
├── example.list           # Example launcher configuration
└── README.md             # This file
```

### Code Standards
- **Language**: C++20 with strict compiler warnings
- **Framework**: Qt6 (Core, Gui, Widgets)
- **Build Systems**: CMake (preferred) and qmake
- **Compiler Flags**: Pedantic warnings, treat warnings as errors
- **Style**: Follow existing code conventions

### Contributing
1. Fork the repository
2. Create a feature branch
3. Make your changes following the code standards
4. Test with both CMake and qmake builds
5. Submit a pull request

### Testing
```bash
# Test CMake build
cmake -B build && cmake --build build
./build/custom-toolbox

# Test qmake build
qmake6 && make
./custom-toolbox

# Clean builds
make clean  # for qmake
rm -rf build  # for cmake
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
