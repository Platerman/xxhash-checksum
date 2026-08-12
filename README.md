# xxhash-checksum

A fast xxHash64 checksum utility for generating and verifying file integrity manifests.

## Overview

`xxhash-checksum` creates `checksum.txt` manifest files containing xxHash64 hashes for individual files or entire directory trees. It also provides verification functionality to validate existing checksum manifests against the current filesystem state.

## Features

- **Single File Hashing** — Generate a `checksum.txt` for an individual file
- **Directory Tree Processing** — Recursively hash all files in a directory structure
- **Manifest Verification** — Validate existing `checksum.txt` files against current files
- **Symbolic Link Handling** — Automatically skips symbolic links
- **Cyclic Directory Detection** — Prevents infinite recursion in directory structures
- **XXHash3-64 Algorithm** — Fast, high-quality hashing suitable for integrity checking

## Installation

### Prerequisites

- GCC compiler
- xxHash library (included in the source)

### Build

```bash
gcc -Wall -O3 -march=native -std=c99 -D_POSIX_C_SOURCE=200809L -o xxhash src/main.c src/xxhash.c
```

Alternatively, use the provided Makefile:

```bash
make
```

## Usage

### Generate Checksum for a Single File

```bash
./xxhash /path/to/file
```

Creates `checksum.txt` in the same directory as the target file.

### Generate Checksums for a Directory

```bash
./xxhash /path/to/directory
```

Creates `checksum.txt` in the specified directory with hashes for all regular files (recursively).

### Verify an Existing Checksum Manifest

```bash
./xxhash -c /path/to/checksum.txt
```

Validates all files listed in the manifest against their current hashes and reports:
- ✅ Matches
- ❌ Mismatches
- ❓ Missing files or errors

## Output Format

The generated `checksum.txt` uses the standard format:

```
<hash_hex> *<relative_file_path>
```

Example:
```
6051aab43d316bc7 *src/main.c
04f4a20a3dbd30d4 *README.md
```

## Verification Output

During verification, each file is displayed with an indicator:

```
<< ✅ >> - src/main.c
<< ❌ >> - docs/README.md (checksum mismatch)
<< ❓ >> - missing/file.c (file not found)
```

## Return Codes

| Code | Meaning |
|------|---------|
| `0`  | Success (no mismatches or missing files during verification) |
| `1`  | Error occurred (verification failures, I/O errors, or invalid input) |

## Limitations

- Maximum directory depth: 1024 levels
- Maximum path length: 4096 characters
- Symbolic links are ignored during traversal
- Only regular files are hashed (directories, symlinks, and special files are skipped)

## Technical Notes

The utility uses the xxHash3-64 algorithm for fast, high-quality hashing. While suitable for integrity verification, it is **not cryptographically secure** and should not be used for security-critical applications.

## License

This project is licensed under the MIT License — see the [LICENSE](LICENSE) file for details.

## Third-Party Libraries

This project includes the xxHash library, which is distributed under the BSD 2-Clause License.
