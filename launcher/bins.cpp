#include <sdk/os/file.hpp>
#include <sdk/os/mem.hpp>
#include <sdk/os/string.hpp>
#include "bins.hpp"
#include <sdk/os/serial.hpp>

#define hex2asc(x) ((x)>9?((x)+'A'-10):((x)+'0'))

#define Serial_print(s) Serial_Write((const unsigned char*)s,sizeof(s)-1)

#define Serial_printhex(x)	Serial_WriteSingle( hex2asc( ((x)>>28)&0xf ) );\
							Serial_WriteSingle( hex2asc( ((x)>>24)&0xf ) );\
							Serial_WriteSingle( hex2asc( ((x)>>20)&0xf ) );\
							Serial_WriteSingle( hex2asc( ((x)>>16)&0xf ) );\
							Serial_WriteSingle( hex2asc( ((x)>>12)&0xf ) );\
							Serial_WriteSingle( hex2asc( ((x)>> 8)&0xf ) );\
							Serial_WriteSingle( hex2asc( ((x)>> 4)&0xf ) );\
							Serial_WriteSingle( hex2asc( ((x)>> 0)&0xf ) );

class File {
public:
	File() : m_opened(false), m_fd(-1) {

	}

	~File() {
		if (m_opened) {
			close(m_fd);
		}
	}

	int open(const char *path, int flags) {
		m_fd = ::open(path, flags);
		m_opened = m_fd >= 0;
		return m_fd;
	}

	int getAddr(int offset, const void **addr) {
		return ::getAddr(m_fd, offset, addr);
	}

	int getSize(uint32_t *size) {
		if (!size || !m_opened)
			return EINVAL;
		struct stat info;
		int result = ::fstat(m_fd, &info);
		if (result < 0)
			return result;
		*size = info.fileSize;
		return 0;
	}

	int read(void *buf, int count) {
		return ::read(m_fd, buf, count);
	}

private:
	bool m_opened;
	int m_fd;
};

class Find {
public:
	Find() : m_opened(false), m_findHandle(-1) {

	}

	~Find() {
		if (m_opened) {
			findClose(m_findHandle);
		}
	}

	int findFirst(const wchar_t *path, wchar_t *name, struct findInfo *findInfoBuf) {
		int ret = ::findFirst(path, &m_findHandle, name, findInfoBuf);
		m_opened = ret >= 0;
		return ret;
	}

	int findNext(wchar_t *name, struct findInfo *findInfoBuf) {
		return ::findNext(m_findHandle, name, findInfoBuf);
	}

private:
	bool m_opened;
	int m_findHandle;
};

namespace Bins {
    namespace {
    bool CopyMetadataField(char *destination, int capacity,
                           const char **cursor, uint32_t *remaining) {
        if (!destination || capacity <= 0 || !cursor || !*cursor ||
            !remaining)
            return false;

        int written = 0;
        while (*remaining > 0 && **cursor != 0) {
            if (written < capacity - 1)
                destination[written++] = **cursor;
            ++(*cursor);
            --(*remaining);
        }
        destination[written] = 0;
        if (*remaining == 0)
            return false;

        // Consume the terminator and skip empty fields, matching the legacy
        // BIN metadata parser while ensuring every read stays in file bounds.
        ++(*cursor);
        --(*remaining);
        while (*remaining > 0 && **cursor == 0) {
            ++(*cursor);
            --(*remaining);
        }
        return true;
    }
    }
	const char *BIN_FOLDER[] = {
		"\\fls0\\",
		"\\drv0\\"
	};
	const char FILE_MASK[] = "*.bin";

    struct AppInfo g_apps[MAX_APPS];
    int g_numApps;

	void LoadApp(const char *folder, wchar_t *fileName) {
		if (g_numApps >= MAX_APPS)
			return;
		struct AppInfo app;
		memset(&app, 0, sizeof(app));

		// copy the file name (converting to a non-wide string in the
		// process)
		for (int i = 0; i < 99 && fileName[i] != 0; ++i)
			app.fileName[i] = static_cast<char>(fileName[i]);
		app.fileName[99] = 0;

		// build the path
		//strcat(app.path, "\\fls0\\");
		strcat(app.path, folder);
		strcat(app.path, app.fileName);

		File f;
		int ret = f.open(app.path, OPEN_READ);
		if (ret < 0) {
			return;
		}
		uint32_t fileSize = 0;
		if (f.getSize(&fileSize) < 0 || fileSize <= 0x10)
			return;

		const char *binInfo = 0;
		if (f.getAddr(0x10, reinterpret_cast<const void **>(&binInfo)) < 0 ||
		    !binInfo)
			return;

		if (*binInfo >= 32 && *binInfo < 127) {
			const char *cursor = binInfo;
			uint32_t remaining = fileSize - 0x10;
			if (!CopyMetadataField(app.name, sizeof(app.name),
			                       &cursor, &remaining))
				return;
			if (remaining > 0)
				CopyMetadataField(app.description, sizeof(app.description),
				                 &cursor, &remaining);
			if (remaining > 0)
				CopyMetadataField(app.author, sizeof(app.author),
				                 &cursor, &remaining);
			if (remaining > 0)
				CopyMetadataField(app.version, sizeof(app.version),
				                 &cursor, &remaining);
		}
		g_apps[g_numApps++] = app;
	}

	void LoadAppInfo() {
		g_numApps = 0;

		for (unsigned int dirNr=0; dirNr<sizeof(BIN_FOLDER)/sizeof(BIN_FOLDER[0]);dirNr++){
			Find find;

			wchar_t fileName[100];
			struct findInfo findInfoBuf;

			wchar_t findDir[100];
			int i=0;
			while (BIN_FOLDER[dirNr][i]!=0){
				findDir[i] = (wchar_t)BIN_FOLDER[dirNr][i]; 
				i++;
			}
			int j=0;
			while (FILE_MASK[j]!=0){
				findDir[i] = (wchar_t)FILE_MASK[j];
				i++; j++;
			}
			findDir[i] = 0;
			int ret = find.findFirst(findDir, fileName, &findInfoBuf);
			while (ret >= 0) {
				if (findInfoBuf.type == findInfoBuf.EntryTypeFile) {
					const char exclude[] = "run.bin";
					bool isLauncher = true;
					for (unsigned int i = 0; i < sizeof(exclude); ++i) {
						if (fileName[i] != exclude[i]) {
							isLauncher = false;
							break;
						}
					}
					if (!isLauncher)
						LoadApp(BIN_FOLDER[dirNr], fileName);
				}
				ret = find.findNext(fileName, &findInfoBuf);
			}
		}
	}

	EntryPoint RunApp(int i) {
		if (i < 0 || i >= g_numApps)
			return nullptr;
		struct AppInfo *app = &g_apps[i];

		File f;
		int ret = f.open(app->path, OPEN_READ);
		if (ret < 0) {
		    return nullptr;
		}
		//get address where the program should be loaded
		unsigned char *address = 0;
		if (f.getAddr(0x0c, (const void **)&address) < 0 || !address)
			return nullptr;
		EntryPoint entrypoint = (EntryPoint)0x8cff0000;
		if (address[0]==0x8c)
				entrypoint = (EntryPoint)((address[0]<<24) + (address[1]<<16) + (address[2]<<8) + (address[3]));

		int length = f.read((void *)entrypoint, 0x20000);
		if (length <= 0)
			return nullptr;
		//unsigned char mode[] = {0,5,0,0,0,0};
		//Serial_Open(mode);
		//Serial_print("The length is: 0x");
		//Serial_printhex(length);
		//unsigned char c;
		//while(c!='\n') {
		//		Serial_ReadSingle(&c);
		//		if(c=='.'){
		//				Serial_Close(0);
		//				return nullptr;
		//		}
		//}
		//Serial_Close(0);
		return entrypoint;
    }
}
