# Encrypted menu backgrounds

The optional `assets/client/backgrounds/menu-backgrounds.bspbg` stores the menu
backgrounds and parallax layers in an encrypted container. The frontend and its
preload workers authenticate it and decode PNGs in memory. No decrypted PNGs
are extracted. Static backgrounds, layered motion, cropping and transparency use
the existing scene catalog and rendering behavior.

The client prefers packed backgrounds when present. Without a valid pack, it
uses the existing loose artwork and retail fallback. The normal client-assets
staging, installer and asset-update component already carry this directory.
Restart the client after replacing a pack; loaded packs are immutable shared
data for the lifetime of their users.

Build and package locally:

```powershell
cmake --build --preset native-dev --target aos_pack_menu_backgrounds
./out/build/native-dev/src/RelWithDebInfo/aos_pack_menu_backgrounds.exe `
  ./assets/original ./assets/catalog/menu-backgrounds.json `
  ./assets/catalog/menu-background-scenes.json `
  ./assets/client/backgrounds/menu-backgrounds.bspbg
```

The packager verifies each source against its catalog SHA-256, writes the pack
atomically, then decrypts every entry and compares it byte-for-byte. The current
catalog contains 33 static backgrounds and 17 parallax layers. Source PNGs are
preserved.

Format version 1 has the eight-byte magic `BSPBGPK1`, a little-endian 32-bit entry
count, then records containing a 16-bit name length, 32-bit plaintext size, name,
24-byte nonce and XChaCha20-Poly1305 ciphertext with its 16-byte tag. Each name is
authenticated as associated data. Names are limited to PNGs below
`png/ui/menu_backgrounds/`; duplicates, unsafe paths, trailing data, truncated or
unauthenticated entries are rejected. Limits are 128 entries, 8 MiB per PNG and
128 MiB per container. The loader authenticates the whole pack before exposing
its optional scenes.

The embedded client key makes this storage obfuscation, not DRM or a way to
enforce confidentiality. Encryption does not change the artwork's copyright or
redistribution permissions. The current local pack contains retail-derived art;
its redistribution rights remain unresolved.
