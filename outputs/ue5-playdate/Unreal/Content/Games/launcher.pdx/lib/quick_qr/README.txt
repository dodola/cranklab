Fast QR Code Library for Playdate

This directory is for the playdate-qrcode C library.

To install:
1. git clone https://github.com/notpeter/playdate-qrcode.git temp-qrcode
2. cp temp-qrcode/src/* source/lib/quick_qr/
3. cp temp-qrcode/library/* source/lib/quick_qr/
4. rm -rf temp-qrcode

The library provides ~7ms QR generation vs ~1000ms with the SDK.

Without the library, the app falls back to SDK's generateQRCode().
