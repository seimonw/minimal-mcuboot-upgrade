# Documentation

This folder contains technical documentation and reference materials for the minimal MCUboot upgrade project.

## Contents

### Technical Analysis
- **[MCXN236_MEMORY_ANALYSIS.md](MCXN236_MEMORY_ANALYSIS.md)** - Comprehensive analysis of the MCXN236 memory layout and MCUboot hard fault issue

### Reference Materials
- **[reference/MCXN23x.pdf](reference/MCXN23x.pdf)** - Official NXP MCXN236 datasheet

## Quick Reference

### Memory Layout (Corrected)
```
0x00000000-0x00013FFF   80KB    MCUboot Bootloader
0x00014000-0x00033FFF   128KB   Primary Slot (image-0)
0x00034000-0x00053FFF   128KB   Secondary Slot (image-1)
0x00054000-0x00063FFF   64KB    Storage Partition
```

### Key Technical Details
- **MCU**: ARM Cortex-M33 @ 150MHz
- **Flash**: 1MB (2 × 512KB banks) with ECC
- **SRAM**: 352KB total
- **XIP Base**: 0x10000000
- **MCUboot Size**: 80KB (CONFIG_FLASH_LOAD_SIZE=0x14000)

## Related Issues
- [GitHub Issue #1](https://github.com/seimonw/minimal-mcuboot-upgrade/issues/1) - MCUboot Hard Fault Analysis and Fix
