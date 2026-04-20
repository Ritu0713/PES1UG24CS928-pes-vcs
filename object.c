// object.c — Content-addressable object store
//
// Every piece of data (file contents, directory listings, commits) is stored
// as an "object" named by its SHA-256 hash. Objects are stored under
// .pes/objects/XX/YYYYYY... where XX is the first two hex characters of the
// hash (directory sharding).
//
// PROVIDED functions: compute_hash, object_path, object_exists, hash_to_hex, hex_to_hash
// TODO functions: object_write, object_read

#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <openssl/sha.h>

// ─── PROVIDED ────────────────────────────────────────────────────────────────

void hash_to_hex(const ObjectID *id, char *hex_out) {
    for (int i = 0; i < HASH_SIZE; i++) {
        sprintf(hex_out + i * 2, "%02x", id->hash[i]);
    }
    hex_out[HASH_HEX_SIZE] = '\0';
}

int hex_to_hash(const char *hex, ObjectID *id_out) {
    if (strlen(hex) < HASH_HEX_SIZE) return -1;
    for (int i = 0; i < HASH_SIZE; i++) {
        unsigned int byte;
        if (sscanf(hex + i * 2, "%2x", &byte) != 1) return -1;
        id_out->hash[i] = (uint8_t)byte;
    }
    return 0;
}

void compute_hash(const void *data, size_t len, ObjectID *id_out) {
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, data, len);
    SHA256_Final(id_out->hash, &ctx);
}

// Get the filesystem path where an object should be stored.
// Format: .pes/objects/XX/YYYYYYYY...
// The first 2 hex chars form the shard directory; the rest is the filename.
void object_path(const ObjectID *id, char *path_out, size_t path_size) {
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id, hex);
    snprintf(path_out, path_size, "%s/%.2s/%s", OBJECTS_DIR, hex, hex + 2);
}

int object_exists(const ObjectID *id) {
    char path[512];
    object_path(id, path, sizeof(path));
    return access(path, F_OK) == 0;
}

// ─── IMPLEMENTED ─────────────────────────────────────────────────────────────

// Write an object to the store.
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out) {
    // Step 1: Build the header string: "blob <size>\0", "tree <size>\0", "commit <size>\0"
    const char *type_str;
    if      (type == OBJ_BLOB)   type_str = "blob";
    else if (type == OBJ_TREE)   type_str = "tree";
    else if (type == OBJ_COMMIT) type_str = "commit";
    else return -1;

    char header[64];
    int header_len = snprintf(header, sizeof(header), "%s %zu", type_str, len);
    // header_len does NOT include the null byte; but we need the null byte in the object
    size_t full_len = (size_t)header_len + 1 + len; // +1 for '\0' after header

    uint8_t *full = malloc(full_len);
    if (!full) return -1;

    memcpy(full, header, (size_t)header_len);
    full[header_len] = '\0';                        // the null byte separator
    memcpy(full + header_len + 1, data, len);       // actual data

    // Step 2: Compute SHA-256 of the full object (header + data)
    compute_hash(full, full_len, id_out);

    // Step 3: Deduplication — if object already exists, skip writing
    if (object_exists(id_out)) {
        free(full);
        return 0;
    }

    // Step 4: Create shard directory (.pes/objects/XX/)
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id_out, hex);

    char shard_dir[512];
    snprintf(shard_dir, sizeof(shard_dir), "%s/%.2s", OBJECTS_DIR, hex);
    mkdir(shard_dir, 0755); // ignore error — may already exist

    // Step 5: Build final path and a temp path in same directory
    char final_path[512];
    object_path(id_out, final_path, sizeof(final_path));

    char tmp_path[520];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", final_path);

    // Step 6: Write to temp file
    int fd = open(tmp_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) { free(full); return -1; }

    size_t written = 0;
    while (written < full_len) {
        ssize_t n = write(fd, (const uint8_t*)full + written, full_len - written);
        if (n < 0) { close(fd); free(full); return -1; }
        written += (size_t)n;
    }

    // Step 7: fsync to ensure data reaches disk
    fsync(fd);
    close(fd);
    free(full);

    // Step 8: Atomic rename to final path
    if (rename(tmp_path, final_path) != 0) return -1;

    // Step 9: fsync the shard directory to persist the rename
    int dir_fd = open(shard_dir, O_RDONLY);
    if (dir_fd >= 0) {
        fsync(dir_fd);
        close(dir_fd);
    }

    return 0;
}

// Read an object from the store.
int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out) {
    // Step 1: Get the file path
    char path[512];
    object_path(id, path, sizeof(path));

    // Step 2: Open and read the entire file
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size < 0) { fclose(f); return -1; }

    uint8_t *buf = malloc((size_t)file_size);
    if (!buf) { fclose(f); return -1; }

    if (fread(buf, 1, (size_t)file_size, f) != (size_t)file_size) {
        fclose(f); free(buf); return -1;
    }
    fclose(f);

    // Step 3: Verify integrity — recompute hash of file contents
    ObjectID computed;
    compute_hash(buf, (size_t)file_size, &computed);
    if (memcmp(computed.hash, id->hash, HASH_SIZE) != 0) {
        free(buf);
        return -1; // Corrupted object
    }

    // Step 4: Parse the header — find the '\0' that separates header from data
    uint8_t *null_pos = memchr(buf, '\0', (size_t)file_size);
    if (!null_pos) { free(buf); return -1; }

    // Parse type string
    if (strncmp((char*)buf, "blob ", 5) == 0)        *type_out = OBJ_BLOB;
    else if (strncmp((char*)buf, "tree ", 5) == 0)   *type_out = OBJ_TREE;
    else if (strncmp((char*)buf, "commit ", 7) == 0) *type_out = OBJ_COMMIT;
    else { free(buf); return -1; }

    // Step 5: Extract the data portion (after the '\0')
    uint8_t *data_start = null_pos + 1;
    size_t data_len = (size_t)((buf + file_size) - data_start);

    // Step 6: Allocate and return a copy of just the data portion
    uint8_t *out = malloc(data_len + 1); // +1 for safety null terminator
    if (!out) { free(buf); return -1; }
    memcpy(out, data_start, data_len);
    out[data_len] = '\0';

    *data_out = out;
    *len_out  = data_len;

    free(buf);
    return 0;
}
