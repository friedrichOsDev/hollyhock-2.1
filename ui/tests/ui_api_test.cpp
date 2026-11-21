#include <stdint.h>

#include <ui/filesystem.hpp>
#include <ui/types.hpp>

// Avoid including stdio.h here: the SDK declares remove()/rename() with
// signatures that conflict with the host C library on some toolchains.
// The test only needs printf(), so declare that function directly.
extern "C" int printf(const char *format, ...);

namespace {

int failures = 0;
int checks = 0;

void Check(bool condition, const char *name) {
    ++checks;
    if (condition) {
        printf("PASS: %s\n", name);
    } else {
        printf("FAIL: %s\n", name);
        ++failures;
    }
}

bool TextEquals(const char *left, const char *right) {
    if (!left || !right)
        return left == right;
    int i = 0;
    while (left[i] && right[i] && left[i] == right[i])
        ++i;
    return left[i] == right[i];
}

int g_openResult = 7;
int g_openCalls = 0;
int g_openFlags = 0;
int g_closeCalls = 0;
int g_readCalls = 0;
int g_seekCalls = 0;
int g_findFirstCalls = 0;
int g_findNextCalls = 0;
int g_findCloseCalls = 0;
int g_statResult = 0;
int g_fstatResult = 0;
char g_openPath[256] = {0};
uint32_t g_fileSize = 123;

void CopyWide(wchar_t *destination, const wchar_t *source, int capacity) {
    int i = 0;
    while (i < capacity - 1 && source[i]) {
        destination[i] = source[i];
        ++i;
    }
    destination[i] = 0;
}

void DummyAction(UI::Application &) {}

} // namespace

extern "C" int open(const char *path, int flags) {
    ++g_openCalls;
    int i = 0;
    while (path && path[i] && i < (int)sizeof(g_openPath) - 1) {
        g_openPath[i] = path[i];
        ++i;
    }
    g_openPath[i] = 0;
    g_openFlags = flags;
    return g_openResult;
}

extern "C" int close(int) {
    ++g_closeCalls;
    return 0;
}

extern "C" int read(int, void *buffer, int count) {
    ++g_readCalls;
    if (count > 0 && buffer) {
        char *bytes = static_cast<char *>(buffer);
        for (int i = 0; i < count; ++i)
            bytes[i] = 'x';
    }
    return count;
}

extern "C" int lseek(int, int offset, int) {
    ++g_seekCalls;
    return offset;
}

extern "C" int fstat(int, struct stat *info) {
    if (g_fstatResult < 0)
        return g_fstatResult;
    if (info)
        info->fileSize = g_fileSize;
    return 0;
}

extern "C" int stat(const char *, struct stat *info) {
    if (g_statResult < 0)
        return g_statResult;
    if (info)
        info->fileSize = g_fileSize;
    return 0;
}

extern "C" int findFirst(const wchar_t *path, int *handle,
                         wchar_t *name, struct findInfo *info) {
    ++g_findFirstCalls;
    if (!path || !handle || !name || !info)
        return EINVAL;

    *handle = 42;
    CopyWide(name, L"first.txt", 128);
    info->type = findInfo::EntryTypeFile;
    info->size = 5;
    return 0;
}

extern "C" int findNext(int, wchar_t *name, struct findInfo *info) {
    ++g_findNextCalls;
    if (g_findNextCalls == 1) {
        CopyWide(name, L"Second", 128);
        info->type = findInfo::EntryTypeDirectory;
        info->size = 0;
        return 0;
    }
    return ENOPATH;
}

extern "C" int findClose(int handle) {
    if (handle != 42)
        return EINVAL;
    ++g_findCloseCalls;
    return 0;
}

int main() {
    const UI::FileFilter filters[] = {
        UI::FileFilter(".txt", "Text"),
        UI::FileFilter("txt", "Text without leading dot"),
        UI::FileFilter("CPV2", "Video"),
        UI::FileFilter("md", "Markdown")
    };

    Check(UI::FileFilterMatches("readme.txt", filters, 4),
          "leading-dot extension");
    Check(UI::FileFilterMatches("VIDEO.CPV2", filters, 4),
          "case-insensitive extension without dot");
    Check(UI::FileFilterMatches("README.MD", filters, 4),
          "extension without dot requires a dot boundary");
    Check(!UI::FileFilterMatches("notxt", filters, 4),
          "reject extension text without a dot boundary");
    Check(!UI::FileFilterMatches("readme.md.bak", filters, 4),
          "reject a non-terminal extension");
    Check(!UI::FileFilterMatches("readme.bin", filters, 4),
          "reject unmatched extension");
    Check(UI::FileFilterMatches("any.bin", 0, 0),
          "null filter list means allow all");
    Check(UI::FileFilterMatches("any.bin", filters, 0),
          "zero filter count means allow all");
    Check(!UI::FileFilterMatches(0, filters, 4),
          "null filename rejected");
    Check(!UI::FileFilterMatches("", filters, 4),
          "empty filename rejected");
    const UI::FileFilter emptyFilter[] = {
        UI::FileFilter("", "ignored")
    };
    Check(!UI::FileFilterMatches("file", emptyFilter, 1),
          "empty extension ignored");

    bool enabled = false;
    int amount = 7;
    int mode = 1;
    const char *options[] = {"A", "B", "C"};
    UI::Setting boolean = UI::Setting::Boolean("Enabled", &enabled);
    UI::Setting integer = UI::Setting::Integer("Amount", &amount, 0, 10, 2);
    UI::Setting enumeration =
        UI::Setting::Enumeration("Mode", &mode, options, 3);
    UI::Setting action = UI::Setting::Action("Action", DummyAction);

    Check(boolean.type == UI::SettingBoolean && boolean.value == &enabled,
          "boolean setting factory");
    Check(integer.type == UI::SettingInteger && integer.minimum == 0 &&
          integer.maximum == 10 && integer.step == 2 &&
          integer.value == &amount, "integer setting factory");
    Check(enumeration.type == UI::SettingEnum &&
          enumeration.optionCount == 3 && enumeration.options == options &&
          enumeration.value == &mode, "enumeration setting factory");
    Check(action.type == UI::SettingAction && action.action == DummyAction,
          "action setting factory");

    UI::Config config;
    Check(config.showStart && config.showQuit &&
          config.manageCalculatorLifecycle, "configuration defaults");
    Check(!config.showFileSelector && !config.showSettings &&
          !config.onStart, "optional entries default off");
    Check(config.settingsFolder == 0,
          "persistent settings disabled by default");

    char path[256];
    Check(UI::FileSystem::NormalizePath(
              "fls0/folder/file.txt", path, sizeof(path)) == 0 &&
          TextEquals(path, "\\fls0\\folder\\file.txt"),
          "normalize relative fls0 path");
    Check(UI::FileSystem::NormalizePath(
              "/fls0/file.txt", path, sizeof(path)) == 0 &&
          TextEquals(path, "\\fls0\\file.txt"),
          "normalize absolute forward-slash path");
    Check(UI::FileSystem::NormalizePath(
              "\\\\fls0\\\\file.txt", path, sizeof(path)) == 0 &&
          TextEquals(path, "\\fls0\\file.txt"),
          "normalize repeated backslashes");
    Check(UI::FileSystem::NormalizePath(
              "fls0//folder/./file.txt", path, sizeof(path)) == 0 &&
          TextEquals(path, "\\fls0\\folder\\file.txt"),
          "collapse separators and current-directory components");
    Check(UI::FileSystem::NormalizePath(
              "FLS0", path, sizeof(path)) == 0 &&
          TextEquals(path, "\\fls0\\"),
          "normalize volume root and case");
    Check(UI::FileSystem::NormalizePath(
              "fls0/folder/../secret", path, sizeof(path)) == EACCES &&
          path[0] == 0, "reject parent traversal and clear output");
    Check(UI::FileSystem::NormalizePath(
              "other/file.txt", path, sizeof(path)) == ENOVOLUME &&
          path[0] == 0, "reject unsupported volume");
    Check(UI::FileSystem::NormalizePath(
              "fls0/file:name.txt", path, sizeof(path)) == EINVAL,
          "reject reserved path characters");
    Check(UI::FileSystem::NormalizePath(
              "fls0/trailing.", path, sizeof(path)) == EINVAL,
          "reject trailing dot");
    Check(UI::FileSystem::NormalizePath(
              "fls0/trailing ", path, sizeof(path)) == EINVAL,
          "reject trailing space");

    char unicodePath[] = "fls0/caf\xC3\xA9.txt";
    Check(UI::FileSystem::NormalizePath(
              unicodePath, path, sizeof(path)) == EINVAL,
          "reject non-ASCII paths");
    Check(UI::FileSystem::NormalizePath(
              "fls0/file.txt", path, 7) == ENAMETOOLONG && path[0] == 0,
          "reject undersized output buffer");
    char shortOutput[6] = "abcde";
    Check(UI::FileSystem::NormalizePath(
              "fls0", shortOutput, sizeof(shortOutput)) == ENAMETOOLONG &&
          shortOutput[0] == 0, "reject buffer smaller than volume root");
    Check(UI::FileSystem::NormalizePath(
              0, path, sizeof(path)) == EINVAL,
          "reject null input");
    Check(UI::FileSystem::NormalizePath(
              "", path, sizeof(path)) == EINVAL,
          "reject empty input");
    char aliasedPath[64] = "fls0/alias.txt";
    Check(UI::FileSystem::NormalizePath(
              aliasedPath, aliasedPath, sizeof(aliasedPath)) == 0 &&
          TextEquals(aliasedPath, "\\fls0\\alias.txt"),
          "support in-place path normalization");

    UI::File file;
    Check(!file.IsOpen(), "file is initially closed");
    Check(file.Read(0, 1) == EBADF, "read on closed file returns EBADF");
    g_openResult = 7;
    const int openResult = file.Open("fls0/file.txt");
    Check(openResult == 7 && file.IsOpen(), "open file");
    Check(g_openFlags == OPEN_READ &&
          TextEquals(g_openPath, "\\fls0\\file.txt"),
          "file wrapper always uses read-only mode");
    char data[4];
    Check(file.Read(data, sizeof(data)) == 4 && data[0] == 'x',
          "read file data");
    Check(file.Read(0, 1) == EINVAL,
          "reject null read buffer for non-zero count");
    Check(file.Read(0, 0) == 0,
          "allow zero-byte read with null buffer");
    Check(file.Read(data, -1) == EINVAL,
          "reject negative read size");
    Check(file.Seek(12, SEEK_SET) == 12,
          "seek file");
    uint32_t fileSize = 0;
    Check(file.GetSize(&fileSize) == 0 && fileSize == g_fileSize,
          "get file size");
    Check(file.Close() == 0 && !file.IsOpen(),
          "close file");
    const int closedBefore = g_closeCalls;
    Check(file.Close() == 0 && g_closeCalls == closedBefore,
          "closing an already closed file is harmless");

    g_openResult = 9;
    {
        UI::File automaticClose;
        Check(automaticClose.Open("fls0/auto.txt") == 9,
              "open file for destructor test");
    }
    Check(g_closeCalls == closedBefore + 1,
          "file destructor closes its descriptor");

    g_openResult = 10;
    Check(file.Open("fls0/old.txt") == 10, "open file before failed reopen");
    g_openResult = ENOPATH;
    Check(file.Open("fls0/missing.txt") == ENOPATH && !file.IsOpen(),
          "failed reopen closes previous descriptor");
    g_openResult = 7;

    struct stat info;
    Check(UI::FileSystem::Stat("fls0/file.txt", &info) == 0 &&
          info.fileSize == g_fileSize, "stat file metadata");
    Check(UI::FileSystem::Exists("fls0/file.txt"),
          "exists returns true for existing paths");
    g_statResult = ENOPATH;
    Check(!UI::FileSystem::Exists("fls0/missing.txt"),
          "exists returns false when stat fails");
    g_statResult = 0;

    UI::Directory directory;
    Check(directory.Open("fls0/") == 0 && directory.IsOpen(),
          "open directory");
    UI::DirectoryEntry entry;
    Check(directory.Next(&entry) == 0 &&
          TextEquals(entry.name, "first.txt") && !entry.isDirectory &&
          entry.size == 5, "directory returns first entry");
    Check(directory.Next(&entry) == 0 &&
          TextEquals(entry.name, "Second") && entry.isDirectory,
          "directory returns subsequent entry");
    Check(directory.Next(&entry) == ENOPATH,
          "directory propagates end-of-search result");
    Check(directory.Close() == 0 && !directory.IsOpen() &&
          g_findCloseCalls == 1, "close directory search handle");
    const int findClosesBefore = g_findCloseCalls;
    Check(directory.Close() == 0 &&
          g_findCloseCalls == findClosesBefore,
          "closing an already closed directory is harmless");

    {
        UI::Directory automaticClose;
        Check(automaticClose.Open("fls0/") == 0,
              "open directory for destructor test");
    }
    Check(g_findCloseCalls == findClosesBefore + 1,
          "directory destructor closes its search handle");

    printf("\n%d checks, %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
