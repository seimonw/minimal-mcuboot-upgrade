# MCXN236 Memory Layout Analysis

## Overview

This document provides technical analysis of the MCXN236 memory layout and its relationship to the MCUboot hard fault issue encountered in this project.

## Hardware Specifications

### MCXN236 Key Features (from datasheet)
- **MCU**: Arm® Cortex®-M33 150MHz 32-bit
- **Flash**: Up to 1MB (2 × 512KB banks)
- **SRAM**: 352KB total
- **Flash Features**:
  - Flash Swap support
  - Read While Write capability
  - ECC (Error Correcting Code)
  - On-the-fly memory encryption with authentication
  - Implicit-protected Flash Region (IFR)

### Memory Architecture
- **Dual Flash Banks**: 2 × 512KB banks supporting advanced operations
- **Flash Swap**: Hardware-level bank swapping capability
- **ECC Protection**: Error correction for flash memory integrity
- **Memory Encryption**: Hardware encryption for secure code execution

## MCUboot Configuration Analysis

### From `mcuboot.config`:
```
CONFIG_FLASH_SIZE=1024                    # 1MB total flash
CONFIG_FLASH_BASE_ADDRESS=0x10000000      # XIP (Execute-in-Place) base
CONFIG_FLASH_LOAD_SIZE=0x14000            # 80KB MCUboot size
CONFIG_SRAM_SIZE=192                      # 192KB SRAM
CONFIG_SRAM_BASE_ADDRESS=0x30000000       # SRAM base address
```

### Key Insights:
1. **XIP Addressing**: Flash is accessed at `0x10000000` (not `0x00000000`)
2. **MCUboot Size**: Expects 80KB (0x14000) not the 64KB we initially configured
3. **Total Flash**: 1MB available, much larger than our partition layout assumed

## Root Cause Analysis

### Original Problem
The hard fault occurred because of a **multi-layered issue**:

1. **Incorrect MCUboot Size**: 64KB configured vs 80KB expected
2. **Wrong Slot Addresses**: Partitions didn't align with MCUboot's expectations
3. **🚨 CRITICAL: TrustZone Security Boundary**: Hardware-enforced ~305KB limit
4. **Memory Protection Violations**: Accessing Secure flash from Non-Secure world

### Memory Access Pattern and TrustZone Discovery
```
MCUboot Boot Sequence:
1. Boot from 0x10000000 (XIP base) in Non-Secure world
2. Validate primary slot (image-0) 
3. Check secondary slot (image-1) for updates ← HARD FAULT HERE
4. Perform signature validation (RSA-2048)
5. Copy/activate new image if valid
```

**GDB Analysis Revealed:**
- Memory accessible: 0x00000000-0x0004C0FF (~305KB)
- Hard boundary: "Cannot access memory" at 0x4C100+
- This represents the **TrustZone Non-Secure/Secure partition boundary**

**TrustZone Architecture:**
```
Non-Secure Flash: 0x00000000-0x0004C0FF (~305KB) ← MCUboot accessible
Secure Flash:     0x0004C100-0x000FFFFF (~719KB) ← Hardware protected
```

The hard fault occurred when MCUboot tried to read the secondary slot at addresses beyond the TrustZone boundary.

## Corrected Memory Layout (TrustZone-Safe)

### Physical Flash Layout
```
Address Range           Size    Purpose                 TrustZone Status
0x00000000-0x00013FFF   80KB    MCUboot Bootloader     Non-Secure (0-80KB)
0x00014000-0x0002BFFF   96KB    Primary Slot (image-0)  Non-Secure (80-176KB)
0x0002C000-0x00043FFF   96KB    Secondary Slot (image-1) Non-Secure (176-272KB)
0x00044000-0x0004BFFF   32KB    Storage Partition       Non-Secure (272-304KB)
0x0004C000-0x000FFFFF   ~719KB  Secure Flash Region     Secure (304KB-1MB)
```

### XIP Mapping (Runtime View)
```
XIP Address             Physical Address    Purpose                 TrustZone
0x10000000-0x10013FFF   0x00000000-0x13FFF  MCUboot (executing)     Non-Secure
0x10014000-0x1002BFFF   0x00014000-0x2BFFF  Primary Slot (executing) Non-Secure
0x1002C000-0x10043FFF   0x0002C000-0x43FFF  Secondary Slot (staging) Non-Secure
0x10044000-0x1004BFFF   0x00044000-0x4BFFF  Storage                 Non-Secure
0x1004C000-0x100FFFFF   0x0004C000-0xFFFFF  Secure Flash (protected) Secure
```

## Flash Bank Considerations

### Dual Bank Architecture
- **Bank 0**: 0x00000000-0x0007FFFF (512KB)
- **Bank 1**: 0x00080000-0x000FFFFF (512KB)

### Bank Boundary Analysis
Our corrected layout keeps all partitions within Bank 0:
- MCUboot: 0x00000000-0x00013FFF ✓ (Bank 0)
- Slot 0: 0x00014000-0x00033FFF ✓ (Bank 0)  
- Slot 1: 0x00034000-0x00053FFF ✓ (Bank 0)
- Storage: 0x00054000-0x00063FFF ✓ (Bank 0)

This avoids potential bank-crossing issues that could cause access violations.

## MCUboot Operation Details

### Image Validation Process
1. **Header Check**: Verify image magic number and version
2. **Signature Validation**: RSA-2048 signature verification
3. **Hash Verification**: SHA-256 hash check
4. **TLV Processing**: Process Type-Length-Value trailer data

### Update Process (Overwrite-Only Mode)
1. **Secondary Slot Check**: Validate staged update image
2. **Image Copy**: Copy from secondary to primary slot
3. **Verification**: Verify copied image integrity
4. **Boot**: Execute new primary image

## Technical Recommendations

### Memory Safety
1. **Alignment**: Ensure all flash operations are properly aligned
2. **Boundary Checking**: Verify addresses don't cross bank boundaries
3. **ECC Handling**: Account for ECC overhead in size calculations
4. **Access Patterns**: Use sequential access where possible

### Future Enhancements
1. **Bank Utilization**: Consider using both flash banks for larger images
2. **Encryption**: Leverage hardware encryption capabilities
3. **Swap Mode**: Evaluate swap mode for more robust updates
4. **External Flash**: Consider external flash for secondary slot

## Debug Information

### Memory Access Testing
```bash
# GDB commands to verify memory access
(gdb) x/16x 0x34000    # Secondary slot start
(gdb) x/16x 0x54000    # Storage partition
(gdb) info mem         # Memory region information
```

### MCUboot Debug Output
Enable debug logging to monitor:
- Slot validation messages
- Signature verification progress
- Memory access patterns
- Error conditions

## References

- **Datasheet**: `docs/reference/MCXN23x.pdf`
- **MCUboot Documentation**: https://docs.mcuboot.com/
- **Zephyr Flash Documentation**: https://docs.zephyrproject.org/latest/hardware/peripherals/flash.html
- **ARM Cortex-M33 Reference**: ARM DDI 0489F

## Conclusion

The hard fault was caused by a mismatch between the expected and actual memory layout. The corrected configuration aligns with the MCXN236's dual-bank flash architecture and MCUboot's configuration requirements, providing a robust foundation for firmware updates.

The fix addresses both the immediate hard fault issue and establishes a proper memory layout for future development and maintenance.
