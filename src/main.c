#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <libgen.h>
#include <unistd.h>
#include <limits.h>

#define XXH_STATIC_LINKING_ONLY
#include "xxhash.h"

// This code is a fucking disaster. Every function is a mess. Proceed with caution.
#define DIGEST_LENGTH 8   /* xxHash64 = 8 bytes */
#define MAX_DIR_DEPTH 1024

static struct { ino_t ino; dev_t dev; } visited_dirs[MAX_DIR_DEPTH];
static int visited_count = 0;
// Global state is evil, but who cares? This is a toy program.

#define BUF_SIZE (1024 * 1024)   /* 1 MB for fewer system calls */

static int compute_hash(const char *path, uint8_t *md) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }

    XXH3_state_t state;
    XXH3_64bits_reset(&state);

    uint8_t buf[BUF_SIZE];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        XXH3_64bits_update(&state, buf, n);

    if (ferror(f)) {
        fclose(f);
        perror(path);
        return -1;
    }
    fclose(f);

    XXH64_hash_t hash = XXH3_64bits_digest(&state);

    for (int i = 0; i < 8; i++)
        md[7 - i] = (uint8_t)((hash >> (i * 8)) & 0xFF);
    return 0;
}

static void hex_string(const uint8_t *md, char *out) {
    for (int i = 0; i < DIGEST_LENGTH; i++)
        snprintf(out + i*2, 3, "%02x", md[i]);
    out[2*DIGEST_LENGTH] = '\0';
}

static char *normalize_path(const char *path) {
    // normalize_path is a hack, but it's acceptable for this piece of shit.
    if (!path) return NULL;
    size_t len = strlen(path);
    char *out = malloc(len + 1);
    if (!out) return NULL;
    size_t j = 0;
    int last_slash = 0;
    for (size_t i = 0; i < len; i++) {
        if (path[i] == '/') {
            if (!last_slash) out[j++] = '/';
            last_slash = 1;
        } else {
            out[j++] = path[i];
            last_slash = 0;
        }
    }
    if (j > 1 && out[j-1] == '/') j--;
    out[j] = '\0';
    return out;
}

static int process_file(const char *filepath) {
    char *base = strdup(filepath);
    if (!base) { perror("strdup"); exit(1); }
    char *filename = strdup(basename(base));
    if (!filename) {
        perror("strdup");
        free(base);
        return 1;
    }
    if (strcmp(filename, "checksum.txt") == 0) {
        free(filename);
        free(base);
        return 0;
    }
    uint8_t md[DIGEST_LENGTH];
    if (compute_hash(filepath, md) != 0) {
        free(filename);
        free(base);
        return 1;
    }
    char hex[2*DIGEST_LENGTH+1];
    hex_string(md, hex);

    char *dirc = strdup(filepath);
    if (!dirc) { perror("strdup"); free(filename); free(base); return 1; }
    char *dir = dirname(dirc);
    char outpath[4096];
    int ret;
    if (strcmp(dir, "/") == 0) {
        ret = snprintf(outpath, sizeof(outpath), "/checksum.txt");
    } else {
        ret = snprintf(outpath, sizeof(outpath), "%s/checksum.txt", dir);
    }
    if (ret >= (int)sizeof(outpath)) {
        fprintf(stderr, "Path too long: %s/checksum.txt\n", dir);
        free(dirc);
        free(filename);
        free(base);
        return 1;
    }
    // What the fuck? This overwrites checksum.txt every time process_file is called.
    // If you have multiple files in same dir, this is retarded. Should be handled by caller.
    FILE *out = fopen(outpath, "w");
    if (!out) { perror(outpath); free(dirc); free(filename); free(base); return 1; }
    if (fprintf(out, "%s *%s\n", hex, filename) < 0) {
        perror("fprintf");
        fclose(out);
        free(dirc); free(filename); free(base);
        return 1;
    }
    fclose(out);
    free(dirc); free(filename); free(base);
    return 0;
}

static void walk_dir_recursive(const char *root, const char *current, FILE *out, unsigned int depth, int *write_err) {
    if (*write_err) return;
    if (depth > MAX_DIR_DEPTH) {
        fprintf(stderr, "Directory nesting too deep (exceeds %d): %s\n", MAX_DIR_DEPTH, current);
        return;
    }
    DIR *d = opendir(current);
    if (!d) { perror(current); return; }
    struct dirent *entry;
    char subpath[4096];
    while ((entry = readdir(d)) != NULL) {
        if (*write_err) break;
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        int ret = snprintf(subpath, sizeof(subpath), "%s/%s", current, entry->d_name);
        if (ret >= (int)sizeof(subpath)) {
            fprintf(stderr, "Path too long: %s/%s\n", current, entry->d_name);
            continue;
        }
        struct stat st;
        if (lstat(subpath, &st) == -1) { perror(subpath); continue; }
        if (S_ISLNK(st.st_mode)) continue;
        if (S_ISDIR(st.st_mode)) {
            // This cycle detection is a hacky piece of shit. Why not use a proper hash set?
            // Oh wait, we're in C and the author is lazy. Fuck it.
            {
                int cycle = 0;
                for (int i = 0; i < visited_count; i++) {
                    if (visited_dirs[i].ino == st.st_ino &&
                        visited_dirs[i].dev == st.st_dev) {
                        cycle = 1;
                    break;
                        }
                }
                if (!cycle) {
                    if (visited_count < MAX_DIR_DEPTH) {
                        visited_dirs[visited_count].ino = st.st_ino;
                        visited_dirs[visited_count].dev = st.st_dev;
                        visited_count++;
                        walk_dir_recursive(root, subpath, out, depth + 1, write_err);
                        visited_count--;
                    } else {
                        fprintf(stderr, "Warning: too many distinct directories, skipping: %s\n", subpath);
                    }
                }
            }
        } else if (S_ISREG(st.st_mode)) {
                if (strcmp(entry->d_name, "checksum.txt") == 0) continue;
                uint8_t md[DIGEST_LENGTH];
                if (compute_hash(subpath, md) != 0) continue;
                char hex[2*DIGEST_LENGTH+1];
                hex_string(md, hex);
                const char *rel = subpath + strlen(root);
                while (*rel == '/') rel++;
                if (fprintf(out, "%s *%s\n", hex, rel) < 0) {
                    perror("fprintf");
                    *write_err = 1;
                    closedir(d);
                    return;
                }
            }
        }
        closedir(d);
    }

    static void process_directory(const char *dirpath) {
        visited_count = 0;
        char *rawpath = strdup(dirpath);
        if (!rawpath) { perror("strdup"); exit(1); }
        char *cleanpath = normalize_path(rawpath);
        free(rawpath);
        if (!cleanpath) { perror("normalize_path"); exit(1); }
        char outpath[4096];
        int ret;
        if (strcmp(cleanpath, "/") == 0) {
            ret = snprintf(outpath, sizeof(outpath), "/checksum.txt");
        } else {
            ret = snprintf(outpath, sizeof(outpath), "%s/checksum.txt", cleanpath);
        }
        if (ret >= (int)sizeof(outpath)) {
            fprintf(stderr, "Path too long: %s/checksum.txt\n", cleanpath);
            free(cleanpath);
            exit(1);
        }
        FILE *out = fopen(outpath, "w");
        if (!out) { perror(outpath); free(cleanpath); exit(1); }
        int write_err = 0;
        walk_dir_recursive(cleanpath, cleanpath, out, 0, &write_err);
        if (ferror(out)) {
            perror("write error in checksum.txt");
            fclose(out);
            free(cleanpath);
            exit(1);
        }
        fclose(out);
        free(cleanpath);
    }

    static int verify_checksum_file(const char *checksum_path) {
        FILE *f = fopen(checksum_path, "r");
        if (!f) {
            perror(checksum_path);
            return 1;
        }
        // Get directory of checksum file
        char *dirc = strdup(checksum_path);
        if (!dirc) { perror("strdup"); fclose(f); return 1; }
        char *dir = strdup(dirname(dirc));
        if (!dir) {
            perror("strdup");
            fclose(f);
            free(dirc);
            return 1;
        }
        char line[4096];
        int total = 0, mismatches = 0, missing = 0;
        int io_error = 0;

        // This line parsing is a fucking disaster. Manual string handling is error‑prone.
        // Why not use sscanf? Too much effort? Fuck.
        while (1) {
            // Read a line, handling possible truncation
            if (!fgets(line, sizeof(line), f)) {
                if (ferror(f)) {
                    perror(checksum_path);
                    free(dir);
                    free(dirc);
                    fclose(f);
                    return 1;
                }
                break; // EOF
            }
            size_t len = strlen(line);
            // If line is too long (no newline and not EOF), discard the rest
            if (len == sizeof(line) - 1 && line[len-1] != '\n') {
                int c;
                while ((c = fgetc(f)) != '\n' && c != EOF) {}
                if (ferror(f)) {
                    perror("fgetc");
                    io_error = 1;
                    break;
                }
                fprintf(stderr, "Warning: line too long, skipped: %s...\n", line);
                continue;
            }
            // Remove trailing newline
            if (len > 0 && line[len-1] == '\n') line[--len] = '\0';
            // Skip empty lines
            if (len == 0) continue;
            // Expect at least 16 hex chars
            if (len < 16) {
                fprintf(stderr, "Invalid line (too short): %s\n", line);
                continue;
            }
            char hash_hex[17];
            strncpy(hash_hex, line, 16);
            hash_hex[16] = '\0';
            // Find filename after the hash: skip spaces and possible '*'
            const char *p = line + 16;
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '*') p++;
            while (*p == ' ' || *p == '\t') p++;
            const char *filename = p;
            if (*filename == '\0') {
                fprintf(stderr, "Invalid line (no filename): %s\n", line);
                continue;
            }
            // Build full path: if filename absolute, use it; else prepend dir
            char fullpath[4096];
            int ret;
            if (filename[0] == '/') {
                ret = snprintf(fullpath, sizeof(fullpath), "%s", filename);
            } else {
                ret = snprintf(fullpath, sizeof(fullpath), "%s/%s", dir, filename);
            }
            if (ret >= (int)sizeof(fullpath)) {
                fprintf(stderr, "Path too long: %s\n", filename);
                continue;
            }
            // Check if file exists and is regular
            struct stat st;
            if (stat(fullpath, &st) != 0) {
                printf("<< ❓ >> - %s (file not found)\n", filename);
                missing++;
                total++;
                continue;
            }
            if (!S_ISREG(st.st_mode)) {
                printf("<< ❓ >> - %s (not a regular file)\n", filename);
                missing++;
                total++;
                continue;
            }
            // Compute hash
            uint8_t md[DIGEST_LENGTH];
            if (compute_hash(fullpath, md) != 0) {
                printf("<< ❓ >> - %s (hash computation failed)\n", filename);
                missing++;
                total++;
                continue;
            }
            char computed_hex[17];
            hex_string(md, computed_hex);
            if (strcmp(computed_hex, hash_hex) == 0) {
                printf("<< ✅ >> - %s\n", filename);
            } else {
                printf("<< ❌ >> - %s (checksum mismatch)\n", filename);
                mismatches++;
            }
            total++;
        }
        free(dir);
        fclose(f);
        free(dirc);

        printf("\nSummary: %d files checked, %d mismatches, %d missing.\n", total, mismatches, missing);
        if (io_error) {
            return 1;
        }
        return (mismatches > 0 || missing > 0) ? 1 : 0;
    }

    static void print_usage(const char *prog) {
        fprintf(stderr,
                "Usage: %s <file|directory>          - generate checksum.txt using xxHash64\n"
                "       %s -c <checksum_file>       - verify checksums against the given file\n"
                "\n"
                "Examples:\n"
                "  %s /path/to/file                - creates /path/to/checksum.txt with that file's xxHash64\n"
                "  %s /path/to/dir                 - creates /path/to/dir/checksum.txt with all files' xxHash64\n"
                "  %s -c /path/to/checksum.txt     - verifies all files listed in checksum.txt\n",
                prog, prog, prog, prog, prog);
    }

    int main(int argc, char *argv[]) {
        // Command line parsing is a fucking disaster. Only supports two modes, but could be cleaner.
        // No arguments -> print usage
        if (argc == 1) {
            print_usage(argv[0]);
            return 0;
        }
        // Verify mode
        if (argc == 3 && strcmp(argv[1], "-c") == 0) {
            return verify_checksum_file(argv[2]);
        }
        if (argc == 2) {
            struct stat st;
            if (stat(argv[1], &st) == -1) { perror(argv[1]); return 1; }
            if (S_ISREG(st.st_mode)) return process_file(argv[1]);
            else if (S_ISDIR(st.st_mode)) { process_directory(argv[1]); return 0; }
            else { fprintf(stderr, "%s: not a regular file or directory\n", argv[1]); return 1; }
        }
        // Invalid arguments
        print_usage(argv[0]);
        return 1;
    }
