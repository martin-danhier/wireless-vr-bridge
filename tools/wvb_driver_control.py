"""Control the WVB driver state from the command line."""

import click
import sys
import subprocess
import os

# Default SteamVR directory
# On Windows
if sys.platform == "win32":
    STEAMVR_DIR = r"C:\Program Files (x86)\Steam\steamapps\common\SteamVR"
else:
    STEAMVR_DIR = rf"{os.getenv('HOME')}/.steam/steam/steamapps/common/SteamVR"

# Utils

def openvr_platform():
    """Returns the current platform in the OpenVR format.

    One of: "win32", "win64", "linux32", "linux64", "osx32"
    """

    if sys.platform == "win32":
        return "win64" if sys.maxsize > 2**32 else "win32"
    elif sys.platform == "linux":
        return "linux64" if sys.maxsize > 2**32 else "linux32"
    elif sys.platform == "darwin":
        return "osx32"
    else:
        raise RuntimeError("unsupported platform")

def vrpathreg_path(steamvr_dir):
    """Returns the path to the vrpathreg executable."""

    platform = openvr_platform()

    # On Windows, it is a .exe file
    if platform.startswith("win"):
        filename = "vrpathreg.exe"
    else:
        # TODO test on Linux and OSX
        filename = "vrpathreg"

    path = os.path.join(steamvr_dir, "bin", platform, filename)

    # Ensure that the file exists
    if not os.path.isfile(path):
        raise RuntimeError("could not find vrpathreg executable")

    return path

def get_absolute_driver_dir(driver_dir):
    # Ensure that the driver directory exists
    if not os.path.isdir(driver_dir):
        raise RuntimeError("driver directory does not exist")

    # And that there is a driver.vrdrivermanifest file
    manifest_path = os.path.join(driver_dir, "driver.vrdrivermanifest")
    if not os.path.isfile(manifest_path):
        raise RuntimeError("driver manifest does not exist, the directory is not a driver")

    # Get the absolute path
    return os.path.abspath(driver_dir)


# Commands

@click.group()
def driver_control():
    """Control the WVB driver from the command line."""
    pass

@driver_control.command()
@click.option('--driver_dir', required=True, help='Path to WVB driver directory.')
@click.option('--steamvr_dir', default=STEAMVR_DIR, help='Path to SteamVR directory.')
def enable( driver_dir, steamvr_dir):
    """Enable the WVB driver."""
    if steamvr_dir != STEAMVR_DIR:
        print(f'Using SteamVR directory \"{steamvr_dir}\"')

    # Get paths
    driver_dir = get_absolute_driver_dir(driver_dir)
    vrpathreg = vrpathreg_path(steamvr_dir)

    # Enable the driver
    process = subprocess.run([vrpathreg, "adddriver", driver_dir], check=True)

    if process.returncode == 0:
        print("Driver enabled.")



@driver_control.command()
@click.option('--steamvr_dir', default=STEAMVR_DIR, help='Path to SteamVR directory.')
def disable(steamvr_dir):
    """Disable the WVB driver."""

    if steamvr_dir != STEAMVR_DIR:
        print(f'Using SteamVR directory \"{steamvr_dir}\"')

    # Get paths
    vrpathreg = vrpathreg_path(steamvr_dir)

    # Disable the driver
    process = subprocess.run([vrpathreg, "removedriverswithname", "wvb_driver"], check=True)

    if process.returncode == 0:
        print("All WVB drivers disabled.")

@driver_control.command()
@click.option('--steamvr_dir', default=STEAMVR_DIR, help='Path to SteamVR directory.')
def status(steamvr_dir):
    """Print the driver status."""

    if steamvr_dir != STEAMVR_DIR:
        print(f'Using SteamVR directory \"{steamvr_dir}\"')

    # Get paths
    vrpathreg = vrpathreg_path(steamvr_dir)

    # Check the driver status
    process = subprocess.run([vrpathreg, "finddriver", "wvb_driver"], capture_output=True, text=True)

    # If there is a line in the output, the driver is enabled
    if process.stdout:
        print("The driver is currently enabled.")
    else:
        print("The driver is currently disabled.")

# Main dispatcher
if __name__ == '__main__':
    try:
        driver_control()
    except RuntimeError as e:
        # Print in stderr
        print(f"\nError: {e}.", file=sys.stderr)
        sys.exit(1)
