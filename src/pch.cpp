#include "pch.h"

String read_file(const char *path) {
    FILE *fp = nullptr;
#ifdef _WIN32
    if (fopen_s(&fp, path, "r") && !fp) {
        // file not exists
        return String();
    }
#else
    fp = fopen(path, "r");
    if (!fp) {
        perror("open file failed!");
        throw std::logic_error("open file failed");
    }
#endif
    fseek(fp, 0, SEEK_END);
    const long len = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    String buf;
    buf.resize(len);

    if (fread(&buf[0], 1, len, fp) != buf.size()) {
        throw std::logic_error("");
    }
    printf("read %s:%s\n", path, buf.c_str());
    fclose(fp);

    return buf;
}

void write_file(const String &path, const String &buf) {
    printf("write %s: %s\n", path.c_str(), buf.c_str());
    FILE *fp = nullptr;
#ifdef _WIN32
    if (fopen_s(&fp, path.c_str(), "w+") && !fp) {
        // file not exists
        perror("open failed");
        throw std::logic_error("open file failed");
    }
#else
    fp = fopen(path.c_str(), "w+");
    if (!fp) {
        perror("open file failed!");
        throw std::logic_error("open file failed");
    }
#endif
    if (fwrite(buf.c_str(), 1, buf.size(), fp) != buf.size()) {
        perror("write failed");
        throw std::logic_error("write failed");
    }

    fclose(fp);
}
