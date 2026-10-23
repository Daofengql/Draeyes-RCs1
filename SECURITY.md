# Security Policy

## Supported Versions

Only the latest commit on `main` is supported.

## Reporting a Vulnerability

Please do not report security issues in public. Use GitHub's private vulnerability reporting (repository **Security → Report a vulnerability**) if available, or email `daofengqianlin@silverdragon.cn`.

Please include the affected commit, reproduction steps, and impact. We will acknowledge reports within 3 business days and coordinate disclosure after a fix is available.

## Device Notes

Draeyes RCs1 is an ESP-NOW controller. It does not join Wi-Fi networks or store Wi-Fi credentials. Pairing uses a random nonce and a broadcast peer address; the protocol does not perform a secret key exchange. Do not publish real device MAC addresses, pairing logs, or calibration dumps from your own hardware.
