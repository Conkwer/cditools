import os
import sys
import subprocess
import time
import shutil
import datetime
from pathlib import Path

def main():
    # Architecture check — redump2cdi is x86-64 only
    import platform
    if platform.machine() not in ("x86_64", "AMD64"):
        print("This script relies on redump2cdi which is x86-64 only.")
        print("It will not work on your architecture (" + platform.machine() + ").")
        print("Use mkcdi.sh instead.")
        import time
        time.sleep(20)
        sys.exit(1)

    # Configuration variables
    lba = 45000
    binary = "0WINCEOS.BIN"
    enable_emulator = True
    romname = "mygame"
    
    # Constants for CD calculations
    TOT_SECTORS = 369000  # For 80-minute CD (700MB)
    LOUT = 6750
    OVR1 = 11400
    SECTOR_SIZE = 2048
    MARGIN_SECTORS = 150
    
    # Add system directory to PATH
    system_path = os.path.join(".", "system")
    os.environ["PATH"] = f"{system_path}{os.pathsep}{os.environ.get('PATH', '')}"
    
    try:
        size_sanity(lba, SECTOR_SIZE, OVR1)
        data_verification(binary)
        image_building(romname, TOT_SECTORS, LOUT, SECTOR_SIZE, MARGIN_SECTORS, OVR1)
        build_ver = image_renaming(romname)
        
        if enable_emulator:
            emulator(romname, build_ver)
            
    except Exception as e:
        print(f"Error: {e}")
        import sys as _sys; _sys.exit(0) if not _sys.stdin.isatty() else input("Press Enter to exit...")
        sys.exit(1)
    
    print("This window will be closed automatically")
    time.sleep(5)

def size_sanity(lba, SECTOR_SIZE, OVR1):
    """Check if Session1 size is within limits for the given LBA"""
    available_sectors = lba - OVR1
    max_bytes = available_sectors * SECTOR_SIZE
    max_mb = max_bytes // 1048576
    
    # Calculate data1 directory size
    data1_path = os.path.join(".", "data1")
    if not os.path.exists(data1_path):
        print("Warning: data1 directory not found")
        return lba
    
    size = get_directory_size(data1_path)
    size_mb = size // 1048576
    
    print(f"Checking Session1 size: {size_mb} MB against maximum {max_mb} MB for LBA {lba}")
    
    if size > max_bytes:
        print("\nERROR: Session1 oversized")
        print(f"Current size: {size_mb} MB")
        print(f"Maximum allowed for LBA {lba}: {max_mb} MB")
        print("Either reduce Session1 content or increase the LBA value\n")
        
        # Calculate sectors needed (rounding up) and set a new lba
        sectors = size // SECTOR_SIZE
        remainder = size % SECTOR_SIZE
        if remainder != 0:
            sectors += 1
        
        new_lba = OVR1 + sectors
        print(f"Adjusted LBA: {new_lba}")
        time.sleep(5)
        return new_lba  # You might want to use this value
    else:
        print(f"Session1 size OK: {size_mb} MB / {max_mb} MB")
        return lba

def get_directory_size(path):
    """Calculate total size of directory"""
    total = 0
    for dirpath, dirnames, filenames in os.walk(path):
        for filename in filenames:
            filepath = os.path.join(dirpath, filename)
            total += os.path.getsize(filepath)
    return total

def data_verification(binary):
    """Verify data directory and required files"""
    data_path = os.path.join(".", "data")
    
    # Create data directory if it doesn't exist
    if not os.path.exists(data_path):
        os.makedirs(data_path)
        print("Created data directory")
    
    # Remove read-only attributes
    for root, dirs, files in os.walk(data_path):
        for file in files:
            filepath = os.path.join(root, file)
            try:
                os.chmod(filepath, 0o644)  # Remove read-only
            except:
                pass
    
    # Check for main binary files
    binary_files = ["1ST_READ.BIN", "0WINCEOS.BIN", "1NOSDC.BIN"]
    found_binary = None
    
    for bin_file in binary_files:
        if os.path.exists(os.path.join(data_path, bin_file)):
            found_binary = bin_file
            break
    
    if not found_binary:
        print("Warning: No main binary file found (1ST_READ.BIN, 0WINCEOS.BIN, or 1NOSDC.BIN)")
        time.sleep(7)
        sys.exit(1)
    
    # Copy preconfigured IP.BIN if it doesn't exist
    ip_bin_path = os.path.join(data_path, "IP.BIN")
    if not os.path.exists(ip_bin_path):
        print("Warning: IP.BIN not found")
        print("Creating generic IP.BIN...")
        time.sleep(2)
        
        katana_bin = os.path.join(".", "system", "precon", "katana.bin")
        if os.path.exists(katana_bin):
            shutil.copy2(katana_bin, ip_bin_path)
            print("Copied generic IP.BIN")
        else:
            print("Error: Could not find katana.bin for generic IP.BIN")
    
    # Handle special case for 1NOSDC.BIN
    if found_binary == "1NOSDC.BIN":
        lodoss_bin = os.path.join(".", "system", "precon", "lodoss-5167.bin")
        if os.path.exists(lodoss_bin):
            shutil.copy2(lodoss_bin, ip_bin_path)
            print("Copied Lodoss IP.BIN for 1NOSDC.BIN")

def image_building(romname, TOT_SECTORS, LOUT, SECTOR_SIZE, MARGIN_SECTORS, OVR1):
    """Build the CD image sessions"""
    print("Building 1st session...")
    
    # Check for sortfile.str and set SORT parameter
    SORT = ""
    if os.path.exists("sortfile.str"):
        SORT = "-sort sortfile.str"
    
    # Check for data/0.0 and set DUMMY parameter
    DUMMY = ""
    if os.path.exists(os.path.join("data", "0.0")):
        DUMMY = "-hide 0.0 -hide-joliet 0.0"
    
    # Build 1st session (unpadded)
    cmd = ["mkisofs", "-V", romname, "-duplicates-once", "-l", "-J", "-r", "-o", "session1.iso", "data1"]
    run_command(cmd)
    
    # Calculate optimal LBA for session 2
    print("Calculating optimal LBA for session 2...")
    cmd = ["mkisofs", "-print-size", "-V", romname, "-exclude", "IP.BIN", 
           "-G", "data/IP.BIN", "-duplicates-once", "-l", "-J", "-r", "data"]
    
    # Add optional parameters if they exist
    if SORT:
        cmd.insert(-1, SORT.split()[0])
        cmd.insert(-1, SORT.split()[1])
    if DUMMY:
        for param in DUMMY.split():
            cmd.insert(-1, param)
    
    result = subprocess.run(cmd, capture_output=True, text=True, cwd=".")
    S2_SECTORS = int(result.stdout.strip())
    
    S2_LBA_OPT = TOT_SECTORS - LOUT - S2_SECTORS - MARGIN_SECTORS
    S1_target_bytes = (S2_LBA_OPT - OVR1) * SECTOR_SIZE
    
    print(f"Session 2 sectors: {S2_SECTORS}")
    print(f"Optimal LBA: {S2_LBA_OPT}")
    print(f"Session 1 target bytes: {S1_target_bytes}")
    
    # Verify the LBA is reasonable
    if S2_LBA_OPT < 15000:
        print("ERROR: Calculated LBA is too small!")
        print("Session 2 is too large or something is wrong with the calculation.")
        import sys as _sys; _sys.exit(0) if not _sys.stdin.isatty() else input("Press Enter to exit...")
        sys.exit(1)
    
    # Pad session 1 to hit the optimal LBA
    print("Padding session 1...")
    run_command(["fill", "session1.iso", str(S2_LBA_OPT)])
    
    # Building 2nd session at optimal LBA
    print(f"Building 2nd session at LBA {S2_LBA_OPT}...")
    cmd = ["mkisofs", "-C", f"0,{S2_LBA_OPT}", "-V", romname, "-exclude", "IP.BIN", 
           "-G", "data/IP.BIN", "-M", "session1.iso", "-duplicates-once", "-l", 
           "-J", "-r", "-o", "session2.iso", "data"]
    
    # Add optional parameters if they exist
    if SORT:
        cmd.insert(-1, SORT.split()[0])
        cmd.insert(-1, SORT.split()[1])
    if DUMMY:
        for param in DUMMY.split():
            cmd.insert(-1, param)
    
    run_command(cmd)
    
    # Merging the isos
    print("Merging the isos...")
    run_command(["iso2raw", "session1.iso", "session1.bin", "--mode2"])
    run_command(["iso2raw", "session2.iso", "session2.bin", "--mode2"])
    
    # Building CDI
    print("Building CDI...")
    # Write a Linux-compatible CUE (Windows paths from bundled CUE won't work)
    with open("selfboot_linux.cue", "w") as f:
        f.write('REM SESSION 01\nFILE "session1.bin" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n')
        f.write('REM SESSION 02\nFILE "session2.bin" BINARY\n  TRACK 02 MODE2/2352\n    INDEX 01 00:00:00\n')
    run_command(["redump2cdi", "--cue", "selfboot_linux.cue", "--cdi", "output.cdi"], check_output=False)
    
    if not os.path.exists("./output.cdi"):
        print("ERROR: Verify selfboot.cue and paths")
        import sys as _sys; _sys.exit(0) if not _sys.stdin.isatty() else input("Press Enter to exit...")
        sys.exit(1)

def image_renaming(romname):
    """Rename and organize the output files"""
    # Generate build version using current date and time
    build_ver = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    
    # Write build version to file (optional, for consistency with original behavior)
    build_ver_path = os.path.join(".", "system", "build.ver")
    with open(build_ver_path, 'w') as f:
        f.write(build_ver)
    
    # Rename output file
    temp_name = f"{romname}-{build_ver}.tmp"
    if os.path.exists("output.cdi"):
        os.rename("output.cdi", temp_name)
    
    # Create archive directory if it doesn't exist
    archive_dir = "archive"
    if not os.path.exists(archive_dir):
        os.makedirs(archive_dir)
    
    # Move existing CDI files to archive
    for file in os.listdir("."):
        if file.endswith(".cdi"):
            shutil.move(file, os.path.join(archive_dir, file))
    
    # Rename temp file to final CDI
    if os.path.exists(temp_name):
        final_name = f"{romname}-{build_ver}.cdi"
        os.rename(temp_name, final_name)
        print(f'File "{final_name}" is created.')
    
    # Clean up temporary files
    for ext in [".iso", ".bin"]:
        for file in os.listdir("."):
            if file.endswith(ext):
                try:
                    os.remove(file)
                except:
                    pass
    
    return build_ver

def emulator(romname, build_ver):
    """Run the emulator if requested"""
    print("Press Enter if you want to run the emulator or just close the console")
    input()
    
    cdi_file = f"{romname}-{build_ver}.cdi"
    redream_path = os.path.join(".", "redream", "redream.exe")
    
    if os.path.exists(redream_path) and os.path.exists(cdi_file):
        run_command([redream_path, cdi_file])
    else:
        print("Emulator or CDI file not found")

def run_command(cmd, check_output=True, cwd="."):
    """Run a command and handle errors"""
    try:
        if check_output:
            result = subprocess.run(cmd, capture_output=True, text=True, check=True, cwd=cwd)
            return result
        else:
            result = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, cwd=cwd)
            return result
    except subprocess.CalledProcessError as e:
        print(f"Command failed: {' '.join(cmd)}")
        print(f"Error: {e}")
        raise
    except FileNotFoundError as e:
        print(f"Command not found: {cmd[0]}")
        print("Make sure all required tools are installed and in PATH")
        raise

if __name__ == "__main__":
    main()