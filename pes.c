#include "pes.h"
#include "index.h"
#include "commit.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Forward declarations for functions implemented in other files
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);
int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out);

// ─── PROVIDED: Phase 5 Command Wrappers ─────────────────────────────────────

void cmd_branch(int argc, char *argv[]) {
    (void)argc; (void)argv;
    fprintf(stderr, "branch: not implemented (Phase 5)\n");
}

void cmd_checkout(int argc, char *argv[]) {
    (void)argc; (void)argv;
    fprintf(stderr, "checkout: not implemented (Phase 5)\n");
}

// ─── PROVIDED: init ──────────────────────────────────────────────────────────

void cmd_init(void) {
    // Create the .pes directory structure
    mkdir(PES_DIR,      0755);
    mkdir(OBJECTS_DIR,  0755);
    mkdir(REFS_DIR,     0755);

    // Create .pes/HEAD pointing to main branch
    FILE *f = fopen(HEAD_FILE, "w");
    if (f) {
        fprintf(f, "ref: refs/heads/main\n");
        fclose(f);
    }

    printf("Initialized empty PES repository in %s/\n", PES_DIR);
}

// ─── PROVIDED: add ───────────────────────────────────────────────────────────

void cmd_add(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: pes add <file>...\n");
        return;
    }

    Index index;
    index.count = 0;
    index_load(&index);

    for (int i = 2; i < argc; i++) {
        if (index_add(&index, argv[i]) == 0) {
            printf("added: %s\n", argv[i]);
        } else {
            fprintf(stderr, "error: failed to add '%s'\n", argv[i]);
        }
    }
}

// ─── PROVIDED: status ────────────────────────────────────────────────────────

void cmd_status(void) {
    Index index;
    index.count = 0;
    index_load(&index);
    index_status(&index);
}

// ─── IMPLEMENTED: commit ─────────────────────────────────────────────────────

void cmd_commit(int argc, char *argv[]) {
    // Parse -m <message> from arguments
    const char *message = NULL;
    for (int i = 2; i < argc - 1; i++) {
        if (strcmp(argv[i], "-m") == 0) {
            message = argv[i + 1];
            break;
        }
    }

    if (!message) {
        fprintf(stderr, "error: commit requires a message (-m \"message\")\n");
        return;
    }

    ObjectID commit_id;
    if (commit_create(message, &commit_id) != 0) {
        fprintf(stderr, "error: commit failed\n");
        return;
    }

    // Print first 12 hex chars of the commit hash
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(&commit_id, hex);
    hex[12] = '\0'; // truncate to 12 chars for display
    printf("Committed: %s... %s\n", hex, message);
}

// ─── PROVIDED: log ───────────────────────────────────────────────────────────

static void print_commit(const ObjectID *id, const Commit *c, void *ctx) {
    (void)ctx;
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id, hex);
    printf("commit %s\n", hex);
    printf("Author: %s\n", c->author);
    printf("Date:   %llu\n", (unsigned long long)c->timestamp);
    printf("\n    %s\n\n", c->message);
}

void cmd_log(void) {
    if (commit_walk(print_commit, NULL) != 0) {
        fprintf(stderr, "error: no commits yet\n");
    }
}

// ─── PROVIDED: Command dispatch ─────────────────────────────────────────────

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: pes <command> [args]\n");
        fprintf(stderr, "\nCommands:\n");
        fprintf(stderr, "  init              Create a new PES repository\n");
        fprintf(stderr, "  add <file>...     Stage files for commit\n");
        fprintf(stderr, "  status            Show working directory status\n");
        fprintf(stderr, "  commit -m <msg>   Create a commit from staged files\n");
        fprintf(stderr, "  log               Show commit history\n");
        fprintf(stderr, "  branch            List, create, or delete branches\n");
        fprintf(stderr, "  checkout <ref>    Switch branches or restore working tree\n");
        return 1;
    }

    const char *cmd = argv[1];

    if      (strcmp(cmd, "init")     == 0) cmd_init();
    else if (strcmp(cmd, "add")      == 0) cmd_add(argc, argv);
    else if (strcmp(cmd, "status")   == 0) cmd_status();
    else if (strcmp(cmd, "commit")   == 0) cmd_commit(argc, argv);
    else if (strcmp(cmd, "log")      == 0) cmd_log();
    else if (strcmp(cmd, "branch")   == 0) cmd_branch(argc, argv);
    else if (strcmp(cmd, "checkout") == 0) cmd_checkout(argc, argv);
    else {
        fprintf(stderr, "Unknown command: %s\n", cmd);
        fprintf(stderr, "Run 'pes' with no arguments for usage.\n");
        return 1;
    }

    return 0;
}
