#include "common.h"

#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

char *prism_read_file(const char *path) {
    gchar *contents = NULL;
    gsize length = 0;
    if (!g_file_get_contents(path, &contents, &length, NULL)) {
        return NULL;
    }
    if (length == 0) {
        g_free(contents);
        return NULL;
    }
    return contents;
}

char *prism_read_first_line(const char *path) {
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return NULL;
    }
    char buffer[512];
    char *line = fgets(buffer, sizeof(buffer), file);
    fclose(file);
    if (line == NULL) {
        return NULL;
    }
    prism_trim(line);
    return g_strdup(line);
}

void prism_trim(char *text) {
    if (text == NULL) {
        return;
    }
    char *start = text;
    while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') {
        start++;
    }
    if (start != text) {
        memmove(text, start, strlen(start) + 1);
    }
    size_t len = strlen(text);
    while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\t' ||
                       text[len - 1] == '\n' || text[len - 1] == '\r')) {
        text[--len] = '\0';
    }
}

const char *prism_basename(const char *path) {
    if (path == NULL) {
        return "";
    }
    const char *slash = strrchr(path, '/');
    return slash == NULL ? path : slash + 1;
}

void prism_username_for_uid(unsigned int uid, char *out, size_t out_len) {
    struct passwd pwd;
    struct passwd *result = NULL;
    char buffer[4096];
    if (getpwuid_r((uid_t)uid, &pwd, buffer, sizeof(buffer), &result) == 0 &&
        result != NULL && result->pw_name != NULL) {
        g_strlcpy(out, result->pw_name, out_len);
        return;
    }
    snprintf(out, out_len, "%u", uid);
}

double prism_parse_double_after(const char *text, const char *key) {
    if (text == NULL || key == NULL) {
        return 0;
    }
    const char *hit = strstr(text, key);
    if (hit == NULL) {
        return 0;
    }
    return strtod(hit + strlen(key), NULL);
}
