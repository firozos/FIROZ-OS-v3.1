#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/sysctl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <ctype.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <sys/statvfs.h>
#include <time.h>
#include <errno.h>
#include <stdint.h>

#include "icon.h"

#define PORT 8080
#define CHUNK_SIZE (4 * 1024 * 1024)

typedef struct {
    volatile int is_running;
    volatile int cancel_requested;
    char current_file[512];
    char task_type[32];
    volatile long long total_bytes;
    volatile long long copied_bytes;
    volatile time_t start_time;
} TaskStatus;

static TaskStatus global_task = {0, 0, "", "", 0, 0, 0};
static pthread_mutex_t task_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_active_port = PORT;

typedef struct {
    int type;
    int req_id;
    int priority;
    int msg_id;
    int target_id;
    int user_id;
    int unk1;
    int unk2;
    int app_id;
    int error_num;
    int unk3;
    unsigned char use_icon_image_uri;
    char message[1024];
    char icon_uri[1024];
    char unk[1024];
} OrbisNotificationRequest;

typedef struct {
    unsigned char *next_in;
    unsigned int avail_in;
    unsigned long total_in;
    unsigned char *next_out;
    unsigned int avail_out;
    unsigned long total_out;
    char *msg;
    void *state;
    void *zalloc;
    void *zfree;
    void *opaque;
    int data_type;
    unsigned long adler;
    unsigned long reserved;
} z_stream_ps5;

void send_all(int sock, const void *data, size_t length) {
    size_t total_sent = 0;
    const char *ptr = (const char *)data;
    while (total_sent < length) {
        ssize_t sent = send(sock, ptr + total_sent, length - total_sent, 0);
        if (sent <= 0) break;
        total_sent += (size_t)sent;
    }
}

void ps5_notify(const char *msg) {
    int (*sceKernelSendNotificationRequest)(int, OrbisNotificationRequest*, size_t, int) = NULL;
    sceKernelSendNotificationRequest = dlsym(RTLD_DEFAULT, "sceKernelSendNotificationRequest");
    if (!sceKernelSendNotificationRequest) {
        void *lib = dlopen("/system/common/lib/libkernel_web.sprx", RTLD_LAZY);
        if (!lib) lib = dlopen("/system/common/lib/libkernel.sprx", RTLD_LAZY);
        if (!lib) lib = dlopen("/system/common/lib/libkernel_sys.sprx", RTLD_LAZY);
        if (lib) sceKernelSendNotificationRequest = dlsym(lib, "sceKernelSendNotificationRequest");
    }
    if (sceKernelSendNotificationRequest) {
        OrbisNotificationRequest req;
        memset(&req, 0, sizeof(req));
        req.target_id = -1;
        req.use_icon_image_uri = 1;
        snprintf(req.message, sizeof(req.message), "%s", msg);
        snprintf(req.icon_uri, sizeof(req.icon_uri), "http://127.0.0.1:%d/icon0.png", g_active_port);
        sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
    }
}

void load_ps5_sysmodule(uint32_t id) {
    void *sysmod = dlopen("/system/common/lib/libSceSysmodule.sprx", RTLD_LAZY);
    if (sysmod) {
        int (*sceSysmoduleLoadModuleInternal)(uint32_t) = dlsym(sysmod, "sceSysmoduleLoadModuleInternal");
        if (sceSysmoduleLoadModuleInternal) {
            sceSysmoduleLoadModuleInternal(id);
        }
    }
}

void install_media_launcher(int port) {
    mkdir("/data", 0777);
    mkdir("/data/firozfar051_app", 0777);
    mkdir("/user/app/FMGR00051", 0755);
    mkdir("/user/app/FMGR00051/sce_sys", 0755);

    const char *paths_json[2] = {
        "/user/app/FMGR00051/sce_sys/param.json",
        "/data/firozfar051_app/param.json"
    };
    const char *paths_icon[2] = {
        "/user/app/FMGR00051/sce_sys/icon0.png",
        "/data/firozfar051_app/icon0.png"
    };

    for (int i = 0; i < 2; i++) {
        FILE *json = fopen(paths_json[i], "wb");
        if (json) {
            fprintf(json,
                "{\n"
                "  \"applicationCategoryType\": 65536,\n"
                "  \"titleId\": \"FMGR00051\",\n"
                "  \"titleName\": \"FIROZ OS\",\n"
                "  \"deeplinkUri\": \"http://127.0.0.1:%d/\",\n"
                "  \"localizedParameters\": {\n"
                "    \"defaultLanguage\": \"en-US\",\n"
                "    \"en-US\": {\n"
                "      \"titleName\": \"FIROZ OS\"\n"
                "    }\n"
                "  }\n"
                "}\n", port);
            fclose(json);
        }
        FILE *icon = fopen(paths_icon[i], "wb");
        if (icon) {
            fwrite(icon0_png, 1, icon0_png_len, icon);
            fclose(icon);
        }
    }

    load_ps5_sysmodule(0x80000018);
    void *libAppInst = dlopen("/system/common/lib/libSceAppInstUtil.sprx", RTLD_LAZY);
    if (!libAppInst) libAppInst = dlopen("libSceAppInstUtil.sprx", RTLD_LAZY);
    if (libAppInst) {
        int (*init)(void) = dlsym(libAppInst, "sceAppInstUtilInitialize");
        int (*installAll)(void*) = dlsym(libAppInst, "sceAppInstUtilAppInstallAll");
        if (init) init();
        if (installAll) installAll(NULL);
    }
}

int launch_game(const char *title_id) {
    if (!title_id || !title_id[0]) return -1;
    load_ps5_sysmodule(0x80000022);
    void *libAppMgr = dlopen("/system/common/lib/libSceAppMgr.sprx", RTLD_LAZY);
    if (!libAppMgr) libAppMgr = dlopen("libSceAppMgr.sprx", RTLD_LAZY);
    if (libAppMgr) {
        int (*launch)(const char*, const char*, int) = dlsym(libAppMgr, "sceAppMgrLaunchAppByAppId");
        if (launch) {
            int res = launch(title_id, NULL, 0);
            char msg[128];
            snprintf(msg, sizeof(msg), "FIROZ OS: Launching %s", title_id);
            ps5_notify(msg);
            return res;
        }
    }
    return -1;
}

int install_pkg(const char *pkg_path) {
    if (!pkg_path || access(pkg_path, F_OK) != 0) return -1;
    load_ps5_sysmodule(0x80000018);
    void *libAppInst = dlopen("/system/common/lib/libSceAppInstUtil.sprx", RTLD_LAZY);
    if (!libAppInst) libAppInst = dlopen("libSceAppInstUtil.sprx", RTLD_LAZY);
    if (libAppInst) {
        int (*init)(void) = dlsym(libAppInst, "sceAppInstUtilInitialize");
        int (*install)(const char*, void*, void*) = dlsym(libAppInst, "sceAppInstUtilInstallByPackage");
        if (init) init();
        if (install) {
            unsigned char meta_buf[512];
            unsigned char playgo_buf[512];
            memset(meta_buf, 0, sizeof(meta_buf));
            memset(playgo_buf, 0, sizeof(playgo_buf));
            int ret = install(pkg_path, meta_buf, playgo_buf);
            const char *base = strrchr(pkg_path, '/');
            char msg[256];
            snprintf(msg, sizeof(msg), "FIROZ OS: Installing %s", base ? base + 1 : pkg_path);
            ps5_notify(msg);
            return ret;
        }
    }
    return -1;
}

int launch_elf_payload(const char *elf_path) {
    FILE *f = fopen(elf_path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 4) { fclose(f); return -1; }

    char *data = malloc((size_t)sz);
    if (!data) { fclose(f); return -1; }
    if (fread(data, 1, (size_t)sz, f) != (size_t)sz) {
        free(data); fclose(f); return -1;
    }
    fclose(f);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { free(data); return -1; }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9021);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock); free(data); return -1;
    }

    size_t sent = 0;
    while (sent < (size_t)sz) {
        ssize_t n = send(sock, data + sent, (size_t)sz - sent, 0);
        if (n <= 0) break;
        sent += (size_t)n;
    }
    close(sock);
    free(data);

    const char *base = strrchr(elf_path, '/');
    char msg[256];
    snprintf(msg, sizeof(msg), "FIROZ OS: Injected %s", base ? base + 1 : elf_path);
    ps5_notify(msg);
    return (sent == (size_t)sz) ? 0 : -1;
}

void url_decode(char *dst, const char *src, size_t max_len) {
    size_t written = 0;
    char a, b;
    while (*src && written + 1 < max_len) {
        if ((*src == '%') && ((a = src[1]) && (b = src[2])) && (isxdigit(a) && isxdigit(b))) {
            if (a >= 'a') a -= 'a' - 'A';
            if (a >= 'A') a -= ('A' - 10); else a -= '0';
            if (b >= 'a') b -= 'a' - 'A';
            if (b >= 'A') b -= ('A' - 10); else b -= '0';
            *dst++ = (char)(16 * a + b);
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
        written++;
    }
    *dst = '\0';
}

void get_param(const char *url, const char *key, char *out, size_t max_len) {
    out[0] = '\0';
    if (!url || !key) return;
    const char *pos = url;
    size_t klen = strlen(key);
    while ((pos = strstr(pos, key)) != NULL) {
        if (pos == url || *(pos - 1) == '?' || *(pos - 1) == '&') {
            pos += klen;
            char raw[2048];
            size_t i = 0;
            while (pos[i] != '&' && pos[i] != ' ' && pos[i] != '\0' && i < sizeof(raw) - 1) {
                raw[i] = pos[i];
                i++;
            }
            raw[i] = '\0';
            url_decode(out, raw, max_len);
            return;
        }
        pos++;
    }
}

void escape_json(char *dest, const char *src, size_t max_len) {
    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j + 6 < max_len; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '\\' || c == '"') { dest[j++] = '\\'; dest[j++] = (char)c; }
        else if (c == '\n') { dest[j++] = '\\'; dest[j++] = 'n'; }
        else if (c == '\r') { dest[j++] = '\\'; dest[j++] = 'r'; }
        else if (c == '\t') { dest[j++] = '\\'; dest[j++] = 't'; }
        else if (c >= 32) { dest[j++] = (char)c; }
    }
    dest[j] = '\0';
}

const char* detect_usb_path(void) {
    static char usb_path[32];
    for (int i = 0; i <= 7; i++) {
        snprintf(usb_path, sizeof(usb_path), "/mnt/usb%d", i);
        struct statvfs vfs;
        if (statvfs(usb_path, &vfs) == 0 && vfs.f_blocks > 0) {
            return usb_path;
        }
    }
    return "/mnt/usb0";
}

void get_ps5_fw_string(char *out, size_t max_len) {
    uint32_t ver = 0;
    size_t len = sizeof(ver);
    int (*sys_by_name)(const char *, void *, size_t *, const void *, size_t) = dlsym(RTLD_DEFAULT, "sysctlbyname");
    if (sys_by_name && sys_by_name("kern.sdk_version", &ver, &len, NULL, 0) == 0 && ver > 0) {
        unsigned int maj = (ver >> 24) & 0xFF;
        unsigned int min = (ver >> 16) & 0xFF;
        snprintf(out, max_len, "%x.%02x", maj, min);
    } else {
        snprintf(out, max_len, "Universal (<=13.60)");
    }
}

void mkdir_p(const char *dir_path) {
    char tmp[2048];
    snprintf(tmp, sizeof(tmp), "%s", dir_path);
    size_t len = strlen(tmp);
    if (len > 1 && tmp[len - 1] == '/') tmp[len - 1] = 0;
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
}

long long calculate_size_recursive(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return 0;
    if (!S_ISDIR(st.st_mode)) return (long long)st.st_size;

    long long total = 0;
    DIR *d = opendir(path);
    if (!d) return 0;
    struct dirent *dir;
    while ((dir = readdir(d)) != NULL) {
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) continue;
        char sub[2048];
        snprintf(sub, sizeof(sub), "%s/%s", path, dir->d_name);
        total += calculate_size_recursive(sub);
    }
    closedir(d);
    return total;
}

int copy_file_single(const char *src, const char *dst) {
    struct stat st;
    if (stat(src, &st) != 0) return -1;

    const char *base = strrchr(src, '/');
    pthread_mutex_lock(&task_mutex);
    snprintf(global_task.current_file, sizeof(global_task.current_file), "%s", base ? base + 1 : src);
    pthread_mutex_unlock(&task_mutex);

    FILE *fs = fopen(src, "rb");
    if (!fs) return -1;
    FILE *fd = fopen(dst, "wb");
    if (!fd) { fclose(fs); return -1; }

    char *buf = malloc(CHUNK_SIZE);
    if (!buf) { fclose(fs); fclose(fd); return -1; }

    size_t br;
    int status = 0;
    while ((br = fread(buf, 1, CHUNK_SIZE, fs)) > 0) {
        pthread_mutex_lock(&task_mutex);
        int canceled = global_task.cancel_requested;
        pthread_mutex_unlock(&task_mutex);
        if (canceled) { status = -2; break; }

        size_t bw = fwrite(buf, 1, br, fd);
        if (bw != br) { status = -1; break; }

        pthread_mutex_lock(&task_mutex);
        global_task.copied_bytes += (long long)bw;
        pthread_mutex_unlock(&task_mutex);
    }

    free(buf);
    fclose(fs);
    fclose(fd);

    if (status != 0) {
        unlink(dst);
        return status;
    }
    chmod(dst, st.st_mode);
    return 0;
}

int recursive_copy(const char *src, const char *dst) {
    pthread_mutex_lock(&task_mutex);
    int canceled = global_task.cancel_requested;
    pthread_mutex_unlock(&task_mutex);
    if (canceled) return -2;

    struct stat st;
    if (stat(src, &st) != 0) return -1;

    if (S_ISDIR(st.st_mode)) {
        mkdir(dst, 0777);
        DIR *d = opendir(src);
        if (!d) return -1;
        struct dirent *dir;
        int res = 0;
        while ((dir = readdir(d)) != NULL) {
            if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) continue;
            char s_path[2048], d_path[2048];
            snprintf(s_path, sizeof(s_path), "%s/%s", src, dir->d_name);
            snprintf(d_path, sizeof(d_path), "%s/%s", dst, dir->d_name);
            if ((res = recursive_copy(s_path, d_path)) != 0) break;
        }
        closedir(d);
        return res;
    } else {
        return copy_file_single(src, dst);
    }
}

void recursive_delete(const char *target) {
    struct stat st;
    if (lstat(target, &st) != 0) return;
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(target);
        if (d) {
            struct dirent *dir;
            while ((dir = readdir(d)) != NULL) {
                if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) continue;
                char path[2048];
                snprintf(path, sizeof(path), "%s/%s", target, dir->d_name);
                recursive_delete(path);
            }
            closedir(d);
        }
        rmdir(target);
    } else {
        unlink(target);
    }
}

void recursive_chmod(const char *target, mode_t mode) {
    struct stat st;
    if (lstat(target, &st) != 0) return;
    chmod(target, mode);
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(target);
        if (d) {
            struct dirent *dir;
            while ((dir = readdir(d)) != NULL) {
                if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) continue;
                char path[2048];
                snprintf(path, sizeof(path), "%s/%s", target, dir->d_name);
                recursive_chmod(path, mode);
            }
            closedir(d);
        }
    }
}

int extract_tar(const char *tar_path, const char *out_dir) {
    FILE *f = fopen(tar_path, "rb");
    if (!f) return -1;
    char header[512];
    while (fread(header, 1, 512, f) == 512) {
        pthread_mutex_lock(&task_mutex);
        int canceled = global_task.cancel_requested;
        pthread_mutex_unlock(&task_mutex);
        if (canceled) { fclose(f); return -2; }

        if (header[0] == '\0') break;
        if (strncmp(header + 257, "ustar", 5) != 0 && header[257] != '\0') {
            fclose(f); return -1;
        }

        char filename[100];
        strncpy(filename, header, 99);
        filename[99] = '\0';
        if (strstr(filename, "../")) continue;
        char *safe_filename = filename;
        while (*safe_filename == '/') safe_filename++;

        pthread_mutex_lock(&task_mutex);
        snprintf(global_task.current_file, sizeof(global_task.current_file), "%s", safe_filename);
        pthread_mutex_unlock(&task_mutex);

        char size_str[13];
        strncpy(size_str, header + 124, 12);
        size_str[12] = '\0';
        long size = strtol(size_str, NULL, 8);
        char type = header[156];

        char outpath[2048];
        snprintf(outpath, sizeof(outpath), "%s/%s", out_dir, safe_filename);

        if (type == '5') {
            mkdir_p(outpath);
        } else {
            char *slash = strrchr(outpath, '/');
            if (slash) { *slash = '\0'; mkdir_p(outpath); *slash = '/'; }
            FILE *out = fopen(outpath, "wb");
            if (out) {
                char buf[32768];
                long remaining = size;
                while (remaining > 0) {
                    size_t to_read = remaining > (long)sizeof(buf) ? sizeof(buf) : (size_t)remaining;
                    size_t r = fread(buf, 1, to_read, f);
                    if (r == 0) break;
                    fwrite(buf, 1, r, out);
                    remaining -= (long)r;
                    pthread_mutex_lock(&task_mutex);
                    global_task.copied_bytes += (long long)r;
                    pthread_mutex_unlock(&task_mutex);
                }
                fclose(out);
                chmod(outpath, 0777);
            } else {
                fseek(f, size, SEEK_CUR);
            }
        }
        long padding = (512 - (size % 512)) % 512;
        fseek(f, padding, SEEK_CUR);
    }
    fclose(f);
    return 0;
}

int extract_zip(const char *zip_path, const char *out_dir) {
    FILE *f = fopen(zip_path, "rb");
    if (!f) return -1;

    void *libz = dlopen("/system/common/lib/libSceZlib.sprx", RTLD_LAZY);
    if (!libz) libz = dlopen("libz.so", RTLD_LAZY);
    int (*inflateInit2_)(z_stream_ps5*, int, const char*, int) = dlsym(libz ? libz : RTLD_DEFAULT, "inflateInit2_");
    int (*inflate_fn)(z_stream_ps5*, int) = dlsym(libz ? libz : RTLD_DEFAULT, "inflate");
    int (*inflateEnd_fn)(z_stream_ps5*) = dlsym(libz ? libz : RTLD_DEFAULT, "inflateEnd");

    unsigned char lfh[30];
    while (fread(lfh, 1, 30, f) == 30) {
        if (memcmp(lfh, "PK\x03\x04", 4) != 0) break;

        uint16_t flags = (uint16_t)(lfh[6] | (lfh[7] << 8));
        uint16_t method = (uint16_t)(lfh[8] | (lfh[9] << 8));
        uint32_t comp_size = (uint32_t)(lfh[18] | (lfh[19] << 8) | (lfh[20] << 16) | (lfh[21] << 24));
        uint16_t name_len = (uint16_t)(lfh[26] | (lfh[27] << 8));
        uint16_t extra_len = (uint16_t)(lfh[28] | (lfh[29] << 8));

        if (name_len == 0 || name_len > 1024) break;
        char fname[1025];
        if (fread(fname, 1, name_len, f) != name_len) break;
        fname[name_len] = '\0';
        fseek(f, extra_len, SEEK_CUR);

        if (strstr(fname, "../")) {
            fseek(f, comp_size, SEEK_CUR);
            continue;
        }

        pthread_mutex_lock(&task_mutex);
        snprintf(global_task.current_file, sizeof(global_task.current_file), "%s", fname);
        int canceled = global_task.cancel_requested;
        pthread_mutex_unlock(&task_mutex);
        if (canceled) break;

        char outpath[2048];
        snprintf(outpath, sizeof(outpath), "%s/%s", out_dir, fname);

        if (fname[name_len - 1] == '/') {
            mkdir_p(outpath);
            continue;
        }

        char *slash = strrchr(outpath, '/');
        if (slash) { *slash = '\0'; mkdir_p(outpath); *slash = '/'; }

        FILE *out = fopen(outpath, "wb");
        if (!out || (flags & 8)) {
            if (out) fclose(out);
            fseek(f, comp_size, SEEK_CUR);
            continue;
        }

        if (method == 0) {
            char buf[32768];
            uint32_t rem = comp_size;
            while (rem > 0) {
                size_t tr = rem > sizeof(buf) ? sizeof(buf) : rem;
                size_t r = fread(buf, 1, tr, f);
                if (r == 0) break;
                fwrite(buf, 1, r, out);
                rem -= (uint32_t)r;
                pthread_mutex_lock(&task_mutex);
                global_task.copied_bytes += (long long)r;
                pthread_mutex_unlock(&task_mutex);
            }
        } else if (method == 8 && inflateInit2_ && inflate_fn && inflateEnd_fn) {
            z_stream_ps5 strm;
            memset(&strm, 0, sizeof(strm));
            if (inflateInit2_(&strm, -15, "1.2.11", (int)sizeof(z_stream_ps5)) == 0) {
                unsigned char inbuf[32768], outbuf[32768];
                uint32_t rem = comp_size;
                while (rem > 0) {
                    size_t tr = rem > sizeof(inbuf) ? sizeof(inbuf) : rem;
                    size_t r = fread(inbuf, 1, tr, f);
                    if (r == 0) break;
                    rem -= (uint32_t)r;
                    pthread_mutex_lock(&task_mutex);
                    global_task.copied_bytes += (long long)r;
                    pthread_mutex_unlock(&task_mutex);

                    strm.next_in = inbuf;
                    strm.avail_in = (unsigned int)r;
                    do {
                        strm.next_out = outbuf;
                        strm.avail_out = sizeof(outbuf);
                        int ret = inflate_fn(&strm, 0);
                        size_t have = sizeof(outbuf) - strm.avail_out;
                        if (have > 0) fwrite(outbuf, 1, have, out);
                        if (ret != 0) break;
                    } while (strm.avail_out == 0);
                }
                inflateEnd_fn(&strm);
            } else {
                fseek(f, comp_size, SEEK_CUR);
            }
        } else {
            fseek(f, comp_size, SEEK_CUR);
        }
        fclose(out);
        chmod(outpath, 0777);
    }
    fclose(f);
    return 0;
}

int extract_rar(const char *rar_path, const char *out_dir) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock >= 0) {
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, "/system_tmp/wfm-7zip-helper.sock", sizeof(addr.sun_path) - 1);
        if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            char req[2048];
            snprintf(req, sizeof(req), "{\"op\":\"extract\",\"src\":\"%s\",\"dst\":\"%s\"}\n", rar_path, out_dir);
            send_all(sock, req, strlen(req));
            char resp[512];
            while (recv(sock, resp, sizeof(resp) - 1, 0) > 0) {
                pthread_mutex_lock(&task_mutex);
                global_task.copied_bytes = global_task.total_bytes / 2;
                pthread_mutex_unlock(&task_mutex);
            }
            close(sock);
            return 0;
        }
        close(sock);
    }

    FILE *f = fopen(rar_path, "rb");
    if (!f) return -1;
    unsigned char sig[7];
    if (fread(sig, 1, 7, f) != 7 || memcmp(sig, "Rar!\x1a\x07\x00", 7) != 0) {
        fclose(f);
        return -1;
    }
    unsigned char bhdr[7];
    while (fread(bhdr, 1, 7, f) == 7) {
        uint8_t type = bhdr[2];
        uint16_t flags = (uint16_t)(bhdr[3] | (bhdr[4] << 8));
        uint16_t head_size = (uint16_t)(bhdr[5] | (bhdr[6] << 8));
        if (head_size < 7) break;

        if (type == 0x74) {
            unsigned char fhdr[25];
            if (fread(fhdr, 1, 25, f) != 25) break;
            uint32_t pack_size = (uint32_t)(fhdr[0] | (fhdr[1] << 8) | (fhdr[2] << 16) | (fhdr[3] << 24));
            uint8_t method = fhdr[18];
            uint16_t name_size = (uint16_t)(fhdr[19] | (fhdr[20] << 8));
            char fname[1024];
            size_t to_rd = name_size < 1023 ? name_size : 1023;
            if (fread(fname, 1, to_rd, f) != to_rd) break;
            fname[to_rd] = '\0';
            for (size_t i = 0; fname[i]; i++) if (fname[i] == '\\') fname[i] = '/';

            long skip_hdr = (long)head_size - 7 - 25 - (long)to_rd;
            if (skip_hdr > 0) fseek(f, skip_hdr, SEEK_CUR);

            char outpath[2048];
            snprintf(outpath, sizeof(outpath), "%s/%s", out_dir, fname);
            char *slash = strrchr(outpath, '/');
            if (slash) { *slash = '\0'; mkdir_p(outpath); *slash = '/'; }

            if (method == 0x30) {
                FILE *out = fopen(outpath, "wb");
                char buf[32768];
                uint32_t rem = pack_size;
                while (rem > 0) {
                    size_t tr = rem > sizeof(buf) ? sizeof(buf) : rem;
                    size_t r = fread(buf, 1, tr, f);
                    if (r == 0) break;
                    if (out) fwrite(buf, 1, r, out);
                    rem -= (uint32_t)r;
                    pthread_mutex_lock(&task_mutex);
                    global_task.copied_bytes += (long long)r;
                    pthread_mutex_unlock(&task_mutex);
                }
                if (out) { fclose(out); chmod(outpath, 0777); }
            } else {
                fseek(f, pack_size, SEEK_CUR);
            }
        } else {
            uint32_t add_size = 0;
            if (flags & 0x8000) {
                unsigned char add_b[4];
                if (fread(add_b, 1, 4, f) == 4) {
                    add_size = (uint32_t)(add_b[0] | (add_b[1] << 8) | (add_b[2] << 16) | (add_b[3] << 24));
                }
                fseek(f, (long)head_size - 11 + (long)add_size, SEEK_CUR);
            } else {
                fseek(f, (long)head_size - 7, SEEK_CUR);
            }
        }
    }
    fclose(f);
    return 0;
}

int extract_archive_universal(const char *archive_path, const char *out_dir) {
    size_t len = strlen(archive_path);
    if (len > 4 && strcasecmp(archive_path + len - 4, ".zip") == 0) {
        return extract_zip(archive_path, out_dir);
    } else if (len > 4 && strcasecmp(archive_path + len - 4, ".rar") == 0) {
        return extract_rar(archive_path, out_dir);
    }
    return extract_tar(archive_path, out_dir);
}

typedef struct {
    char action[16];
    char src[1024];
    char dst[1024];
} TaskArgs;

void* background_worker(void* args) {
    TaskArgs *task = (TaskArgs*)args;

    long long total = calculate_size_recursive(task->src);
    if (total <= 0) total = 1;

    pthread_mutex_lock(&task_mutex);
    global_task.total_bytes = total;
    global_task.copied_bytes = 0;
    global_task.start_time = time(NULL);
    snprintf(global_task.task_type, sizeof(global_task.task_type), "%s", task->action);
    pthread_mutex_unlock(&task_mutex);

    if (strcmp(task->action, "copy") == 0) {
        recursive_copy(task->src, task->dst);
    } else if (strcmp(task->action, "cut") == 0) {
        if (rename(task->src, task->dst) != 0) {
            if (recursive_copy(task->src, task->dst) == 0) {
                recursive_delete(task->src);
            }
        }
    } else if (strcmp(task->action, "extract") == 0) {
        char ex_dir[1200];
        snprintf(ex_dir, sizeof(ex_dir), "%s_extracted", task->src);
        mkdir_p(ex_dir);
        extract_archive_universal(task->src, ex_dir);
    } else if (strcmp(task->action, "del") == 0) {
        recursive_delete(task->src);
    }

    pthread_mutex_lock(&task_mutex);
    global_task.copied_bytes = global_task.total_bytes;
    global_task.is_running = 0;
    global_task.cancel_requested = 0;
    pthread_mutex_unlock(&task_mutex);

    free(task);
    return NULL;
}

void api_sysinfo(int sock) {
    const char *h = "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=UTF-8\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
    send_all(sock, h, strlen(h));

    struct statvfs vfs_ssd, vfs_usb, vfs_ext, vfs_data;
    double ssd_total = 0, ssd_free = 0, usb_total = 0, usb_free = 0, ext_total = 0, ext_free = 0, data_free = 0;

    if (statvfs("/user", &vfs_ssd) == 0) {
        ssd_total = (double)vfs_ssd.f_blocks * vfs_ssd.f_frsize / (1024.0 * 1024.0 * 1024.0);
        ssd_free  = (double)vfs_ssd.f_bfree  * vfs_ssd.f_frsize / (1024.0 * 1024.0 * 1024.0);
    }
    if (statvfs("/data", &vfs_data) == 0) {
        data_free = (double)vfs_data.f_bfree * vfs_data.f_frsize / (1024.0 * 1024.0 * 1024.0);
    }
    const char *active_usb = detect_usb_path();
    if (statvfs(active_usb, &vfs_usb) == 0) {
        usb_total = (double)vfs_usb.f_blocks * vfs_usb.f_frsize / (1024.0 * 1024.0 * 1024.0);
        usb_free  = (double)vfs_usb.f_bfree  * vfs_usb.f_frsize / (1024.0 * 1024.0 * 1024.0);
    }
    if (statvfs("/mnt/ext0", &vfs_ext) == 0) {
        ext_total = (double)vfs_ext.f_blocks * vfs_ext.f_frsize / (1024.0 * 1024.0 * 1024.0);
        ext_free  = (double)vfs_ext.f_bfree  * vfs_ext.f_frsize / (1024.0 * 1024.0 * 1024.0);
    }

    char fw[64];
    get_ps5_fw_string(fw, sizeof(fw));

    char buf[1024];
    snprintf(buf, sizeof(buf),
        "{\"ok\":true,\"ssd_total\":%.1f,\"ssd_free\":%.1f,\"usb_total\":%.1f,\"usb_free\":%.1f,"
        "\"ext_total\":%.1f,\"ext_free\":%.1f,\"data_free\":%.1f,\"usb_mount\":\"%s\",\"fw\":\"%s\",\"pid\":%d}",
        ssd_total, ssd_free, usb_total, usb_free, ext_total, ext_free, data_free, active_usb, fw, getpid());
    send_all(sock, buf, strlen(buf));
}

void api_status(int sock) {
    const char *h = "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=UTF-8\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
    send_all(sock, h, strlen(h));

    pthread_mutex_lock(&task_mutex);
    int running = global_task.is_running;
    long long total = global_task.total_bytes;
    long long copied = global_task.copied_bytes;
    time_t elapsed = time(NULL) - global_task.start_time;
    char cur[512];
    escape_json(cur, global_task.current_file, sizeof(cur));
    pthread_mutex_unlock(&task_mutex);

    double speed_mb = (elapsed > 0) ? ((double)copied / (1024.0 * 1024.0)) / (double)elapsed : 0.0;
    long long rem_bytes = (total > copied) ? (total - copied) : 0;
    long eta_sec = (speed_mb > 0.05) ? (long)((rem_bytes / (1024.0 * 1024.0)) / speed_mb) : 0;

    char buf[1024];
    snprintf(buf, sizeof(buf),
        "{\"running\":%d,\"total\":%lld,\"copied\":%lld,\"speed\":%.1f,\"eta\":%ld,\"file\":\"%s\"}",
        running, total, copied, speed_mb, eta_sec, cur);
    send_all(sock, buf, strlen(buf));
}

void read_game_title(const char *app_id, char *title_out, size_t max_len) {
    snprintf(title_out, max_len, "%s", app_id);
    char ppath[512];
    snprintf(ppath, sizeof(ppath), "/user/app/%s/sce_sys/param.json", app_id);
    FILE *f = fopen(ppath, "rb");
    if (!f) return;
    char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    char *pos = strstr(buf, "\"titleName\"");
    if (pos) {
        pos = strchr(pos + 11, '"');
        if (pos) {
            pos++;
            char *end = strchr(pos, '"');
            if (end && (size_t)(end - pos) < max_len) {
                memcpy(title_out, pos, (size_t)(end - pos));
                title_out[end - pos] = '\0';
            }
        }
    }
}

void api_game_icon(int sock, const char *app_id) {
    if (!app_id || strstr(app_id, "..") || strchr(app_id, '/')) {
        const char *nf = "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n";
        send_all(sock, nf, strlen(nf));
        return;
    }
    char ipath[512];
    snprintf(ipath, sizeof(ipath), "/user/app/%s/sce_sys/icon0.png", app_id);
    FILE *f = fopen(ipath, "rb");
    if (!f) {
        const char *nf = "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n";
        send_all(sock, nf, strlen(nf));
        return;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    char hdr[256];
    snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: %ld\r\nCache-Control: max-age=3600\r\nConnection: close\r\n\r\n", sz);
    send_all(sock, hdr, strlen(hdr));

    char buf[16384];
    size_t r;
    while ((r = fread(buf, 1, sizeof(buf), f)) > 0) {
        send_all(sock, buf, r);
    }
    fclose(f);
}

void api_list(int sock, const char *path) {
    DIR *d = opendir(path);
    if (!d) {
        const char *err = "HTTP/1.1 404 Not Found\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"ok\":false}";
        send_all(sock, err, strlen(err));
        return;
    }
    const char *h = "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=UTF-8\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
    send_all(sock, h, strlen(h));

    char esc_path[1024];
    escape_json(esc_path, path, sizeof(esc_path));

    char large_buf[8192];
    int offset = snprintf(large_buf, sizeof(large_buf), "{\"ok\":true,\"path\":\"%s\",\"items\":[", esc_path);

    struct dirent *dir;
    int first = 1;
    int is_app_dir = (strcmp(path, "/user/app/") == 0 || strcmp(path, "/user/app") == 0);

    while ((dir = readdir(d)) != NULL) {
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) continue;
        char f_path[2048];
        snprintf(f_path, sizeof(f_path), "%s%s%s", path, (path[strlen(path) - 1] == '/') ? "" : "/", dir->d_name);

        struct stat st;
        if (stat(f_path, &st) != 0) memset(&st, 0, sizeof(st));

        char cln[512], title_cln[512] = "";
        escape_json(cln, dir->d_name, sizeof(cln));

        if (is_app_dir && S_ISDIR(st.st_mode)) {
            char raw_title[256];
            read_game_title(dir->d_name, raw_title, sizeof(raw_title));
            escape_json(title_cln, raw_title, sizeof(title_cln));
        }

        char temp[1200];
        int t_len = snprintf(temp, sizeof(temp),
            "%s{\"name\":\"%s\",\"title\":\"%s\",\"type\":\"%s\",\"size\":%lld}",
            first ? "" : ",", cln, title_cln, S_ISDIR(st.st_mode) ? "dir" : "file", (long long)st.st_size);

        if (offset + t_len >= (int)sizeof(large_buf) - 64) {
            send_all(sock, large_buf, (size_t)offset);
            offset = 0;
        }
        memcpy(large_buf + offset, temp, (size_t)t_len);
        offset += t_len;
        first = 0;
    }
    if (offset > 0) send_all(sock, large_buf, (size_t)offset);
    send_all(sock, "]}", 2);
    closedir(d);
}

void api_download(int sock, const char *filepath) {
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        const char *err = "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\nFile not found";
        send_all(sock, err, strlen(err));
        return;
    }
    fseek(f, 0, SEEK_END);
    long long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    const char *base = strrchr(filepath, '/');
    const char *fname = base ? base + 1 : filepath;

    char hdr[1024];
    snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Content-Disposition: attachment; filename=\"%s\"\r\n"
        "Content-Length: %lld\r\n"
        "Connection: close\r\n\r\n", fname, fsize);
    send_all(sock, hdr, strlen(hdr));

    char *buf = malloc(65536);
    if (buf) {
        size_t r;
        while ((r = fread(buf, 1, 65536, f)) > 0) {
            send_all(sock, buf, r);
        }
        free(buf);
    }
    fclose(f);
}

void api_text_read(int sock, const char *filepath) {
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        const char *err = "HTTP/1.1 404 Not Found\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"ok\":false}";
        send_all(sock, err, strlen(err));
        return;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz > 256 * 1024) sz = 256 * 1024;

    char *raw = malloc((size_t)sz + 1);
    if (!raw) { fclose(f); return; }
    size_t rd = fread(raw, 1, (size_t)sz, f);
    raw[rd] = '\0';
    fclose(f);

    const char *h = "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=UTF-8\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
    send_all(sock, h, strlen(h));
    send_all(sock, raw, rd);
    free(raw);
}

const char *UI_PART1 =
    "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n"
    "<!DOCTYPE html><html lang='fa' dir='rtl'><head><meta charset='UTF-8'><title>FIROZ OS v3.0</title>"
    "<link rel='icon' type='image/png' href='/icon0.png'>"
    "<style>"
    ":root{--glass:rgba(15,23,42,0.68);--glass-border:rgba(56,189,248,0.2);--accent:#38bdf8;--glow:rgba(56,189,248,0.55);--text:#f8fafc;}"
    "body{margin:0;padding:22px 28px;font-family:Tahoma,system-ui,sans-serif;color:var(--text);height:100vh;box-sizing:border-box;overflow:hidden;display:flex;flex-direction:column;background:radial-gradient(circle at top right,#1e1b4b,#0f172a 50%,#020617 95%);background-size:200% 200%;animation:bgMotion 15s ease infinite;}"
    "@keyframes bgMotion{0%{background-position:0% 50%}50%{background-position:100% 50%}100%{background-position:0% 50%}}"
    "@keyframes slideUpFade{from{opacity:0;transform:translateY(14px) translateZ(0);}to{opacity:1;transform:translateY(0) translateZ(0);}}"
    ".anim-slide{opacity:0;animation:slideUpFade 0.32s cubic-bezier(0.2,0.8,0.2,1) forwards;will-change:transform,opacity;}"
    "@keyframes popIn{0%{transform:scale(0.96);opacity:0;}100%{transform:scale(1);opacity:1;}}"
    "header{display:flex;justify-content:space-between;align-items:center;padding-bottom:14px;}"
    "h1{margin:0;font-size:2.3rem;font-weight:900;letter-spacing:1px;text-shadow:0 0 25px var(--glow);display:flex;align-items:center;gap:14px;}"
    ".logo-img{width:52px;height:52px;border-radius:14px;border:2px solid var(--accent);box-shadow:0 0 18px var(--glow);}"
    ".tg-pill{display:flex;align-items:center;gap:10px;background:rgba(56,189,248,0.12);border:1px solid rgba(56,189,248,0.35);padding:6px 16px;border-radius:14px;box-shadow:0 0 20px rgba(56,189,248,0.2);direction:ltr;}"
    ".badge{background:linear-gradient(135deg,#2563eb,#06b6d4);padding:8px 18px;border-radius:12px;font-size:1.05rem;font-weight:900;border:1px solid rgba(255,255,255,0.25);box-shadow:0 8px 20px rgba(0,0,0,0.4);}"
    ".btn-lang{background:var(--glass);backdrop-filter:blur(10px);color:white;border:1px solid var(--glass-border);padding:10px 22px;font-size:1.2rem;font-weight:bold;border-radius:14px;cursor:pointer;outline:none;transition:all 0.25s;}"
    ".tab-bar{display:flex;gap:10px;padding-bottom:14px;border-bottom:1px solid rgba(255,255,255,0.1);}"
    ".tab-btn{flex:1;padding:15px 4px;font-size:1.18rem;font-weight:900;color:#94a3b8;background:rgba(0,0,0,0.3);border:2px solid transparent;border-radius:15px;cursor:pointer;outline:none;transition:all 0.25s;}"
    ".tab-btn.active{color:white;background:var(--glass);border-color:var(--accent);box-shadow:0 8px 25px var(--glow);}"
    ".focusable:focus,.focusable:hover{transform:scale(1.03) translateZ(0);border-color:#38bdf8!important;box-shadow:0 10px 25px rgba(0,0,0,0.65),0 0 20px rgba(56,189,248,0.5)!important;color:white;z-index:50;}"
    ".main-area{display:flex;flex:1;overflow:hidden;margin-top:14px;position:relative;}"
    ".view{display:none;width:100%;height:100%;flex-direction:column;animation:popIn 0.25s ease-out forwards;}.view.active{display:flex;}";

const char *UI_PART2 =
    ".split-grid{display:grid;grid-template-columns:1fr 1fr;gap:24px;flex:1;overflow:hidden;}"
    ".glass-panel{background:var(--glass);backdrop-filter:blur(20px);border:1px solid var(--glass-border);border-radius:22px;display:flex;flex-direction:column;overflow:hidden;box-shadow:0 20px 50px rgba(0,0,0,0.55);transition:border-color 0.25s;}"
    ".panel-title{background:rgba(0,0,0,0.4);padding:12px 18px;font-size:1.25rem;font-weight:bold;display:flex;justify-content:space-between;align-items:center;color:#e2e8f0;border-bottom:1px solid var(--glass-border);}"
    ".quick-mounts{display:flex;gap:6px;flex-wrap:wrap;}"
    ".q-btn{background:rgba(56,189,248,0.14);border:1px solid rgba(56,189,248,0.35);color:#38bdf8;border-radius:8px;padding:4px 10px;font-size:0.9rem;cursor:pointer;font-weight:bold;transition:all 0.2s;}"
    ".q-btn:hover{background:#38bdf8;color:#020617;}"
    ".path-bar{padding:10px 18px;background:rgba(15,23,42,0.75);font-family:monospace;font-size:1.15rem;color:#7dd3fc;border-bottom:1px solid var(--glass-border);white-space:nowrap;overflow:hidden;text-overflow:ellipsis;direction:ltr;text-align:left;}"
    ".file-list{flex:1;overflow-y:auto;padding:14px;scrollbar-width:none;}"
    ".row{display:flex;gap:10px;margin-bottom:10px;}"
    ".btn-chk{width:60px;background:rgba(0,0,0,0.45);border-radius:14px;border:2px solid transparent;display:flex;justify-content:center;align-items:center;font-size:1.7rem;outline:none;cursor:pointer;color:#94a3b8;}"
    ".btn-chk.selected{background:rgba(16,185,129,0.28);border-color:#34d399;color:#34d399;}"
    ".btn-name{flex:1;background:rgba(0,0,0,0.45);border-radius:14px;border:2px solid transparent;display:flex;align-items:center;justify-content:space-between;padding:13px 18px;font-size:1.3rem;font-weight:500;outline:none;color:#f1f5f9;overflow:hidden;cursor:pointer;direction:ltr;}"
    ".icon{font-size:1.85rem;margin-right:12px;margin-left:4px;}"
    ".f-meta{font-size:0.98rem;color:#94a3b8;margin-left:10px;white-space:nowrap;background:rgba(0,0,0,0.35);padding:3px 9px;border-radius:8px;}"
    ".controls{display:grid;grid-template-columns:repeat(9,1fr);gap:10px;margin-top:14px;}"
    ".btn-action{padding:15px 5px;font-size:1.08rem;font-weight:900;font-family:Tahoma,sans-serif;border-radius:14px;border:2px solid rgba(255,255,255,0.12);color:white;background:rgba(30,41,59,0.88);outline:none;cursor:pointer;transition:all 0.2s;}"
    ".bg-copy{background:linear-gradient(135deg,#059669,#10b981);}.bg-del{background:linear-gradient(135deg,#dc2626,#ef4444);}.bg-ext{background:linear-gradient(135deg,#d97706,#f59e0b);}.bg-up{background:linear-gradient(135deg,#2563eb,#3b82f6);}"
    ".launch-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(250px,1fr));gap:22px;flex:1;overflow-y:auto;padding:16px;scrollbar-width:none;}"
    ".game-card{background:rgba(15,23,42,0.78);backdrop-filter:blur(15px);border:2px solid rgba(255,255,255,0.08);border-radius:22px;padding:20px;text-align:center;outline:none;transition:all 0.25s;box-shadow:0 12px 25px rgba(0,0,0,0.5);display:flex;flex-direction:column;align-items:center;justify-content:space-between;cursor:pointer;}"
    ".game-cover{width:128px;height:128px;border-radius:18px;object-fit:cover;margin-bottom:12px;border:2px solid rgba(56,189,248,0.35);box-shadow:0 8px 20px rgba(0,0,0,0.6);}"
    ".game-icon{font-size:4.5rem;margin-bottom:12px;}"
    ".pkg-icon{font-size:4.2rem;filter:drop-shadow(0 0 15px rgba(234,179,8,0.5));}"
    ".game-title{font-size:1.25rem;font-weight:900;color:white;word-break:break-word;margin-bottom:8px;}"
    ".pkg-size{font-size:1.05rem;color:#cbd5e1;background:rgba(0,0,0,0.5);padding:4px 12px;border-radius:8px;margin-bottom:10px;direction:ltr;}"
    ".btn-install{background:linear-gradient(135deg,#eab308,#d97706);border:none;color:white;padding:10px 18px;font-size:1.15rem;font-family:Tahoma,sans-serif;font-weight:bold;border-radius:10px;width:100%;cursor:pointer;}"
    ".dash-grid{display:grid;grid-template-columns:repeat(3,1fr);gap:24px;flex:1;align-items:stretch;padding:16px;}"
    ".stat-card{background:var(--glass);backdrop-filter:blur(25px);border:1px solid var(--glass-border);border-radius:24px;padding:28px;display:flex;flex-direction:column;justify-content:center;box-shadow:0 15px 40px rgba(0,0,0,0.6);}"
    ".s-title{font-size:1.55rem;font-weight:bold;color:#38bdf8;margin-bottom:12px;}"
    ".s-val{font-size:2.7rem;font-weight:900;color:white;margin-bottom:8px;direction:ltr;text-align:right;}"
    "body[dir='ltr'] .s-val{text-align:left;}"
    ".s-sub{font-size:1.18rem;color:#94a3b8;}"
    "#overlay,#overlay-install,#overlay-editor{display:none;position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(2,6,23,0.92);backdrop-filter:blur(20px);z-index:100;flex-direction:column;justify-content:center;align-items:center;}"
    ".modal-box{background:var(--glass);border:1px solid var(--glass-border);border-radius:24px;padding:35px;width:65%;max-width:850px;text-align:center;}"
    ".inst-title{font-size:2.3rem;font-weight:900;color:white;margin-bottom:18px;text-shadow:0 0 20px rgba(56,189,248,0.5);}"
    ".inst-name{font-size:1.35rem;color:#94a3b8;margin-bottom:18px;word-break:break-all;direction:ltr;}"
    ".progress-bg{width:100%;height:28px;background:rgba(0,0,0,0.5);border-radius:16px;overflow:hidden;margin-bottom:18px;border:1px solid var(--glass-border);direction:ltr;}"
    ".progress-fill{height:100%;background:linear-gradient(90deg,#3b82f6,#0ea5e9);width:0%;transition:width 0.3s;}"
    ".progress-fill.yellow{background:linear-gradient(90deg,#eab308,#f59e0b);}"
    ".inst-stats{display:flex;justify-content:space-between;font-size:1.35rem;font-weight:bold;color:white;direction:ltr;}"
    ".stat-box{background:rgba(0,0,0,0.35);padding:12px 20px;border-radius:14px;border:1px solid rgba(255,255,255,0.06);}"
    "textarea.editor-area{width:100%;height:340px;background:#090d16;color:#38bdf8;border:1px solid var(--glass-border);border-radius:14px;padding:14px;font-family:monospace;font-size:1.15rem;box-sizing:border-box;margin-bottom:16px;direction:ltr;text-align:left;}"
    ".spinner{width:64px;height:64px;border:6px solid rgba(255,255,255,0.1);border-top:6px solid var(--accent);border-radius:50%;animation:spin 1s infinite;margin:auto;}"
    "@keyframes spin{100%{transform:rotate(360deg);}}</style>";
const char *UI_PART3 =
    "<script>"
    "const dict = {"
    "  fa: { tab_file:'📁 مدیریت فایل', tab_ps5:'🎮 سیو PS5', tab_ps4:'🕹️ سیو PS4', tab_pkg:'📦 نصب PKG', tab_elf:'⚡ پیلودهای ELF', tab_dash:'🌡️ داشبورد', tab_run:'🚀 اجرای بازی', launch_msg:'در حال اجرای بازی...', inst_msg:'در حال نصب بازی', speed:'سرعت', eta:'زمان باقی‌مانده', back:'بازگشت', copy:'کپی', cut:'انتقال', ext:'استخراج', del:'حذف', mkdir:'پوشه جدید', ren:'تغییر نام', chmod:'دسترسی', upload:'آپلود', wait:'صبر کنید' },"
    "  en: { tab_file:'📁 FILE MANAGER', tab_ps5:'🎮 PS5 SAVES', tab_ps4:'🕹️ PS4 SAVES', tab_pkg:'📦 PKG STORE', tab_elf:'⚡ ELF PAYLOADS', tab_dash:'🌡️ DASHBOARD', tab_run:'🚀 LAUNCHER', launch_msg:'LAUNCHING...', inst_msg:'INSTALLING PKG', speed:'SPEED', eta:'ETA', back:'GO BACK', copy:'COPY', cut:'MOVE', ext:'EXTRACT', del:'DELETE', mkdir:'NEW DIR', ren:'RENAME', chmod:'CHMOD', upload:'UPLOAD', wait:'Wait' }"
    "};"
    "let lang='fa'; let mode='FILE'; let usbMount='/mnt/usb0';"
    "let panels={left:{path:'/user/app/',items:[]}, right:{path:'/mnt/usb0/',items:[]}};"
    "let activePanel='left'; let isProcessing=false; let statusInterval=null; let errorCount=0; let editingPath='';"
    "function t(k){return dict[lang][k] || k;}"
    "function toggleLang(){"
    "  lang=(lang==='fa')?'en':'fa'; document.body.dir=(lang==='fa')?'rtl':'ltr'; document.body.setAttribute('lang', lang);"
    "  ['FILE','PS5','PS4','PKG','ELF','DASH','RUN'].forEach(m=>{ const el=document.getElementById('tab-'+m); if(el) el.innerText=t('tab_'+m.toLowerCase()); });"
    "  document.getElementById('btn-copy').innerText=t('copy');"
    "  document.getElementById('btn-cut').innerText=t('cut');"
    "  document.getElementById('btn-ext').innerText=t('ext');"
    "  document.getElementById('btn-del').innerText=t('del');"
    "  document.getElementById('btn-mkdir').innerText=t('mkdir');"
    "  document.getElementById('btn-ren').innerText=t('ren');"
    "  document.getElementById('btn-chmod').innerText=t('chmod');"
    "  document.getElementById('btn-upload').innerText=t('upload');"
    "  renderList('left'); renderList('right');"
    "}"
    "function formatSize(bytes){ if(!bytes||bytes<=0) return '0 B'; const k=1024, sizes=['B','KB','MB','GB'], i=Math.floor(Math.log(bytes)/Math.log(k)); return parseFloat((bytes/Math.pow(k,i)).toFixed(2))+' '+sizes[i]; }"
    "function formatTime(sec){ if(!isFinite(sec)||sec<0) return '--:--'; let m=Math.floor(sec/60), s=Math.floor(sec%60); return `${m<10?'0':''}${m}:${s<10?'0':''}${s}`; }"
    "async function initSys(){ try{ const r=await fetch('/api/sysinfo'); const d=await r.json(); if(d.usb_mount){ usbMount=d.usb_mount; panels.right.path=usbMount+'/'; } }catch(e){} setMode('FILE'); }"
    "async function setMode(m){"
    "  if(isProcessing) return; mode=m;"
    "  document.querySelectorAll('.tab-btn').forEach(b=>b.classList.remove('active'));"
    "  const tb=document.getElementById('tab-'+m); if(tb){ tb.classList.add('active'); tb.innerText=t('tab_'+m.toLowerCase()); }"
    "  document.querySelectorAll('.view').forEach(v=>v.classList.remove('active'));"
    "  if(m==='FILE'||m==='PS5'||m==='PS4'){"
    "    document.getElementById('view-files').classList.add('active');"
    "    if(m==='FILE'){ loadDir('left',panels.left.path); loadDir('right',panels.right.path); }"
    "    if(m==='PS5'){ const dst=usbMount+'/PS5_SAVES_BACKUP/'; await fetch('/api/action',{method:'POST',body:'type=mkdir&src='+encodeURIComponent(dst)}); loadDir('left','/user/home/'); loadDir('right',dst); }"
    "    if(m==='PS4'){ const dst=usbMount+'/PS4_SAVES_BACKUP/'; await fetch('/api/action',{method:'POST',body:'type=mkdir&src='+encodeURIComponent(dst)}); loadDir('left','/user/home/'); loadDir('right',dst); }"
    "  } else if(m==='DASH'){ document.getElementById('view-dash').classList.add('active'); fetchDash();"
    "  } else if(m==='RUN'){ document.getElementById('view-run').classList.add('active'); loadGames();"
    "  } else if(m==='PKG'){ document.getElementById('view-pkg').classList.add('active'); loadPkgs();"
    "  } else if(m==='ELF'){ document.getElementById('view-elf').classList.add('active'); loadElfs(); }"
    "}"
    "async function fetchDash(){"
    "  try{ const res=await fetch('/api/sysinfo'); const d=await res.json();"
    "    document.getElementById('ssd-free').innerText=d.ssd_free>0?d.ssd_free.toFixed(1)+' GB':'آفلاین';"
    "    document.getElementById('ssd-tot').innerText='کل فضا: '+d.ssd_total.toFixed(1)+' GB | فریمور: '+d.fw;"
    "    document.getElementById('usb-free').innerText=d.usb_free>0?d.usb_free.toFixed(1)+' GB':'متصل نیست';"
    "    document.getElementById('usb-tot').innerText='مسیر: '+d.usb_mount+' ('+d.usb_total.toFixed(1)+' GB)';"
    "    document.getElementById('ext-free').innerText=d.ext_free>0?d.ext_free.toFixed(1)+' GB':(d.data_free.toFixed(1)+' GB (/data)');"
    "    document.getElementById('ext-tot').innerText=d.ext_total>0?'M.2 SSD Total: '+d.ext_total.toFixed(1)+' GB':'پارتیشن داخلی /data';"
    "  }catch(e){}"
    "}"
    "async function loadDir(side, path){"
    "  if(!path.endsWith('/')) path+='/';"
    "  panels[side].path=path; document.getElementById('path-'+side).innerText=path;"
    "  try{ const res=await fetch('/api/list?dir='+encodeURIComponent(path)); const d=await res.json();"
    "    panels[side].items=(d.items||[]).sort((a,b)=>(a.type===b.type?a.name.localeCompare(b.name):(a.type==='dir'?-1:1)));"
    "    renderList(side);"
    "  }catch(e){ document.getElementById('list-'+side).innerHTML=`<div class='row'><button class='btn-name focusable'>⚠️ خطا در خواندن مسیر</button></div>`; }"
    "}"
    "function isTextFile(n){ return /\\.(json|ini|cfg|conf|txt|xml|lua|log|sh|html|css|js|yml|yaml)$/i.test(n); }"
    "function renderList(side){"
    "  const list=document.getElementById('list-'+side); if(!list) return; list.innerHTML=''; const p=panels[side].path;"
    "  if(p!=='/'&&p!==''){ let pr=p.substring(0,p.lastIndexOf('/',p.length-2))+'/'; if(pr==='')pr='/';"
    "    list.innerHTML+=`<div class='row anim-slide'><button class='btn-name focusable' tabindex='0' onclick=\"loadDir('${side}','${pr}')\"><span><span class='icon'>🔙</span>${t('back')}</span></button></div>`;"
    "  }"
    "  panels[side].items.forEach((i, idx)=>{"
    "    const is=i.selected?'selected':''; const mk=i.selected?'✔️':'⭕';"
    "    let ic=i.type==='dir'?'📁':(i.name.endsWith('.tar')||i.name.endsWith('.zip')||i.name.endsWith('.rar')?'📦':(i.name.endsWith('.pkg')?'🎮':(i.name.endsWith('.elf')||i.name.endsWith('.bin')?'⚡':'📄')));"
    "    if(p==='/user/home/'&&i.type==='dir') ic='👤';"
    "    const fullP=p+(p.endsWith('/')?'':'/')+i.name;"
    "    const ac=i.type==='dir'?`loadDir('${side}','${fullP}/')`:(isTextFile(i.name)?`openEditor('${fullP}')`:(i.name.endsWith('.elf')||i.name.endsWith('.bin')?`runElf('${fullP}')`:`toggleSel('${side}','${i.name}')`));"
    "    let dly=idx<15?idx*0.025:0;"
    "    const meta=i.type==='dir'?(i.title?i.title:'پوشه'):formatSize(i.size);"
    "    list.innerHTML+=`<div class='row anim-slide' style='animation-delay:${dly}s'><button class='btn-chk focusable ${is}' tabindex='0' onclick=\"toggleSel('${side}','${i.name}')\">${mk}</button><button class='btn-name focusable' tabindex='0' onclick=\"${ac}\"><span style='display:flex;align-items:center;overflow:hidden;'><span class='icon'>${ic}</span><span style='overflow:hidden;text-overflow:ellipsis;'>${i.name}</span></span><span class='f-meta'>${meta}</span></button></div>`;"
    "  });"
    "}"
    "function toggleSel(side,name){ activePanel=side; const i=panels[side].items.find(x=>x.name===name); if(i){i.selected=!i.selected; renderList(side);} }";

const char *UI_PART4 =
    "async function openEditor(filepath){"
    "  editingPath=filepath; document.getElementById('ed-title').innerText=filepath;"
    "  const r=await fetch('/api/text?file='+encodeURIComponent(filepath));"
    "  document.getElementById('ed-area').value=await r.text();"
    "  document.getElementById('overlay-editor').style.display='flex';"
    "}"
    "async function saveEditor(){"
    "  const content=document.getElementById('ed-area').value;"
    "  await fetch('/api/text/save?file='+encodeURIComponent(editingPath),{method:'POST',body:content});"
    "  document.getElementById('overlay-editor').style.display='none';"
    "}"
    "async function loadGames(){"
    "  const lg=document.getElementById('launch-grid'); lg.innerHTML='<div class=\"spinner\"></div>';"
    "  try{ const res=await fetch('/api/list?dir=/user/app/'); const d=await res.json(); lg.innerHTML='';"
    "    d.items.filter(i=>i.type==='dir').forEach((i, idx)=>{ let dly=idx<15?idx*0.03:0; const title=i.title||i.name;"
    "      lg.innerHTML+=`<button class='game-card anim-slide focusable' style='animation-delay:${dly}s' tabindex='0' onclick=\"launchGame('${i.name}')\"><img src='/api/game-icon?id=${i.name}' class='game-cover' onerror=\"this.style.display='none'\"><div class='game-title'>${title}</div><div class='pkg-size'>${i.name}</div></button>`;"
    "    });"
    "  }catch(e){ lg.innerHTML='<h2>خطا در بارگذاری بازی‌ها</h2>'; }"
    "}"
    "async function loadPkgs(){"
    "  const pg=document.getElementById('pkg-grid'); pg.innerHTML='<div class=\"spinner\"></div>';"
    "  try{ const res=await fetch('/api/list?dir='+encodeURIComponent(usbMount+'/')); const d=await res.json(); pg.innerHTML='';"
    "    const pkgs=(d.items||[]).filter(i=>i.name.toLowerCase().endsWith('.pkg'));"
    "    if(pkgs.length===0){ pg.innerHTML='<h2 style=\"width:100%;text-align:center;color:#94a3b8;\">فایل PKG روی حافظه یافت نشد</h2>'; return; }"
    "    pkgs.forEach((i, idx)=>{ let dly=idx<15?idx*0.03:0;"
    "      pg.innerHTML+=`<button class='game-card anim-slide focusable' style='animation-delay:${dly}s;border-color:rgba(234,179,8,0.3);' tabindex='0' onclick=\"startInstall('${i.name}',${i.size})\"><div class='pkg-icon'>📦</div><div class='game-title'>${i.name}</div><div class='pkg-size'>${formatSize(i.size)}</div><div class='btn-install'>نصب پکیج</div></button>`;"
    "    });"
    "  }catch(e){ pg.innerHTML='<h2>حافظه USB متصل نیست</h2>'; }"
    "}"
    "async function loadElfs(){"
    "  const eg=document.getElementById('elf-grid'); eg.innerHTML='<div class=\"spinner\"></div>';"
    "  try{ const res=await fetch('/api/list?dir='+encodeURIComponent(usbMount+'/')); const d=await res.json(); eg.innerHTML='';"
    "    const elfs=(d.items||[]).filter(i=>i.name.toLowerCase().endsWith('.elf')||i.name.toLowerCase().endsWith('.bin'));"
    "    if(elfs.length===0){ eg.innerHTML='<h2 style=\"width:100%;text-align:center;color:#94a3b8;\">فایل ELF/BIN یافت نشد</h2>'; return; }"
    "    elfs.forEach((i, idx)=>{ let dly=idx<15?idx*0.03:0;"
    "      eg.innerHTML+=`<button class='game-card anim-slide focusable' style='animation-delay:${dly}s;border-color:rgba(56,189,248,0.4);' tabindex='0' onclick=\"runElf('${usbMount}/${i.name}')\"><div class='game-icon'>⚡</div><div class='game-title'>${i.name}</div><div class='pkg-size'>${formatSize(i.size)}</div><div class='btn-install' style='background:linear-gradient(135deg,#0284c7,#38bdf8);'>تزریق پیلود</div></button>`;"
    "    });"
    "  }catch(e){ eg.innerHTML='<h2>حافظه USB متصل نیست</h2>'; }"
    "}"
    "async function runElf(path){"
    "  if(isProcessing) return; isProcessing=true;"
    "  document.getElementById('overlay').style.display='flex'; document.getElementById('p-text').innerText='در حال تزریق پیلود به پورت 9021...';"
    "  try{ await fetch('/api/launch_elf?elf='+encodeURIComponent(path)); }catch(e){}"
    "  setTimeout(()=>{ document.getElementById('overlay').style.display='none'; isProcessing=false; }, 1800);"
    "}"
    "async function launchGame(id){"
    "  if(isProcessing) return; isProcessing=true;"
    "  document.getElementById('overlay').style.display='flex'; document.getElementById('p-text').innerText=t('launch_msg')+' ('+id+')';"
    "  try{ await fetch('/api/launch?id='+encodeURIComponent(id)); }catch(e){}"
    "  setTimeout(()=>{ document.getElementById('overlay').style.display='none'; isProcessing=false; }, 2500);"
    "}"
    "async function startInstall(pkgName, sizeBytes){"
    "  if(isProcessing) return; isProcessing=true;"
    "  document.getElementById('t-inst-msg').innerText=t('inst_msg'); document.getElementById('inst-name').innerText=pkgName;"
    "  document.getElementById('overlay-install').style.display='flex';"
    "  try{ fetch('/api/install_pkg?pkg='+encodeURIComponent(usbMount+'/'+pkgName)); }catch(e){}"
    "  let progress=0; let totalMB=sizeBytes/(1024*1024); if(totalMB<=0) totalMB=1000;"
    "  let interval=setInterval(()=>{"
    "    let speedMBps=55+Math.random()*25; progress+=speedMBps/10; if(progress>=totalMB) progress=totalMB;"
    "    let pct=(progress/totalMB)*100; let etaSec=(totalMB-progress)/speedMBps;"
    "    document.getElementById('inst-pct').innerText=pct.toFixed(1)+'%'; document.getElementById('inst-bar').style.width=pct+'%';"
    "    document.getElementById('inst-speed').innerText=speedMBps.toFixed(1)+' MB/s'; document.getElementById('inst-eta').innerText=formatTime(etaSec);"
    "    if(progress>=totalMB){ clearInterval(interval); document.getElementById('t-inst-msg').innerText='ارسال شد'; setTimeout(()=>{ document.getElementById('overlay-install').style.display='none'; isProcessing=false; },1800); }"
    "  },100);"
    "}"
    "async function cancelTask(){ await fetch('/api/cancel',{method:'POST'}); }"
    "async function checkStatus(){"
    "  if(statusInterval) return;"
    "  statusInterval=setInterval(async()=>{"
    "    try{ let res=await fetch('/api/status'); let d=await res.json(); errorCount=0;"
    "      if(d.running===1 && d.total>0){"
    "        let pct=(d.copied/d.total)*100; document.getElementById('p-bar').style.width=pct+'%';"
    "        document.getElementById('p-text').innerText=`${pct.toFixed(1)}% | ${d.speed.toFixed(1)} MB/s | زمان باقی‌مانده: ${formatTime(d.eta)}`;"
    "        document.getElementById('p-file').innerText=d.file||'';"
    "      } else if(d.running===0){ clearInterval(statusInterval); statusInterval=null; }"
    "    }catch(e){ errorCount++; if(errorCount>4){ clearInterval(statusInterval); statusInterval=null; isProcessing=false; document.getElementById('overlay').style.display='none'; } }"
    "  },600);"
    "}"
    "async function createNewFolder(){"
    "  const name=prompt('نام پوشه جدید:'); if(!name) return;"
    "  const p=panels[activePanel].path+name;"
    "  await fetch('/api/action',{method:'POST',body:`type=mkdir&src=${encodeURIComponent(p)}`});"
    "  loadDir(activePanel, panels[activePanel].path);"
    "}"
    "async function renameSelected(){"
    "  const sel=panels[activePanel].items.filter(i=>i.selected); if(sel.length!==1){ alert('لطفاً فقط ۱ مورد را انتخاب کنید'); return; }"
    "  const newName=prompt('نام جدید:', sel[0].name); if(!newName) return;"
    "  const sp=panels[activePanel].path+sel[0].name; const dp=panels[activePanel].path+newName;"
    "  await fetch('/api/action',{method:'POST',body:`type=rename&src=${encodeURIComponent(sp)}&dst=${encodeURIComponent(dp)}`});"
    "  loadDir(activePanel, panels[activePanel].path);"
    "}"
    "async function chmodSelected(){"
    "  const sel=panels[activePanel].items.filter(i=>i.selected); if(sel.length===0) return;"
    "  for(let i of sel){ const sp=panels[activePanel].path+i.name; await fetch('/api/action',{method:'POST',body:`type=chmod&src=${encodeURIComponent(sp)}`}); }"
    "  alert('دسترسی کامل اعمال شد!'); loadDir(activePanel, panels[activePanel].path);"
    "}"
    "async function uploadFile(inp){"
    "  if(!inp.files.length) return; const file=inp.files[0];"
    "  const dst=panels[activePanel].path+file.name;"
    "  document.getElementById('overlay').style.display='flex'; document.getElementById('p-text').innerText='در حال آپلود '+file.name+'...';"
    "  await fetch('/api/upload?dst='+encodeURIComponent(dst),{method:'POST',body:file});"
    "  document.getElementById('overlay').style.display='none'; inp.value='';"
    "  loadDir(activePanel, panels[activePanel].path);"
    "}"
    "async function processAction(action){"
    "  const ss=activePanel; const ds=(activePanel==='left')?'right':'left'; const sb=panels[ss].path; const db=panels[ds].path;"
    "  const sel=panels[ss].items.filter(i=>i.selected); if(sel.length===0||isProcessing) return; isProcessing=true;"
    "  document.getElementById('overlay').style.display='flex'; document.getElementById('p-bar').style.width='0%'; document.getElementById('p-text').innerText='در حال شروع...';"
    "  for(let i=0;i<sel.length;i++){"
    "    const sp=sb+(sb.endsWith('/')?'':'/')+sel[i].name; const dp=db+(db.endsWith('/')?'':'/')+sel[i].name;"
    "    let res=await fetch('/api/action',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:`type=${action}&src=${encodeURIComponent(sp)}&dst=${encodeURIComponent(dp)}`});"
    "    if(res.status===429){ alert('سیستم مشغول است!'); break; }"
    "    checkStatus();"
    "    while(statusInterval!==null){ await new Promise(r=>setTimeout(r,600)); }"
    "  }"
    "  isProcessing=false; document.getElementById('overlay').style.display='none'; loadDir(ss,panels[ss].path); loadDir(ds,panels[ds].path);"
    "}"
    "window.onload=()=>{ initSys(); };"
    "</script></head><body>"
    "<input type='file' id='file-up' style='display:none' onchange='uploadFile(this)'>"
    "<div id='overlay'><div class='modal-box'><div class='inst-title'>در حال پردازش...</div><div class='inst-name' id='p-file'></div><div class='progress-bg'><div class='progress-fill' id='p-bar'></div></div><h2 id='p-text' style='color:#38bdf8'>0%</h2><button class='btn-action bg-del focusable' style='padding:12px 30px;margin-top:10px;' onclick='cancelTask()'>لغو عملیات</button></div></div>"
    "<div id='overlay-install'><div class='modal-box'><div class='inst-title' id='t-inst-msg'>نصب پکیج</div><div class='inst-name' id='inst-name'>Game.pkg</div><div class='progress-bg'><div class='progress-fill yellow' id='inst-bar'></div></div><div class='inst-stats'><div class='stat-box'><span style='color:#94a3b8;font-size:1.1rem;' id='t-speed'>سرعت</span><br><span id='inst-speed'>0 MB/s</span></div><div class='stat-box' style='color:#eab308;font-size:2.2rem;' id='inst-pct'>0%</div><div class='stat-box'><span style='color:#94a3b8;font-size:1.1rem;' id='t-eta'>زمان</span><br><span id='inst-eta'>--:--</span></div></div></div></div>"
    "<div id='overlay-editor'><div class='modal-box' style='width:80%;max-width:1100px;'><div class='inst-name' id='ed-title'>ویرایشگر</div><textarea id='ed-area' class='editor-area'></textarea><div style='display:flex;gap:15px;justify-content:center;'><button class='btn-action bg-copy focusable' style='padding:14px 40px;' onclick='saveEditor()'>ذخیره</button><button class='btn-action bg-del focusable' style='padding:14px 40px;' onclick=\"document.getElementById('overlay-editor').style.display='none'\">بستن</button></div></div></div>"
    "<header><div style='display:flex;align-items:center;gap:18px;'><h1><img src='/icon0.png' class='logo-img' onerror=\"this.style.display='none'\"><span style='color:#38bdf8;'>FIROZ OS</span></h1><div class='tg-pill'><svg width='22' height='22' viewBox='0 0 24 24' fill='#38bdf8'><path d='M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm4.64 6.8c-.15 1.58-.8 5.42-1.13 7.19-.14.75-.42 1-.68 1.03-.58.05-1.02-.38-1.58-.75-.88-.58-1.38-.94-2.23-1.5-.99-.65-.35-1.01.22-1.59.15-.15 2.71-2.48 2.76-2.69.01-.03.01-.14-.07-.18-.08-.05-.19-.02-.27 0-.12.03-1.96 1.25-5.54 3.67-.52.36-.99.53-1.42.52-.47-.01-1.37-.26-2.03-.48-.82-.27-1.47-.42-1.42-.88.03-.24.29-.48.79-.74 3.08-1.34 5.14-2.22 6.17-2.66 2.93-1.25 3.54-1.46 3.94-1.46.09 0 .28.02.39.09.09.06.14.15.16.26.01.07.02.15.01.24z'/></svg><span style='color:#38bdf8;font-weight:bold;font-size:1.15rem;'>@firozfar051</span></div><div class='badge'>نسخه شاهکار</div></div><button class='btn-lang focusable' tabindex='0' onclick='toggleLang()'>فارسی / EN</button></header>"
    "<div class='tab-bar'>"
    "  <button id='tab-FILE' class='tab-btn focusable active' tabindex='0' onclick=\"setMode('FILE')\">📁 مدیریت فایل</button>"
    "  <button id='tab-PS5' class='tab-btn focusable' tabindex='0' onclick=\"setMode('PS5')\">🎮 سیو PS5</button>"
    "  <button id='tab-PS4' class='tab-btn focusable' tabindex='0' onclick=\"setMode('PS4')\">🕹️ سیو PS4</button>"
    "  <button id='tab-PKG' class='tab-btn focusable' tabindex='0' onclick=\"setMode('PKG')\">📦 نصب PKG</button>"
    "  <button id='tab-ELF' class='tab-btn focusable' tabindex='0' onclick=\"setMode('ELF')\">⚡ پیلودهای ELF</button>"
    "  <button id='tab-DASH' class='tab-btn focusable' tabindex='0' onclick=\"setMode('DASH')\">🌡️ داشبورد</button>"
    "  <button id='tab-RUN' class='tab-btn focusable' tabindex='0' onclick=\"setMode('RUN')\">🚀 اجرای بازی</button>"
    "</div>"
    "<div class='main-area'>"
    "  <div id='view-files' class='view active'><div class='split-grid'>"
    "    <div class='glass-panel' onclick=\"activePanel='left'\"><div class='panel-title'><span>حافظه داخلی (SSD)</span><div class='quick-mounts'><button class='q-btn' onclick=\"loadDir('left','/user/app/')\">نرم‌افزار</button><button class='q-btn' onclick=\"loadDir('left','/data/')\">دیتا</button><button class='q-btn' onclick=\"loadDir('left','/user/home/')\">پوشه کاربر</button><button class='q-btn' onclick=\"loadDir('left','/')\">ریشه</button></div></div><div class='path-bar' id='path-left'></div><div id='list-left' class='file-list'></div></div>"
    "    <div class='glass-panel' onclick=\"activePanel='right'\"><div class='panel-title'><span>حافظه خارجی (USB / M.2)</span><div class='quick-mounts'><button class='q-btn' onclick=\"loadDir('right',usbMount+'/')\">USB</button><button class='q-btn' onclick=\"loadDir('right','/mnt/ext0/')\">M.2 SSD</button><button class='q-btn' onclick=\"loadDir('right','/data/')\">دیتا</button></div></div><div class='path-bar' id='path-right'></div><div id='list-right' class='file-list'></div></div>"
    "  </div><div class='controls'>"
    "    <button id='btn-copy' class='btn-action bg-copy focusable' tabindex='0' onclick=\"processAction('copy')\">کپی</button>"
    "    <button id='btn-cut' class='btn-action focusable' tabindex='0' onclick=\"processAction('cut')\">انتقال</button>"
    "    <button id='btn-ext' class='btn-action bg-ext focusable' tabindex='0' onclick=\"processAction('extract')\">استخراج</button>"
    "    <button id='btn-del' class='btn-action bg-del focusable' tabindex='0' onclick=\"processAction('del')\">حذف</button>"
    "    <button id='btn-mkdir' class='btn-action focusable' tabindex='0' onclick='createNewFolder()'>پوشه جدید</button>"
    "    <button id='btn-ren' class='btn-action focusable' tabindex='0' onclick='renameSelected()'>تغییر نام</button>"
    "    <button id='btn-chmod' class='btn-action focusable' tabindex='0' onclick='chmodSelected()'>دسترسی</button>"
    "    <button id='btn-upload' class='btn-action bg-up focusable' tabindex='0' onclick=\"document.getElementById('file-up').click()\">آپلود</button>"
    "  </div></div>"
    "  <div id='view-dash' class='view'><div class='dash-grid'>"
    "    <div class='stat-card'><div class='s-title'>حافظه داخلی SSD (/user)</div><div class='s-val' id='ssd-free'>--</div><div class='s-sub' id='ssd-tot'>--</div></div>"
    "    <div class='stat-card'><div class='s-title'>حافظه فلش USB</div><div class='s-val' id='usb-free'>--</div><div class='s-sub' id='usb-tot'>--</div></div>"
    "    <div class='stat-card'><div class='s-title'>حافظه جانبی M.2 / دیتا</div><div class='s-val' id='ext-free'>--</div><div class='s-sub' id='ext-tot'>--</div></div>"
    "  </div></div>"
    "  <div id='view-run' class='view'><div class='launch-grid' id='launch-grid'></div></div>"
    "  <div id='view-pkg' class='view'><div class='launch-grid' id='pkg-grid'></div></div>"
    "  <div id='view-elf' class='view'><div class='launch-grid' id='elf-grid'></div></div>"
    "</div></body></html>";

void *client_thread(void *arg) {
    int client_sock = *(int *)arg;
    free(arg);

    int flag = 1;
    setsockopt(client_sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    char *buffer = malloc(16384);
    if (!buffer) { close(client_sock); return NULL; }

    int bytes_read = 0;
    while (bytes_read < 16383) {
        ssize_t n = recv(client_sock, buffer + bytes_read, 16383 - bytes_read, 0);
        if (n <= 0) break;
        bytes_read += (int)n;
        buffer[bytes_read] = '\0';
        if (strstr(buffer, "\r\n\r\n")) break;
    }
    if (bytes_read <= 0) { free(buffer); close(client_sock); return NULL; }

    char req_method[16] = {0}, req_path[2048] = {0};
    if (sscanf(buffer, "%15s %2047s", req_method, req_path) != 2) {
        free(buffer); close(client_sock); return NULL;
    }

    char *header_end = strstr(buffer, "\r\n\r\n");
    int content_length = 0;
    char *cl_ptr = strcasestr(buffer, "Content-Length:");
    if (cl_ptr) content_length = atoi(cl_ptr + 15);

    if (strncmp(req_path, "/icon0.png", 10) == 0) {
        char hdr[256];
        snprintf(hdr, sizeof(hdr),
            "HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: %u\r\nCache-Control: max-age=3600\r\nConnection: close\r\n\r\n",
            icon0_png_len);
        send_all(client_sock, hdr, strlen(hdr));
        send_all(client_sock, icon0_png, icon0_png_len);
    }
    else if (strncmp(req_path, "/api/game-icon", 14) == 0) {
        char id[128];
        get_param(req_path, "id=", id, sizeof(id));
        api_game_icon(client_sock, id);
    }
    else if (strncmp(req_path, "/api/sysinfo", 12) == 0) {
        api_sysinfo(client_sock);
    }
    else if (strncmp(req_path, "/api/list", 9) == 0) {
        char target_dir[1024] = "/user/app/";
        get_param(req_path, "dir=", target_dir, sizeof(target_dir));
        api_list(client_sock, target_dir);
    }
    else if (strncmp(req_path, "/api/status", 11) == 0) {
        api_status(client_sock);
    }
    else if (strncmp(req_path, "/api/cancel", 11) == 0) {
        pthread_mutex_lock(&task_mutex);
        global_task.cancel_requested = 1;
        pthread_mutex_unlock(&task_mutex);
        const char *ok_res = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"ok\":true}";
        send_all(client_sock, ok_res, strlen(ok_res));
    }
    else if (strncmp(req_path, "/api/launch_elf", 15) == 0) {
        char elf[1024];
        get_param(req_path, "elf=", elf, sizeof(elf));
        int r = launch_elf_payload(elf);
        char res[256];
        snprintf(res, sizeof(res), "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"ok\":%s}", r == 0 ? "true" : "false");
        send_all(client_sock, res, strlen(res));
    }
    else if (strncmp(req_path, "/api/launch", 11) == 0) {
        char id[128];
        get_param(req_path, "id=", id, sizeof(id));
        launch_game(id);
        const char *ok_res = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"ok\":true}";
        send_all(client_sock, ok_res, strlen(ok_res));
    }
    else if (strncmp(req_path, "/api/install_pkg", 16) == 0) {
        char pkg[1024];
        get_param(req_path, "pkg=", pkg, sizeof(pkg));
        install_pkg(pkg);
        const char *ok_res = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"ok\":true}";
        send_all(client_sock, ok_res, strlen(ok_res));
    }
    else if (strncmp(req_path, "/api/download", 13) == 0) {
        char file[1024];
        get_param(req_path, "file=", file, sizeof(file));
        api_download(client_sock, file);
    }
    else if (strncmp(req_path, "/api/text/save", 14) == 0 && strcmp(req_method, "POST") == 0) {
        char file[1024];
        get_param(req_path, "file=", file, sizeof(file));
        FILE *f = fopen(file, "wb");
        if (f && header_end) {
            char *body = header_end + 4;
            int already = bytes_read - (int)(body - buffer);
            if (already > 0) fwrite(body, 1, (size_t)already, f);
            int rem = content_length - already;
            char *tbuf = malloc(16384);
            while (rem > 0 && tbuf) {
                ssize_t n = recv(client_sock, tbuf, rem > 16384 ? 16384 : (size_t)rem, 0);
                if (n <= 0) break;
                fwrite(tbuf, 1, (size_t)n, f);
                rem -= (int)n;
            }
            if (tbuf) free(tbuf);
            fclose(f);
        }
        const char *ok_res = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"ok\":true}";
        send_all(client_sock, ok_res, strlen(ok_res));
    }
    else if (strncmp(req_path, "/api/text", 9) == 0) {
        char file[1024];
        get_param(req_path, "file=", file, sizeof(file));
        api_text_read(client_sock, file);
    }
    else if (strncmp(req_path, "/api/upload", 11) == 0 && strcmp(req_method, "POST") == 0) {
        char dst[1024];
        get_param(req_path, "dst=", dst, sizeof(dst));
        FILE *f = fopen(dst, "wb");
        if (f && header_end) {
            char *body = header_end + 4;
            int already = bytes_read - (int)(body - buffer);
            if (already > 0) fwrite(body, 1, (size_t)already, f);
            int rem = content_length - already;
            char *ubuf = malloc(65536);
            while (rem > 0 && ubuf) {
                ssize_t n = recv(client_sock, ubuf, rem > 65536 ? 65536 : (size_t)rem, 0);
                if (n <= 0) break;
                fwrite(ubuf, 1, (size_t)n, f);
                rem -= (int)n;
            }
            if (ubuf) free(ubuf);
            fclose(f);
            chmod(dst, 0777);
        }
        const char *ok_res = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"ok\":true}";
        send_all(client_sock, ok_res, strlen(ok_res));
    }
    else if (strcmp(req_method, "POST") == 0 && strncmp(req_path, "/api/action", 11) == 0) {
        if (!header_end) {
            const char *err_res = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n{\"error\":\"No body\"}";
            send_all(client_sock, err_res, strlen(err_res));
        } else {
            char *body = header_end + 4;
            int body_received = bytes_read - (int)(body - buffer);
            while (body_received < content_length && bytes_read < 16383) {
                ssize_t extra = recv(client_sock, buffer + bytes_read, 16383 - bytes_read, 0);
                if (extra <= 0) break;
                bytes_read += (int)extra;
                body_received += (int)extra;
                buffer[bytes_read] = '\0';
            }

            char act[16] = {0}, src[1024] = {0}, dst[1024] = {0};
            get_param(body, "type=", act, sizeof(act));
            get_param(body, "src=", src, sizeof(src));
            get_param(body, "dst=", dst, sizeof(dst));

            if (strcmp(act, "mkdir") == 0) {
                mkdir_p(src);
                const char *ok_res = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"ok\":true}";
                send_all(client_sock, ok_res, strlen(ok_res));
            } else if (strcmp(act, "rename") == 0) {
                rename(src, dst);
                const char *ok_res = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"ok\":true}";
                send_all(client_sock, ok_res, strlen(ok_res));
            } else if (strcmp(act, "chmod") == 0) {
                recursive_chmod(src, 0777);
                const char *ok_res = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"ok\":true}";
                send_all(client_sock, ok_res, strlen(ok_res));
            } else {
                pthread_mutex_lock(&task_mutex);
                if (global_task.is_running == 1) {
                    pthread_mutex_unlock(&task_mutex);
                    const char *busy_res = "HTTP/1.1 429 Too Many Requests\r\nConnection: close\r\n\r\n{\"error\":\"System is busy\"}";
                    send_all(client_sock, busy_res, strlen(busy_res));
                } else {
                    global_task.is_running = 1;
                    global_task.cancel_requested = 0;
                    global_task.total_bytes = 0;
                    global_task.copied_bytes = 0;
                    pthread_mutex_unlock(&task_mutex);

                    TaskArgs *task = malloc(sizeof(TaskArgs));
                    snprintf(task->action, sizeof(task->action), "%s", act);
                    snprintf(task->src, sizeof(task->src), "%s", src);
                    snprintf(task->dst, sizeof(task->dst), "%s", dst);

                    pthread_t tid;
                    if (pthread_create(&tid, NULL, background_worker, task) == 0) {
                        pthread_detach(tid);
                    } else {
                        free(task);
                        pthread_mutex_lock(&task_mutex);
                        global_task.is_running = 0;
                        pthread_mutex_unlock(&task_mutex);
                    }

                    const char *ok_res = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"ok\":true}";
                    send_all(client_sock, ok_res, strlen(ok_res));
                }
            }
        }
    }
    else if (strcmp(req_path, "/") == 0 || strncmp(req_path, "/index.html", 11) == 0) {
        send_all(client_sock, UI_PART1, strlen(UI_PART1));
        send_all(client_sock, UI_PART2, strlen(UI_PART2));
        send_all(client_sock, UI_PART3, strlen(UI_PART3));
        send_all(client_sock, UI_PART4, strlen(UI_PART4));
    }
    else {
        const char *err = "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n404 Not Found";
        send_all(client_sock, err, strlen(err));
    }

    free(buffer);
    close(client_sock);
    return NULL;
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);

    void (*set_name)(pthread_t, const char*) = dlsym(RTLD_DEFAULT, "pthread_set_name_np");
    if (set_name) set_name(pthread_self(), "FIROZ_OS.elf");

    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) return 1;

    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

    struct sockaddr_in server_addr;
    int bound = 0;
    for (int p = PORT; p < PORT + 10; p++) {
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons((uint16_t)p);
        if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == 0) {
            g_active_port = p;
            bound = 1;
            break;
        }
    }

    if (!bound || listen(server_sock, 128) < 0) {
        close(server_sock);
        return 1;
    }

    install_media_launcher(g_active_port);

    char start_msg[128];
    snprintf(start_msg, sizeof(start_msg), "FIROZ OS Active! Port: %d (@firozfar051)", g_active_port);
    ps5_notify(start_msg);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int c_fd = accept(server_sock, (struct sockaddr *)&client_addr, &client_len);
        if (c_fd < 0) {
            usleep(10000);
            continue;
        }
        int *client_sock = malloc(sizeof(int));
        if (!client_sock) {
            close(c_fd);
            continue;
        }
        *client_sock = c_fd;
        pthread_t tid;
        if (pthread_create(&tid, NULL, client_thread, client_sock) == 0) {
            pthread_detach(tid);
        } else {
            close(c_fd);
            free(client_sock);
        }
    }

    close(server_sock);
    return 0;
}
