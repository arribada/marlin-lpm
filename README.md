# MARLIN Low Power MCU Firmware

This is a repo for the MARLIN Lower Power MCU Firmware

# Requirements #
This repo is for Windows Machines using Windows 11 and above.
It is assumed that VSCode is already installed and that you have SSH access to Github. 

## WSL & Docker Setup ##
The development environment is defined in the Dockerfile present in the .devcontainer folder. It builds an image that can be run as a Dev Container on WSL2. WSL2 enables a Linux environment to run on a Windows Machine. 

1. Follow this guide: https://learn.microsoft.com/en-us/windows/wsl/setup/environment to install WSL2. Ensuring that you:
  1. Install WSL2 with Ubuntu
  2. Create a Linux User
  3. Update and upgrade the packages
3. Install Docker Desktop using the following guide: https://learn.microsoft.com/en-us/windows/wsl/tutorials/wsl-containers#install-docker-desktop
  1. Please follow up to the end of the Install Docker Desktop section
  2. It can be useful to enable "Start Docker Desktop when you sign in to you computer" in the Docker Desktop settings. 
4. Download and install the latest version of [wsl-usb-gui](https://gitlab.com/alelec/wsl-usb-gui/-/releases) This allows USB pass-through from Windows to WSL.
  1. Once installed, open it and allow it to install its dependencies. 

## VS Code Dev Containers ##
1. Install the Dev Containers Extension: https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers 

# Getting Started #
This repo hasn't been tested on Windows machines, initially please follow the Mac Tested steps below. If these don't work please follow the alternative steps.

## Tested Steps ##
1. Open WSL
2. Setup SSH
3. Run `git clone git@github.com:arribada/marlin-lpm.git` in WSL 
4. Open VSCode and click the Blue Box in the bottom left hand corner and click `WSL`
5. Open the Repo with `File -> Open Folder`
6. On opening you should be presented with an open within a dev container prompt. If not press F1 to open the command palate and type: `Open Folder in Dev Container` to find the command.
7. Docker will build the image, this can take some time, follow the Dev Container prompt to show logs.

## Running Main Build  ##
1. Run `just`
2. Run `just flash`

## Running Test Suite  ##
1. Run `just test` from the project directory

## LSP - Clangd ##
This repo uses `clangd` as its LSP for autocomplete etc. However, it uses `compile_commands.json` which are generated in each `build` folder. Therefore if you are moving between different projects (folders containing a `CMakeLists.txt` and `prj.conf`) `clangd` might not find certain files. This is easily fixed by building your active project. 

## Folder structure within the Dev Environment ##
The Docker file installs Zephyr, the Zephyr SDK and it's prerequisites into the folder structure shown below. The bash terminal enters within the arribada folder which contains the sample code. The file structure is a West workspace with the Zephyr files, module folder and other relevant folders stored within this.
```
root/
├── west-workspace/
   ├── marlin-lpm/
    ├── CMakeLists.txt
    ├── justfile
    ├── prj.conf
    ├── README.md
    ├── src
    │   └── main.cpp
    └── west.yml
   ├── etl/
   ├── magic_enum/
   ├── modules/
   └── zephyr/
```

## Extra Info ##
