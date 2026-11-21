#include <sdk/os/file.hpp>
#include <sdk/os/mem.hpp>
#include <sdk/os/string.hpp>
#include "apps.hpp"
#include "elf.h"
#include <sdk/os/serial.hpp>

#define hex2asc(x) ((x)>9?((x)+'A'-10):((x)+'0'))

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

namespace Apps {
	const char *HHK_FOLDER[] = {
		"\\fls0\\",
		"\\drv0\\"
	};
	const char FILE_MASK[] = "*.hhk";

    struct AppInfo g_apps[MAX_APPS];
    int g_numApps;

    bool SectionNameEquals(const char *table, uint32_t tableSize,
                           uint32_t offset, const char *expected) {
        if (!table || !expected || offset >= tableSize)
            return false;
        uint32_t index = 0;
        while (expected[index]) {
            if (index >= tableSize - offset ||
                table[offset + index] != expected[index])
                return false;
            ++index;
        }
        return offset + index < tableSize && table[offset + index] == 0;
    }

    void CopySectionText(char *destination, int capacity,
                         const char *source, uint32_t sourceSize) {
        if (!destination || capacity <= 0)
            return;
        uint32_t index = 0;
        while (index < sourceSize && index < static_cast<uint32_t>(capacity - 1) &&
               source[index]) {
            destination[index] = source[index];
            ++index;
        }
        destination[index] = 0;
    }

    const Elf32_Ehdr *LoadELF(File &f, const Elf32_Shdr **sectionHeaders,
                              uint32_t *fileSize) {
        if (!sectionHeaders || !fileSize)
            return nullptr;

        const Elf32_Ehdr *elf = nullptr;
        if (f.getSize(fileSize) < 0 || *fileSize < sizeof(Elf32_Ehdr) ||
            f.getAddr(0, reinterpret_cast<const void **>(&elf)) < 0 || !elf)
            return nullptr;

        if (!(elf->e_ident[EI_MAG0] == ELFMAG0 &&
              elf->e_ident[EI_MAG1] == ELFMAG1 &&
              elf->e_ident[EI_MAG2] == ELFMAG2 &&
              elf->e_ident[EI_MAG3] == ELFMAG3))
            return nullptr;
        if (elf->e_ident[EI_CLASS] != ELFCLASS32 ||
            elf->e_ident[EI_DATA] != ELFDATA2MSB ||
            elf->e_ident[EI_VERSION] != EV_CURRENT ||
            elf->e_ident[EI_OSABI] != ELFOSABI_SYSV ||
            elf->e_type != ET_EXEC ||
            elf->e_machine != EM_SH ||
            elf->e_version != EV_CURRENT)
            return nullptr;

        if (elf->e_shentsize != sizeof(Elf32_Shdr) ||
            elf->e_shnum == 0 ||
            elf->e_shstrndx >= elf->e_shnum ||
            elf->e_shoff > *fileSize ||
            elf->e_shnum > (*fileSize - elf->e_shoff) / sizeof(Elf32_Shdr))
            return nullptr;

        *sectionHeaders = reinterpret_cast<const Elf32_Shdr *>(
            reinterpret_cast<const uint8_t *>(elf) + elf->e_shoff);

        const Elf32_Shdr *strings = &(*sectionHeaders)[elf->e_shstrndx];
        if (strings->sh_type != SHT_STRTAB ||
            strings->sh_offset > *fileSize ||
            strings->sh_size > *fileSize - strings->sh_offset)
            return nullptr;

        for (uint32_t i = 0; i < elf->e_shnum; ++i) {
            const Elf32_Shdr *section = &(*sectionHeaders)[i];
            if (section->sh_type != SHT_NOBITS &&
                section->sh_type != SHT_NULL &&
                (section->sh_offset > *fileSize ||
                 section->sh_size > *fileSize - section->sh_offset))
                return nullptr;
            if (section->sh_type != SHT_NULL &&
                section->sh_name >= strings->sh_size)
                return nullptr;
        }

        return elf;
    }

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

		const Elf32_Shdr *sectionHeaders = nullptr;
		uint32_t fileSize = 0;
		const Elf32_Ehdr *elf = LoadELF(f, &sectionHeaders, &fileSize);

		if (elf == nullptr) {
			return;
		}

		const Elf32_Shdr *sectionHeaderStringTable = &sectionHeaders[elf->e_shstrndx];
		const char *stringTable = reinterpret_cast<const char *>(
			reinterpret_cast<const uint8_t *>(elf) +
			sectionHeaderStringTable->sh_offset);
		for (int i = 0; i < elf->e_shnum; ++i) {
			const Elf32_Shdr *sectionHeader = &sectionHeaders[i];

			// skip the first empty section header
			if (sectionHeader->sh_type == SHT_NULL) {
				continue;
			}

			if (sectionHeader->sh_type != SHT_PROGBITS)
				continue;

			const char *sectionData = reinterpret_cast<const char *>(
				reinterpret_cast<const uint8_t *>(elf) +
				sectionHeader->sh_offset);

			if (SectionNameEquals(stringTable, sectionHeaderStringTable->sh_size,
			                      sectionHeader->sh_name, ".hollyhock_name")) {
				CopySectionText(app.name, sizeof(app.name), sectionData,
				                sectionHeader->sh_size);
			} else if (SectionNameEquals(stringTable,
			                            sectionHeaderStringTable->sh_size,
			                            sectionHeader->sh_name,
			                            ".hollyhock_description")) {
				CopySectionText(app.description, sizeof(app.description),
				                sectionData, sectionHeader->sh_size);
			} else if (SectionNameEquals(stringTable,
			                            sectionHeaderStringTable->sh_size,
			                            sectionHeader->sh_name,
			                            ".hollyhock_author")) {
				CopySectionText(app.author, sizeof(app.author), sectionData,
				                sectionHeader->sh_size);
			} else if (SectionNameEquals(stringTable,
			                            sectionHeaderStringTable->sh_size,
			                            sectionHeader->sh_name,
			                            ".hollyhock_version")) {
				CopySectionText(app.version, sizeof(app.version), sectionData,
				                sectionHeader->sh_size);
			}
		}

		g_apps[g_numApps++] = app;
	}

	void LoadAppInfo() {
		g_numApps = 0;

		for (unsigned int dirNr=0; dirNr<sizeof(HHK_FOLDER)/sizeof(HHK_FOLDER[0]);dirNr++){
			Find find;

			wchar_t fileName[100];
			struct findInfo findInfoBuf;

			wchar_t findDir[100];
			int i=0;
			while (HHK_FOLDER[dirNr][i]!=0){
				findDir[i] = (wchar_t)HHK_FOLDER[dirNr][i]; 
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
					LoadApp(HHK_FOLDER[dirNr],fileName);
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

        const Elf32_Shdr *sectionHeaders = nullptr;
        uint32_t fileSize = 0;
        const Elf32_Ehdr *elf = LoadELF(f, &sectionHeaders, &fileSize);

		if (elf == nullptr) {
			return nullptr;
		}

		for (int i = 0; i < elf->e_shnum; ++i) {
			const Elf32_Shdr *sectionHeader = &sectionHeaders[i];

			// skip the first empty section header
			if (sectionHeader->sh_type == SHT_NULL) {
				continue;
			}

			const void *sectionData = reinterpret_cast<const void *>(
				reinterpret_cast<const uint8_t *>(elf) +
				sectionHeader->sh_offset
			);

			if ((sectionHeader->sh_flags & SHF_ALLOC) == SHF_ALLOC) {
				void *dest = reinterpret_cast<void *>(sectionHeader->sh_addr);

				if (sectionHeader->sh_type == SHT_PROGBITS) {
					memcpy(dest, sectionData, sectionHeader->sh_size);
				} else if (sectionHeader->sh_type == SHT_NOBITS) {
					memset(dest, 0, sectionHeader->sh_size);
				}
			}
		}

		return reinterpret_cast<EntryPoint>(elf->e_entry);
    }
}
