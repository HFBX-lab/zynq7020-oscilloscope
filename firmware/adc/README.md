# Internal digital ADC selftest (candidate)

This standalone application uses the **new ADC XSA only**, never automatically
connects to a board, and does not measure real ADC inputs. It leaves the shared
LVGL/FreeRTOS sources and old display artifacts unchanged. It reuses snapshot
copies of the existing board/video helpers; it does not feed ADC data into LVGL.

From the project root:

```powershell
& ./firmware/adc/host-test.ps1
& ./firmware/adc/build.ps1 -Xsa ./build/adc-soc/sp2_adc_internal.xsa -BuildRoot D:/FPGA_Project/SP2_ADC_Firmware
```

The ARM build requires the successful sibling hardware build-status.json and
matching XSA/bitstream hashes. It creates a fresh standalone BSP without a boot
BSP. Every run keeps a D-drive source/hardware snapshot, hashes, logs and an ELF
memory inspection. Publication to build/adc-firmware happens only after success.
All LOAD segments, heap and CPU stacks must fit below 0x01000000; the main stack
is 128 KiB. The build performs no JTAG connection, reset, loading or Flash write.

If separately authorized and run on the matching candidate platform, the app
initializes UART/DDR and the existing video pipeline, checks ABI at 0x43C10000,
then performs two 4096-word internal digital captures, validates every word, and
flushes the 16 KiB exports at 0x13000000 and 0x13004000 for later DDR extraction.
It prints capture IDs, first 20-bit sequences and CRC32 values. A third capture
uses a separate low-memory buffer after timeout, reset and recovery testing; the
first two exports remain intact. Polling has a 2000 ms software deadline. The
hardware's 1000000-cycle timeout is checked with TEST_CONTROL.valid disabled.
It deliberately avoids reads that cause AXI SLVERR / Cortex-A9 data aborts.

CRC32 uses polynomial 0xEDB88320, initial/final XOR 0xFFFFFFFF. Record CRC is over
4096 words serialized in little-endian byte order. Each halfword has five zero
reserved bits, synthetic OTR, and ten data bits. The decoder infers the first
20-bit sequence and the validator requires all following 4095 words to be its
consecutive modular sequence, including OTR and reserved bits. It rejects 1024
word address aliasing, sequence changes, bad OTR and nonzero padding.

Host tests establish only C protocol validation. ARM compilation establishes
offline tool/BSP compatibility. Board execution, DDR export CRC comparison and
real ADC/display/touch validation are separate evidence states.
