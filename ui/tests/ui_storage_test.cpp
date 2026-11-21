#include <stdint.h>

#include <ui/storage.hpp>
#include <sdk/os/file.hpp>

// The SDK and host libc use different declarations for some functions.
// Declare only the output routine required by this test.
extern "C" int printf(const char *format, ...);

namespace {

int failures = 0;
int checks = 0;
int createResult = 0;
int createCalls = 0;
int getResult = 0;
int setResult = 0;
int getCalls = 0;
int setCalls = 0;
uint8_t createIndex = 7;
uint8_t storedType = VARTYPE_STR;
char storedName[32] = "SETTINGS";
uint8_t storedData[64] = {0};
uint32_t storedSize = 0;
char lastFolder[16] = {0};
char lastVariable[16] = {0};
uint8_t lastType = 0;
uint32_t lastSize = 0;
uint8_t lastData[64] = {0};

void Check(bool condition, const char *name) {
    ++checks;
    if (condition) {
        printf("PASS: %s\n", name);
    } else {
        printf("FAIL: %s\n", name);
        ++failures;
    }
}

bool Equals(const char *left, const char *right) {
    if (!left || !right)
        return left == right;
    int i = 0;
    while (left[i] && right[i] && left[i] == right[i])
        ++i;
    return left[i] == right[i];
}

void Copy(char *destination, const char *source, int capacity) {
    int i = 0;
    while (source && source[i] && i < capacity - 1) {
        destination[i] = source[i];
        ++i;
    }
    destination[i] = 0;
}

void ResetStoreMock() {
    createResult = 0;
    createCalls = 0;
    getResult = 0;
    setResult = 0;
    getCalls = 0;
    setCalls = 0;
    createIndex = 7;
    storedType = VARTYPE_STR;
    Copy(storedName, "SETTINGS", sizeof(storedName));
    for (unsigned int i = 0; i < sizeof(storedData); ++i)
        storedData[i] = 0;
    storedSize = 0;
    lastFolder[0] = 0;
    lastVariable[0] = 0;
    lastType = 0;
    lastSize = 0;
    for (unsigned int i = 0; i < sizeof(lastData); ++i)
        lastData[i] = 0;
}

} // namespace

extern "C" int MCS_CreateFolder(const char *folder, uint8_t *folderIndex) {
    ++createCalls;
    Copy(lastFolder, folder, sizeof(lastFolder));
    if (folderIndex)
        *folderIndex = createIndex;
    return createResult;
}

extern "C" int MCS_GetVariable(
    const char *folder, const char *name, uint8_t *type,
    char **name2, void **data, uint32_t *size) {
    ++getCalls;
    Copy(lastFolder, folder, sizeof(lastFolder));
    Copy(lastVariable, name, sizeof(lastVariable));
    if (getResult != 0)
        return getResult;
    if (type)
        *type = storedType;
    if (name2)
        *name2 = storedName;
    if (data)
        *data = storedData;
    if (size)
        *size = storedSize;
    return 0;
}

extern "C" int MCS_SetVariable(
    const char *folder, const char *name, uint8_t type,
    uint32_t size, void *data) {
    ++setCalls;
    Copy(lastFolder, folder, sizeof(lastFolder));
    Copy(lastVariable, name, sizeof(lastVariable));
    lastType = type;
    lastSize = size;
    if (size > sizeof(lastData))
        return EOUTOFBOUND;
    const uint8_t *source = static_cast<const uint8_t *>(data);
    for (uint32_t i = 0; i < size; ++i)
        lastData[i] = source[i];
    return setResult;
}

int main() {
    ResetStoreMock();
    UI::PersistentStore store;

    Check(!store.IsOpen() && store.Folder() == 0,
          "store starts closed");
    Check(store.Open("mydata") == 0 && store.IsOpen() &&
          Equals(store.Folder(), "MYDATA") &&
          Equals(lastFolder, "MYDATA") && createCalls == 1,
          "Open validates and normalizes folder names");

    Check(store.Open("12345678") == 0 &&
          Equals(store.Folder(), "12345678"),
          "Open accepts the maximum eight-character folder");

    const int beforeInvalid = createCalls;
    Check(store.Open("") == EINVAL && !store.IsOpen() &&
          createCalls == beforeInvalid,
          "Open rejects empty folder names");
    Check(store.Open("TOO-LONG-NAME") == EINVAL && !store.IsOpen() &&
          createCalls == beforeInvalid,
          "Open rejects names longer than eight characters");
    Check(store.Open("bad/name") == EINVAL && !store.IsOpen() &&
          createCalls == beforeInvalid,
          "Open rejects non-alphanumeric folder characters");

    createResult = MCS_FOLDER_EXISTS;
    Check(store.Open("prefs") == 0 && store.IsOpen() &&
          Equals(store.Folder(), "PREFS"),
          "Open accepts an existing MCS folder");

    createResult = 0;
    getResult = MCS_NO_VARIABLE;
    uint8_t loadedType = 99;
    uint32_t loadedSize = 99;
    Check(store.Load("SETTINGS", &loadedType, storedData,
                     sizeof(storedData), &loadedSize) == MCS_NO_VARIABLE &&
          loadedType == 0 && loadedSize == 0,
          "Load initializes output metadata when the variable is missing");

    getResult = 0;
    storedType = VARTYPE_STR;
    storedSize = 8;
    storedData[0] = 'A';
    storedData[1] = 'B';
    storedData[2] = 'C';
    storedData[3] = 'D';
    storedData[4] = 0;
    storedData[5] = 0;
    storedData[6] = 0;
    storedData[7] = 0;
    uint8_t output[8] = {0};
    Check(store.Load("SETTINGS", &loadedType, output,
                     sizeof(output), &loadedSize) == 0 &&
          loadedType == VARTYPE_STR && loadedSize == 8 &&
          output[0] == 'A' && output[3] == 'D',
          "Load copies aligned MCS variable data");

    uint8_t smallOutput[4] = {0};
    Check(store.Load("SETTINGS", &loadedType, smallOutput,
                     sizeof(smallOutput), &loadedSize) == EOUTOFBOUND &&
          loadedType == VARTYPE_STR && loadedSize == 8,
          "Load preserves metadata when the destination is too small");

    Check(store.Load("bad/name", &loadedType, output,
                     sizeof(output), &loadedSize) == EINVAL,
          "Load rejects invalid variable names");

    const uint8_t payload[5] = {1, 2, 3, 4, 5};
    Check(store.Save("SETTINGS", VARTYPE_STR, payload,
                     sizeof(payload)) == 0 &&
          setCalls == 1 && lastSize == 8 && lastType == VARTYPE_STR &&
          Equals(lastFolder, "PREFS") &&
          Equals(lastVariable, "SETTINGS"),
          "Save pads payload size to a four-byte boundary");
    Check(lastData[0] == 1 && lastData[4] == 5 &&
          lastData[5] == 0 && lastData[6] == 0 && lastData[7] == 0,
          "Save zero-initializes alignment padding");

    Check(store.Save("BAD/NAME", VARTYPE_STR, payload,
                     sizeof(payload)) == EINVAL,
          "Save rejects invalid variable names");
    Check(store.Save("SETTINGS", VARTYPE_STR, 0, 1) == EINVAL,
          "Save rejects a null data pointer");
    setResult = MCS_NO_FOLDER;
    Check(store.Save("SETTINGS", VARTYPE_STR, payload,
                     sizeof(payload)) == MCS_NO_FOLDER,
          "Save propagates MCS errors");
    setResult = 0;

    store.Close();
    Check(!store.IsOpen() && store.Folder() == 0 &&
          store.Load("SETTINGS", &loadedType, output,
                     sizeof(output), &loadedSize) == MCS_NO_FOLDER &&
          store.Save("SETTINGS", VARTYPE_STR, payload,
                     sizeof(payload)) == MCS_NO_FOLDER,
          "closed store rejects reads and writes");

    printf("\n%d checks, %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
