/**
 * vim: set ts=4 sw=4 tw=99 noet :
 * =============================================================================
 * MemoryUtils
 * Copyright (C) 2004-2011 AlliedModders LLC., 2011 Prodigysim
 *  All rights reserved.
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, the authors give you permission to link the
 * code of this program (as well as its derivative works) to "Half-Life 2," the
 * "Source Engine," the "SourcePawn JIT," and any Game MODs that run on software
 * by the Valve Corporation.  You must obey the GNU General Public License in
 * all respects for all other code used.  Additionally, AlliedModders LLC grants
 * this exception to all derivative works.  AlliedModders LLC defines further
 * exceptions, found in LICENSE.txt (as of this writing, version JULY-31-2007),
 * or <http://www.sourcemod.net/license.php>.
 */

#include "memutils.h"
#include <string.h>

#if SH_SYS == SH_SYS_LINUX
#include <fcntl.h>
#include <link.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define PAGE_SIZE			4096
#define PAGE_ALIGN_UP(x)	((x + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define ALIGN(ar) ((long)ar & ~(PAGE_SIZE-1))
#define PAGE_EXECUTE_READWRITE  PROT_READ|PROT_WRITE|PROT_EXEC
#endif

#if SH_SYS == SH_SYS_APPLE
#include <AvailabilityMacros.h>
#include <mach/task.h>
#include <mach-o/dyld_images.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>

/* Define things from 10.6 SDK for older SDKs */
#ifndef MAC_OS_X_VERSION_10_6
struct task_dyld_info
{
	mach_vm_address_t all_image_info_addr;
	mach_vm_size_t all_image_info_size;
};
typedef struct task_dyld_info task_dyld_info_data_t;
#define TASK_DYLD_INFO 17
#define TASK_DYLD_INFO_COUNT (sizeof(task_dyld_info_data_t) / sizeof(natural_t))
#endif // MAC_OS_X_VERSION_10_6
#endif // SH_SYS_APPLE

MemoryUtils g_MemUtils;

MemoryUtils::MemoryUtils()
{
#if SH_SYS == SH_SYS_APPLE

	Gestalt(gestaltSystemVersionMajor, &m_OSXMajor);
	Gestalt(gestaltSystemVersionMinor, &m_OSXMinor);

	/* Get pointer to struct that describes all loaded mach-o images in process */
	if ((m_OSXMajor == 10 && m_OSXMinor >= 6) || m_OSXMajor > 10)
	{
		task_dyld_info_data_t dyld_info;
		mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;
		task_info(mach_task_self(), TASK_DYLD_INFO, (task_info_t)&dyld_info, &count);
		m_ImageList = (struct dyld_all_image_infos *)dyld_info.all_image_info_addr;
	}
	else
	{
		struct nlist list[2];
		memset(list, 0, sizeof(list));
		list[0].n_un.n_name = (char *)"_dyld_all_image_infos";
		nlist("/usr/lib/dyld", list);
		m_ImageList = (struct dyld_all_image_infos *)list[0].n_value;
	}

#endif
}

MemoryUtils::~MemoryUtils()
{
#if SH_SYS == SH_SYS_LINUX || SH_SYS == SH_SYS_APPLE
	for (size_t i = 0; i < m_SymTables.size(); i++)
	{
		delete m_SymTables[i];
	}
	m_SymTables.clear();
#endif
}

void *MemoryUtils::ResolveSymbol(void *handle, const char *symbol)
{
#if SH_SYS == SH_SYS_WIN32

	return GetProcAddress((HMODULE)handle, symbol);
	
#elif SH_SYS == SH_SYS_LINUX

	struct link_map *dlmap;
	struct stat dlstat;
	int dlfile;
	uintptr_t map_base;
	Elf32_Ehdr *file_hdr;
	Elf32_Shdr *sections, *shstrtab_hdr, *symtab_hdr, *strtab_hdr;
	Elf32_Sym *symtab;
	const char *shstrtab, *strtab;
	uint16_t section_count;
	uint32_t symbol_count;
	LibSymbolTable *libtable;
	SymbolTable *table;
	Symbol *symbol_entry;

	dlmap = (struct link_map *)handle;
	symtab_hdr = NULL;
	strtab_hdr = NULL;
	table = NULL;
	
	/* See if we already have a symbol table for this library */
	for (size_t i = 0; i < m_SymTables.size(); i++)
	{
		libtable = m_SymTables[i];
		if (libtable->lib_base == dlmap->l_addr)
		{
			table = &libtable->table;
			break;
		}
	}

	/* If we don't have a symbol table for this library, then create one */
	if (table == NULL)
	{
		libtable = new LibSymbolTable();
		libtable->table.Initialize();
		libtable->lib_base = dlmap->l_addr;
		libtable->last_pos = 0;
		table = &libtable->table;
		m_SymTables.push_back(libtable);
	}

	/* See if the symbol is already cached in our table */
	symbol_entry = table->FindSymbol(symbol, strlen(symbol));
	if (symbol_entry != NULL)
	{
		return symbol_entry->address;
	}

	/* If symbol isn't in our table, then we have open the actual library */
	dlfile = open(dlmap->l_name, O_RDONLY);
	if (dlfile == -1 || fstat(dlfile, &dlstat) == -1)
	{
		close(dlfile);
		return NULL;
	}

	/* Map library file into memory */
	file_hdr = (Elf32_Ehdr *)mmap(NULL, dlstat.st_size, PROT_READ, MAP_PRIVATE, dlfile, 0);
	map_base = (uintptr_t)file_hdr;
	if (file_hdr == MAP_FAILED)
	{
		close(dlfile);
		return NULL;
	}
	close(dlfile);

	if (file_hdr->e_shoff == 0 || file_hdr->e_shstrndx == SHN_UNDEF)
	{
		munmap(file_hdr, dlstat.st_size);
		return NULL;
	}

	sections = (Elf32_Shdr *)(map_base + file_hdr->e_shoff);
	section_count = file_hdr->e_shnum;
	/* Get ELF section header string table */
	shstrtab_hdr = &sections[file_hdr->e_shstrndx];
	shstrtab = (const char *)(map_base + shstrtab_hdr->sh_offset);

	/* Iterate sections while looking for ELF symbol table and string table */
	for (uint16_t i = 0; i < section_count; i++)
	{
		Elf32_Shdr &hdr = sections[i];
		const char *section_name = shstrtab + hdr.sh_name;

		if (strcmp(section_name, ".symtab") == 0)
		{
			symtab_hdr = &hdr;
		}
		else if (strcmp(section_name, ".strtab") == 0)
		{
			strtab_hdr = &hdr;
		}
	}

	/* Uh oh, we don't have a symbol table or a string table */
	if (symtab_hdr == NULL || strtab_hdr == NULL)
	{
		munmap(file_hdr, dlstat.st_size);
		return NULL;
	}

	symtab = (Elf32_Sym *)(map_base + symtab_hdr->sh_offset);
	strtab = (const char *)(map_base + strtab_hdr->sh_offset);
	symbol_count = symtab_hdr->sh_size / symtab_hdr->sh_entsize;

	/* Iterate symbol table starting from the position we were at last time */
	for (uint32_t i = libtable->last_pos; i < symbol_count; i++)
	{
		Elf32_Sym &sym = symtab[i];
		unsigned char sym_type = ELF32_ST_TYPE(sym.st_info);
		const char *sym_name = strtab + sym.st_name;
		Symbol *cur_sym;

		/* Skip symbols that are undefined or do not refer to functions or objects */
		if (sym.st_shndx == SHN_UNDEF || (sym_type != STT_FUNC && sym_type != STT_OBJECT))
		{
			continue;
		}

		/* Caching symbols as we go along */
		cur_sym = table->InternSymbol(sym_name, strlen(sym_name), (void *)(dlmap->l_addr + sym.st_value));
		if (strcmp(symbol, sym_name) == 0)
		{
			symbol_entry = cur_sym;
			libtable->last_pos = ++i;
			break;
		}
	}

	munmap(file_hdr, dlstat.st_size);
	return symbol_entry ? symbol_entry->address : NULL;

#elif SH_SYS == SH_SYS_APPLE
	
	uintptr_t dlbase, linkedit_addr;
	uint32_t image_count;
	struct mach_header *file_hdr;
	struct load_command *loadcmds;
	struct segment_command *linkedit_hdr;
	struct symtab_command *symtab_hdr;
	struct nlist *symtab;
	const char *strtab;
	uint32_t loadcmd_count;
	uint32_t symbol_count;
	LibSymbolTable *libtable;
	SymbolTable *table;
	Symbol *symbol_entry;
	
	dlbase = 0;
	image_count = m_ImageList->infoArrayCount;
	linkedit_hdr = NULL;
	symtab_hdr = NULL;
	table = NULL;
	
	/* Loop through mach-o images in process.
	 * We can skip index 0 since that is just the executable.
	 */
	for (uint32_t i = 1; i < image_count; i++)
	{
		const struct dyld_image_info &info = m_ImageList->infoArray[i];
		
		/* "Load" each one until we get a matching handle */
		void *h = dlopen(info.imageFilePath, RTLD_NOLOAD);
		if (h == handle)
		{
			dlbase = (uintptr_t)info.imageLoadAddress;
			dlclose(h);
			break;
		}
		
		dlclose(h);
	}
	
	if (!dlbase)
	{
		/* Uh oh, we couldn't find a matching handle */
		return NULL;
	}
	
	/* See if we already have a symbol table for this library */
	for (size_t i = 0; i < m_SymTables.size(); i++)
	{
		libtable = m_SymTables[i];
		if (libtable->lib_base == dlbase)
		{
			table = &libtable->table;
			break;
		}
	}
	
	/* If we don't have a symbol table for this library, then create one */
	if (table == NULL)
	{
		libtable = new LibSymbolTable();
		libtable->table.Initialize();
		libtable->lib_base = dlbase;
		libtable->last_pos = 0;
		table = &libtable->table;
		m_SymTables.push_back(libtable);
	}
	
	/* See if the symbol is already cached in our table */
	symbol_entry = table->FindSymbol(symbol, strlen(symbol));
	if (symbol_entry != NULL)
	{
		return symbol_entry->address;
	}
	
	/* If symbol isn't in our table, then we have to locate it in memory */
	
	file_hdr = (struct mach_header *)dlbase;
	loadcmds = (struct load_command *)(dlbase + sizeof(struct mach_header));
	loadcmd_count = file_hdr->ncmds;
	
	/* Loop through load commands until we find the ones for the symbol table */
	for (uint32_t i = 0; i < loadcmd_count; i++)
	{
		if (loadcmds->cmd == LC_SEGMENT && !linkedit_hdr)
		{
			struct segment_command *seg = (struct segment_command *)loadcmds;
			if (strcmp(seg->segname, "__LINKEDIT") == 0)
			{
				linkedit_hdr = seg;
				if (symtab_hdr)
				{
					break;
				}
			}
		}
		else if (loadcmds->cmd == LC_SYMTAB)
		{
			symtab_hdr = (struct symtab_command *)loadcmds;
			if (linkedit_hdr)
			{
				break;
			}
		}

		/* Load commands are not of a fixed size which is why we add the size */
		loadcmds = (struct load_command *)((uintptr_t)loadcmds + loadcmds->cmdsize);
	}
	
	if (!linkedit_hdr || !symtab_hdr || !symtab_hdr->symoff || !symtab_hdr->stroff)
	{
		/* Uh oh, no symbol table */
		return NULL;
	}

	linkedit_addr = dlbase + linkedit_hdr->vmaddr;
	symtab = (struct nlist *)(linkedit_addr + symtab_hdr->symoff - linkedit_hdr->fileoff);
	strtab = (const char *)(linkedit_addr + symtab_hdr->stroff - linkedit_hdr->fileoff);
	symbol_count = symtab_hdr->nsyms;
	
	/* Iterate symbol table starting from the position we were at last time */
	for (uint32_t i = libtable->last_pos; i < symbol_count; i++)
	{
		struct nlist &sym = symtab[i];
		/* Ignore the prepended underscore on all symbols, so +1 here */
		const char *sym_name = strtab + sym.n_un.n_strx + 1;
		Symbol *cur_sym;
		
		/* Skip symbols that are undefined */
		if (sym.n_sect == NO_SECT)
		{
			continue;
		}
		
		/* Caching symbols as we go along */
		cur_sym = table->InternSymbol(sym_name, strlen(sym_name), (void *)(dlbase + sym.n_value));
		if (strcmp(symbol, sym_name) == 0)
		{
			symbol_entry = cur_sym;
			libtable->last_pos = ++i;
			break;
		}
	}
	
	return symbol_entry ? symbol_entry->address : NULL;

#endif
	return NULL;
}
