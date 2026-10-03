

**Build command (Docker)**

>[!info] CD เข้าไปใน project folder เพราะเราจะ map เป็น workspace โดยตรง ด้วย
`--mount "type=bind,source=$((Get-Location).Path),target=/workspace"`

```powershell

docker run --rm --mount "type=bind,source=$((Get-Location).Path),target=/workspace" -w /workspace/ espressif/idf:release-v6.1  idf.py -B build-v6 build

```

**Flash command (Window)**

```powershell

python -m esptool --chip esp32  -p COM24 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 2MB --flash_freq 40m 0x1000 build-v6\bootloader\bootloader.bin 0x8000 build-v6\partition_table\partition-table.bin 0x10000 build-v6\Lab9-1-SPI-OLED-Bringup.bin && idf monitor

```
