# Media transfer

The current player firmware mounts the SD card for playback and does not expose
it as USB mass storage or a network share.

## Recommended workflow

1. Insert the SD card in a PC card reader.
2. Run `scripts/sync-media.ps1 -Destination X:\`, replacing `X:` with the
   removable SD drive.
3. The script copies `.mjpeg` and `.wav` files from
   `videoConverter/output_sd`, then verifies SHA-256 for every destination file.
   It does not delete unrelated files.
4. Eject the card safely and return it to the device.

## Why upload through the device is not enabled

Serial upload would require a new binary protocol and exclusive SD ownership
during transfer. Large video sets would take many minutes at practical serial
rates. TCP upload can be faster, but requires Wi-Fi provisioning, authentication,
temporary files, checksums, and recovery from interrupted writes. USB mass
storage would also require the player to unmount the card while the PC owns it.

These methods are possible future features, but direct card-reader transfer is
currently faster and has fewer filesystem-corruption risks.
