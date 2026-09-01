// SPDX-License-Identifier: GPL-3.0-or-later
#define _POSIX_C_SOURCE 200809L

#include <synapse/core.h>

#include "settings_internal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef SYNAPSE_SETTINGS_VERSION
#define SYNAPSE_SETTINGS_VERSION "0.9.0-alpha.1"
#endif

typedef struct {
    char *id;
    char *type;
    char *version;
} layer_record;

typedef struct {
    layer_record *items;
    size_t count;
    size_t capacity;
} layer_list;

typedef struct {
    char *layer;
    char *package;
} layer_package_record;

typedef struct {
    layer_package_record *items;
    size_t count;
    size_t capacity;
} layer_package_list;

typedef struct {
    char *name;
    char *version;
} package_record;

typedef struct {
    package_record *items;
    size_t count;
    size_t capacity;
} package_list;

typedef struct {
    char *name;
    char *image;
    char *status;
} container_record;

typedef struct {
    container_record *items;
    size_t count;
    size_t capacity;
    int available;
    const char *reason;
} container_list;

static char *copy_string(const char *text) {
    const char *value = text ? text : "";
    if (strlen(value) > SETTINGS_FIELD_LIMIT) {
        errno = E2BIG;
        return NULL;
    }
    char *copy = strdup(value);
    if (!copy) perror("strdup");
    return copy;
}

static int grow_array(void **items, size_t *capacity, size_t count, size_t item_size) {
    if (count >= SETTINGS_RECORD_LIMIT) {
        errno = E2BIG;
        return -1;
    }
    if (count < *capacity) return 0;
    size_t next = *capacity ? *capacity * 2 : 32;
    if (next > SIZE_MAX / item_size) {
        errno = ENOMEM;
        return -1;
    }
    void *grown = realloc(*items, next * item_size);
    if (!grown) return -1;
    *items = grown;
    *capacity = next;
    return 0;
}

static int push_layer(layer_list *list, const char *id, const char *type, const char *version) {
    if (grow_array((void **)&list->items, &list->capacity, list->count, sizeof(*list->items)) != 0) return -1;
    layer_record item = { copy_string(id), copy_string(type), copy_string(version) };
    if (!item.id || !item.type || !item.version) {
        free(item.id); free(item.type); free(item.version);
        return -1;
    }
    list->items[list->count++] = item;
    return 0;
}

static int push_layer_package(layer_package_list *list, const char *layer, const char *package) {
    if (grow_array((void **)&list->items, &list->capacity, list->count, sizeof(*list->items)) != 0) return -1;
    layer_package_record item = { copy_string(layer), copy_string(package) };
    if (!item.layer || !item.package) {
        free(item.layer); free(item.package);
        return -1;
    }
    list->items[list->count++] = item;
    return 0;
}

static int push_package(package_list *list, const char *name, const char *version) {
    if (grow_array((void **)&list->items, &list->capacity, list->count, sizeof(*list->items)) != 0) return -1;
    package_record item = { copy_string(name), copy_string(version) };
    if (!item.name || !item.version) {
        free(item.name); free(item.version);
        return -1;
    }
    list->items[list->count++] = item;
    return 0;
}

static int push_container(container_list *list, const char *name, const char *image, const char *status) {
    if (grow_array((void **)&list->items, &list->capacity, list->count, sizeof(*list->items)) != 0) return -1;
    container_record item = { copy_string(name), copy_string(image), copy_string(status) };
    if (!item.name || !item.image || !item.status) {
        free(item.name); free(item.image); free(item.status);
        return -1;
    }
    list->items[list->count++] = item;
    return 0;
}

static void free_layers(layer_list *list) {
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i].id); free(list->items[i].type); free(list->items[i].version);
    }
    free(list->items);
}

static void free_layer_packages(layer_package_list *list) {
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i].layer); free(list->items[i].package);
    }
    free(list->items);
}

static void free_packages(package_list *list) {
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i].name); free(list->items[i].version);
    }
    free(list->items);
}

static void free_containers(container_list *list) {
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i].name); free(list->items[i].image); free(list->items[i].status);
    }
    free(list->items);
}

static const char *composition_dir(void) {
    const char *override = getenv("SYNAPSE_COMPOSITION_DIR");
    return override && *override ? override : "/usr/lib/synapse/composition";
}

static int composition_path(char *buffer, size_t size, const char *name) {
    int written = snprintf(buffer, size, "%s/%s", composition_dir(), name);
    if (written < 0 || (size_t)written >= size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

static int load_layers(layer_list *layers) {
    char path[SYNAPSE_PATH_MAX];
    if (composition_path(path, sizeof(path), "layers.tsv") != 0) return -1;
    FILE *file = fopen(path, "r");
    if (!file) return -1;
    char *line = NULL;
    size_t capacity = 0;
    int first = 1;
    int status = 0;
    while (getline(&line, &capacity, file) >= 0) {
        if (strlen(line) > SETTINGS_LINE_LIMIT) { status = -1; errno = E2BIG; break; }
        char *text = synapse_trim(line);
        if (first) { first = 0; continue; }
        if (!*text) continue;
        char *id = text;
        char *type = strchr(id, '\t');
        if (!type) continue;
        *type++ = '\0';
        char *version = strchr(type, '\t');
        if (!version) continue;
        *version++ = '\0';
        if (push_layer(layers, id, type, synapse_trim(version)) != 0) { status = -1; break; }
    }
    free(line);
    fclose(file);
    return status;
}

static int load_layer_package_map(layer_package_list *packages) {
    char path[SYNAPSE_PATH_MAX];
    if (composition_path(path, sizeof(path), "layer-packages.tsv") != 0) return -1;
    FILE *file = fopen(path, "r");
    if (!file) return -1;
    char *line = NULL;
    size_t capacity = 0;
    int first = 1;
    int status = 0;
    while (getline(&line, &capacity, file) >= 0) {
        if (strlen(line) > SETTINGS_LINE_LIMIT) { status = -1; errno = E2BIG; break; }
        char *text = synapse_trim(line);
        if (first) { first = 0; continue; }
        if (!*text) continue;
        char *package = strchr(text, '\t');
        if (!package) continue;
        *package++ = '\0';
        if (push_layer_package(packages, text, synapse_trim(package)) != 0) { status = -1; break; }
    }
    free(line);
    fclose(file);
    return status;
}

static int load_layered_packages(package_list *packages) {
    char path[SYNAPSE_PATH_MAX];
    if (composition_path(path, sizeof(path), "package-lock.tsv") != 0) return -1;
    FILE *file = fopen(path, "r");
    if (!file) return -1;
    char *line = NULL;
    size_t capacity = 0;
    int status = 0;
    while (getline(&line, &capacity, file) >= 0) {
        if (strlen(line) > SETTINGS_LINE_LIMIT) { status = -1; errno = E2BIG; break; }
        char *text = synapse_trim(line);
        if (!*text) continue;
        char *separator = strchr(text, ' ');
        if (!separator) separator = strchr(text, '\t');
        if (separator) *separator++ = '\0';
        if (push_package(packages, text, separator ? synapse_trim(separator) : "") != 0) { status = -1; break; }
    }
    free(line);
    fclose(file);
    return status;
}

static int desc_value(char *data, const char *field, char **result) {
    size_t field_length = strlen(field);
    char *cursor = data;
    while (cursor && *cursor) {
        char *line_end = strchr(cursor, '\n');
        if (line_end) *line_end = '\0';
        if (strlen(cursor) == field_length + 2 && cursor[0] == '%' && cursor[field_length + 1] == '%'
            && strncmp(cursor + 1, field, field_length) == 0) {
            if (!line_end || !line_end[1]) return -1;
            char *value = line_end + 1;
            char *value_end = strchr(value, '\n');
            if (value_end) *value_end = '\0';
            *result = value;
            return 0;
        }
        cursor = line_end ? line_end + 1 : NULL;
    }
    return -1;
}

static int load_installed_packages(package_list *packages) {
    const char *root = getenv("SYNAPSE_PACMAN_LOCAL");
    if (!root || !*root) root = "/var/lib/pacman/local";
    DIR *stream = opendir(root);
    if (!stream) return -1;
    struct dirent *entry;
    int status = 0;
    while ((entry = readdir(stream))) {
        if (entry->d_name[0] == '.') continue;
        char path[SYNAPSE_PATH_MAX];
        int written = snprintf(path, sizeof(path), "%s/%s/desc", root, entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(path)) continue;
        size_t size = 0;
        char *data = synapse_read_file(path, 128U * 1024U, &size);
        if (!data) continue;
        char *name = NULL;
        char *version = NULL;
        if (desc_value(data, "NAME", &name) == 0) {
            char *data_for_version = synapse_read_file(path, 128U * 1024U, NULL);
            if (data_for_version) {
                if (desc_value(data_for_version, "VERSION", &version) == 0
                    && push_package(packages, name, version) != 0) status = -1;
                free(data_for_version);
            }
        }
        free(data);
        if (status != 0) break;
    }
    closedir(stream);
    return status;
}

static int package_compare(const void *left, const void *right) {
    const package_record *a = left;
    const package_record *b = right;
    return synapse_ascii_casecmp(a->name, b->name);
}

static int package_present(const package_list *packages, const char *name) {
    size_t low = 0;
    size_t high = packages->count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        int compared = synapse_ascii_casecmp(packages->items[middle].name, name);
        if (compared == 0) return 1;
        if (compared < 0) low = middle + 1;
        else high = middle;
    }
    return 0;
}

static const char *package_version(const package_list *packages, const char *name) {
    size_t low = 0;
    size_t high = packages->count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        int compared = synapse_ascii_casecmp(packages->items[middle].name, name);
        if (compared == 0) return packages->items[middle].version;
        if (compared < 0) low = middle + 1;
        else high = middle;
    }
    return "";
}

static size_t layer_package_count(const layer_package_list *packages, const char *layer) {
    size_t count = 0;
    for (size_t i = 0; i < packages->count; i++) if (strcmp(packages->items[i].layer, layer) == 0) count++;
    return count;
}

static int build_unlayered(const package_list *installed, package_list *layered, package_list *unlayered) {
    if (layered->count > 1U) qsort(layered->items, layered->count, sizeof(*layered->items), package_compare);
    for (size_t i = 0; i < installed->count; i++) {
        if (!package_present(layered, installed->items[i].name)
            && push_package(unlayered, installed->items[i].name, installed->items[i].version) != 0) return -1;
    }
    if (unlayered->count > 1U) qsort(unlayered->items, unlayered->count, sizeof(*unlayered->items), package_compare);
    return 0;
}

static int executable_available(const char *path) {
    if (strchr(path, '/')) return access(path, X_OK) == 0;
    const char *path_env = getenv("PATH");
    if (!path_env) return 0;
    char *copy = copy_string(path_env);
    if (!copy) return 0;
    int found = 0;
    char *save = NULL;
    for (char *directory = strtok_r(copy, ":", &save); directory; directory = strtok_r(NULL, ":", &save)) {
        char candidate[SYNAPSE_PATH_MAX];
        int written = snprintf(candidate, sizeof(candidate), "%s/%s", *directory ? directory : ".", path);
        if (written > 0 && (size_t)written < sizeof(candidate) && access(candidate, X_OK) == 0) { found = 1; break; }
    }
    free(copy);
    return found;
}

static char *capture_docker(const char *binary, int *exit_status) {
    int pipefd[2];
    if (pipe(pipefd) != 0) return NULL;
    pid_t child = fork();
    if (child < 0) { close(pipefd[0]); close(pipefd[1]); return NULL; }
    if (child == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        int null_fd = open("/dev/null", O_WRONLY);
        if (null_fd >= 0) { dup2(null_fd, STDERR_FILENO); close(null_fd); }
        close(pipefd[0]); close(pipefd[1]);
        execlp(binary, binary, "ps", "--format", "{{.Names}}\t{{.Image}}\t{{.Status}}", (char *)NULL);
        _exit(127);
    }
    close(pipefd[1]);
    fcntl(pipefd[0], F_SETFL, fcntl(pipefd[0], F_GETFL) | O_NONBLOCK);
    size_t capacity = 4096;
    size_t used = 0;
    char *output = malloc(capacity + 1);
    if (!output) { close(pipefd[0]); kill(child, SIGKILL); waitpid(child, NULL, 0); return NULL; }

    int elapsed = 0;
    while (elapsed < 2000 && used < 1024U * 1024U) {
        struct pollfd descriptor = { pipefd[0], POLLIN | POLLHUP, 0 };
        int ready = poll(&descriptor, 1, 100);
        elapsed += 100;
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) break;
        if (descriptor.revents & POLLIN) {
            if (used == capacity) {
                size_t next = capacity * 2;
                if (next > 1024U * 1024U) next = 1024U * 1024U;
                char *grown = realloc(output, next + 1);
                if (!grown) break;
                output = grown; capacity = next;
            }
            ssize_t count = read(pipefd[0], output + used, capacity - used);
            if (count > 0) used += (size_t)count;
        }
        if (descriptor.revents & POLLHUP) break;
    }
    close(pipefd[0]);

    int status = 0;
    pid_t waited = waitpid(child, &status, WNOHANG);
    while (waited == 0 && elapsed < 2000) {
        struct timespec delay = {0, 50 * 1000 * 1000};
        nanosleep(&delay, NULL);
        elapsed += 50;
        waited = waitpid(child, &status, WNOHANG);
    }
    if (waited == 0) {
        kill(child, SIGKILL);
        waitpid(child, &status, 0);
        *exit_status = 124;
    } else if (waited < 0 || !WIFEXITED(status)) {
        *exit_status = 1;
    } else {
        *exit_status = WEXITSTATUS(status);
    }
    output[used] = '\0';
    return output;
}

static void load_containers(container_list *containers) {
    const char *binary = getenv("SYNAPSE_DOCKER");
    if (!binary || !*binary) binary = "docker";
    if (!executable_available(binary)) {
        containers->available = 0;
        containers->reason = "not-installed";
        return;
    }
    int status = 1;
    char *output = capture_docker(binary, &status);
    if (!output || status != 0) {
        containers->available = 0;
        containers->reason = status == 124 ? "timeout" : "unavailable";
        free(output);
        return;
    }
    containers->available = 1;
    containers->reason = "";
    char *save = NULL;
    for (char *line = strtok_r(output, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *name = line;
        char *image = strchr(name, '\t');
        if (!image) continue;
        *image++ = '\0';
        char *state = strchr(image, '\t');
        if (!state) continue;
        *state++ = '\0';
        if (push_container(containers, name, image, state) != 0) break;
    }
    free(output);
}

static void print_package_json(const package_record *package) {
    fputs("{\"name\":", stdout); synapse_json_string(stdout, package->name);
    fputs(",\"version\":", stdout); synapse_json_string(stdout, package->version);
    fputc('}', stdout);
}

static void print_layers_json(const layer_list *layers, const layer_package_list *layer_packages,
                              const package_list *installed, const package_list *layered,
                              const package_list *unlayered, const container_list *containers) {
    fputs("{\"schema\":\"synapse.settings.layers/v2\",\"section\":{\"id\":\"layers\",\"title\":\"Layers\",\"icon\":\"layers\"},\"semanticLayerCount\":", stdout);
    fprintf(stdout, "%zu,\"layers\":[", layers->count);
    for (size_t i = 0; i < layers->count; i++) {
        if (i) fputc(',', stdout);
        const layer_record *layer = &layers->items[i];
        fputs("{\"id\":", stdout); synapse_json_string(stdout, layer->id);
        fputs(",\"title\":", stdout); synapse_json_string(stdout, layer->id);
        fputs(",\"type\":", stdout); synapse_json_string(stdout, layer->type);
        fputs(",\"version\":", stdout); synapse_json_string(stdout, layer->version);
        fprintf(stdout, ",\"generated\":false,\"packageCount\":%zu,\"packages\":[",
                layer_package_count(layer_packages, layer->id));
        size_t emitted = 0;
        for (size_t j = 0; j < layer_packages->count; j++) {
            if (strcmp(layer_packages->items[j].layer, layer->id) != 0) continue;
            if (emitted++) fputc(',', stdout);
            package_record package = {
                layer_packages->items[j].package,
                (char *)package_version(layered, layer_packages->items[j].package)
            };
            print_package_json(&package);
        }
        fputs("]}", stdout);
    }
    if (layers->count) fputc(',', stdout);
    fprintf(stdout, "{\"id\":\"unlayered\",\"title\":\"Non layerizzato\",\"type\":\"live-drift\",\"version\":\"live\",\"generated\":true,\"packageCount\":%zu,\"packages\":[", unlayered->count);
    for (size_t i = 0; i < unlayered->count; i++) {
        if (i) fputc(',', stdout);
        print_package_json(&unlayered->items[i]);
    }
    fprintf(stdout, "]}],\"packages\":{\"backend\":\"pacman\",\"installedCount\":%zu,\"factoryLockedCount\":%zu,\"unlayeredCount\":%zu}",
            installed->count, layered->count, unlayered->count);
    fputs(",\"docker\":{\"available\":", stdout); fputs(containers->available ? "true" : "false", stdout);
    fputs(",\"reason\":", stdout); synapse_json_string(stdout, containers->reason);
    fputs(",\"active\":[", stdout);
    for (size_t i = 0; i < containers->count; i++) {
        if (i) fputc(',', stdout);
        fputs("{\"name\":", stdout); synapse_json_string(stdout, containers->items[i].name);
        fputs(",\"image\":", stdout); synapse_json_string(stdout, containers->items[i].image);
        fputs(",\"status\":", stdout); synapse_json_string(stdout, containers->items[i].status);
        fputc('}', stdout);
    }
    fprintf(stdout, "],\"activeCount\":%zu}}\n", containers->count);
}

static void print_layers_text(const layer_list *layers, const layer_package_list *layer_packages,
                              const package_list *installed, const package_list *layered,
                              const package_list *unlayered, const container_list *containers) {
    puts("Layers navigabili");
    for (size_t i = 0; i < layers->count; i++) {
        const layer_record *layer = &layers->items[i];
        printf("\n%s  %s  %s\n", layer->id, layer->type, layer->version);
        for (size_t j = 0; j < layer_packages->count; j++) {
            if (strcmp(layer_packages->items[j].layer, layer->id) == 0)
                printf("  %s %s\n", layer_packages->items[j].package,
                       package_version(layered, layer_packages->items[j].package));
        }
    }
    printf("\nNon layerizzato  live-drift  live\n");
    for (size_t i = 0; i < unlayered->count; i++) printf("  %s %s\n", unlayered->items[i].name, unlayered->items[i].version);
    printf("\nPacchetti: %zu installati, %zu nel lock di fabbrica, %zu non layerizzati\n", installed->count, layered->count, unlayered->count);
    if (!containers->available) printf("\nDocker: %s\n", containers->reason);
    else {
        printf("\nContainer Docker attivi: %zu\n", containers->count);
        for (size_t i = 0; i < containers->count; i++) printf("  %s  %s  %s\n", containers->items[i].name, containers->items[i].image, containers->items[i].status);
    }
}

static void usage(FILE *out) {
    fputs("Usage:\n"
          "  synapse-settings sections [--format text|json]\n"
          "  synapse-settings layers [--format text|json]\n"
          "  synapse-settings audio inventory [--format text|json]\n"
          "  synapse-settings audio broker-status [--format text|json]\n"
          "  synapse-settings audio goxlr-status [--format text|json]\n"
          "  synapse-settings audio plan-default --direction output|input --device ID [--format text|json]\n"
          "  synapse-settings audio set-default --direction output|input --device ID --ack synapse-settings/audio-default/v1 [--format text|json]\n"
          "  synapse-settings audio plan-volume --target ID --percent 0..100 [--format text|json]\n"
          "  synapse-settings audio set-volume --target ID --from-percent 0..999 --percent 0..100 --cohort ID --ack synapse-settings/audio-control/v1 [--format text|json]\n"
          "  synapse-settings audio plan-mute --target ID --muted true|false [--format text|json]\n"
          "  synapse-settings audio set-mute --target ID --from-muted true|false --muted true|false --cohort ID --ack synapse-settings/audio-control/v1 [--format text|json]\n"
          "  synapse-settings audio policy show [--format text|json]\n"
          "  synapse-settings audio policy set-rule --match executable|directory --path PATH --direction output|input --device ID --ack synapse-settings/audio-route-policy/v1 [--format text|json]\n"
          "  synapse-settings audio policy set-process-rule --stream ID --device ID --ack synapse-settings/audio-route-policy/v1 [--format text|json]\n"
          "  synapse-settings audio policy remove-rule --rule ID --ack synapse-settings/audio-route-policy/v1 [--format text|json]\n"
          "  synapse-settings audio resolve --path EXECUTABLE --direction output|input [--format text|json]\n"
          "  synapse-settings --version\n", out);
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        puts("synapse-settings " SYNAPSE_SETTINGS_VERSION);
        return 0;
    }
    if (argc < 2 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        usage(argc < 2 ? stderr : stdout);
        return argc < 2 ? 2 : 0;
    }
    if (strcmp(argv[1], "audio") == 0) return settings_audio_command(argc - 1, argv + 1);

    const char *format = "text";
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--format") == 0 && i + 1 < argc) format = argv[++i];
        else if (strcmp(argv[i], "--json") == 0) format = "json";
        else { usage(stderr); return 2; }
    }
    if (strcmp(format, "text") != 0 && strcmp(format, "json") != 0) {
        fputs("synapse-settings: format must be text or json\n", stderr);
        return 2;
    }
    int json = strcmp(format, "json") == 0;
    if (strcmp(argv[1], "sections") == 0) {
        if (json) puts("{\"schema\":\"synapse.settings.sections/v2\",\"sections\":[{\"id\":\"layers\",\"title\":\"Layers\",\"icon\":\"layers\",\"available\":true,\"lazy\":true},{\"id\":\"audio\",\"title\":\"Audio\",\"icon\":\"audio-card\",\"available\":true,\"lazy\":true},{\"id\":\"input\",\"title\":\"Input\",\"icon\":\"input-keyboard\",\"available\":true,\"lazy\":true},{\"id\":\"themes\",\"title\":\"Template\",\"icon\":\"preferences-desktop-wallpaper\",\"available\":true,\"lazy\":true}]}");
        else puts("layers\tLayers\tlayers\tavailable\naudio\tAudio\taudio-card\tavailable\ninput\tInput\tinput-keyboard\tavailable\nthemes\tTemplate\tpreferences-desktop-wallpaper\tavailable");
        return 0;
    }
    if (strcmp(argv[1], "layers") != 0) { usage(stderr); return 2; }

    layer_list layers = {0};
    layer_package_list layer_packages = {0};
    package_list installed = {0};
    package_list layered = {0};
    package_list unlayered = {0};
    container_list containers = {0};
    if (load_layers(&layers) != 0 || load_layer_package_map(&layer_packages) != 0
        || load_layered_packages(&layered) != 0 || load_installed_packages(&installed) != 0
        || build_unlayered(&installed, &layered, &unlayered) != 0) {
        perror("synapse-settings: layers");
        free_layers(&layers); free_layer_packages(&layer_packages); free_packages(&installed);
        free_packages(&layered); free_packages(&unlayered);
        return 1;
    }
    load_containers(&containers);
    if (json) print_layers_json(&layers, &layer_packages, &installed, &layered, &unlayered, &containers);
    else print_layers_text(&layers, &layer_packages, &installed, &layered, &unlayered, &containers);
    free_layers(&layers); free_layer_packages(&layer_packages); free_packages(&installed);
    free_packages(&layered); free_packages(&unlayered); free_containers(&containers);
    return 0;
}
