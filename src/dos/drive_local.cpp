/*
 *  Copyright (C) 2002-2020  The DOSBox Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

// Uncomment to enable file-open diagnostic messages
// #define DEBUG 1

#include "drives.h"

#include <cerrno>
#include <climits>
#include <cstdio>
#include <limits>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#ifdef _MSC_VER
#include <sys/utime.h>
#else
#include <utime.h>
#endif

#include "dos_inc.h"
#include "dos_mscdex.h"
#include "support.h"
#include "cross.h"
#include "inout.h"

bool localDrive::FileCreate(DOS_File * * file,char * name,Bit16u /*attributes*/) {
//TODO Maybe care for attributes but not likely
	char newname[CROSS_LEN];
	safe_strcpy(newname, basedir);
	safe_strcat(newname, name);
	CROSS_FILENAME(newname);
	char* temp_name = dirCache.GetExpandName(newname); //Can only be used in till a new drive_cache action is preformed */
	/* Test if file exists (so we need to truncate it). don't add to dirCache then */
	bool existing_file = false;
	
	//--Added 2010-01-18 by Alun Bestor to allow Boxer to selectively deny write access to files
	if (!boxer_shouldAllowWriteAccessToPath((const char *)newname, this))
	{
		DOS_SetError(DOSERR_ACCESS_DENIED);
		return false;
	}
	//--End of modifications
	
	//-- Modified 2012-07-24 by Alun Bestor to allow Boxer to shadow local file access
	//FILE * test=fopen_wrap(temp_name,"rb+");
	FILE * test=boxer_openLocalFile(temp_name, this, "rb+");
	//--End of modifications
	if (test) {
		fclose(test);
		existing_file=true;

	}
	
	//-- Modified 2012-07-24 by Alun Bestor to allow Boxer to shadow local file access
	//FILE * hand=fopen_wrap(temp_name,"wb+");
	FILE * hand=boxer_openLocalFile(temp_name, this, "wb+");
	//--End of modifications
	if (!hand) {
		LOG_MSG("Warning: file creation failed: %s",newname);
		return false;
	}
   
	if (!existing_file) dirCache.AddEntry(newname, true);
	/* Make the 16 bit device information */
	*file = new localFile(name, hand, basedir);
	(*file)->flags=OPEN_READWRITE;
	
	//--Added 2010-08-21 by Alun Bestor to let Boxer monitor DOSBox's file operations
	boxer_didCreateLocalFile(temp_name, this);
	//--End of modifications

	return true;
}

bool localDrive::IsFirstEncounter(const std::string& filename) {
	const auto ret = write_protected_files.insert(filename);
	const bool was_inserted = ret.second;
	return was_inserted;
}

// Search the Files[] inventory for an open file matching the requested
// local drive and file name. Returns nullptr if not found.
DOS_File *FindOpenFile(const DOS_Drive *drive, const char *name)
{
	if (!drive || !name)
		return nullptr;

	uint8_t drive_num = DOS_DRIVES; // default out range
	// Look for a matching drive mount
	for (uint8_t i = 0; i < DOS_DRIVES; ++i) {
		if (Drives[i] && Drives[i] == drive) {
			drive_num = i;
			break;
		}
	}
	if (drive_num == DOS_DRIVES) // still out of range, no matching mount
		return nullptr;

	// Look for a matching open filename on the same mount
	for (auto *file : Files)
		if (file && file->IsOpen() && file->GetDrive() == drive_num && file->IsName(name))
			return file; // drive + file path is unique

	return nullptr;
}

bool localDrive::FileOpen(DOS_File **file, char *name, Bit32u flags)
{
	const char *type = nullptr;
	switch (flags&0xf) {
	case OPEN_READ:        type = "rb" ; break;
	case OPEN_WRITE:       type = "rb+"; break;
	case OPEN_READWRITE:   type = "rb+"; break;
	case OPEN_READ_NO_MOD: type = "rb" ; break; //No modification of dates. LORD4.07 uses this
	default:
		DOS_SetError(DOSERR_ACCESS_CODE_INVALID);
		return false;
	}
	char newname[CROSS_LEN];
	safe_strcpy(newname, basedir);
	safe_strcat(newname, name);
	CROSS_FILENAME(newname);
	dirCache.ExpandName(newname);

	//--Added 2010-01-18 by Alun Bestor to allow Boxer to selectively deny write access to files
	if (!strcmp(type, "rb+"))
	{
		if (!boxer_shouldAllowWriteAccessToPath((const char *)newname, this))
		{
			//Copy-pasted from cdromDrive::FileOpen
			if ((flags&3)==OPEN_READWRITE) {
				flags &= ~OPEN_READWRITE;
			} else {
				DOS_SetError(DOSERR_ACCESS_DENIED);
				return false;
			}
		}
	}
	//--End of modifications

	// If the file's already open then flush it before continuing
	// (Betrayal in Antara)
	auto open_file = dynamic_cast<localFile *>(FindOpenFile(this, name));
	if (open_file)
		open_file->Flush();

	//-- Modified 2012-07-24 by Alun Bestor to allow Boxer to shadow local file access
	//FILE* fhandle = fopen(newname, type);
	FILE* fhandle = boxer_openLocalFile(newname, this, type);
	//--End of modifications

#ifdef DEBUG
	std::string open_msg;
	std::string flags_str;
	switch (flags & 0xf) {
		case OPEN_READ:        flags_str = "R";  break;
		case OPEN_WRITE:       flags_str = "W";  break;
		case OPEN_READWRITE:   flags_str = "RW"; break;
		case OPEN_READ_NO_MOD: flags_str = "RN"; break;
		default:               flags_str = "--";
	}
#endif

	// If we couldn't open the file, then it's possible that
	// the file is simply write-protected and the flags requested
	// RW access.  So check if this is the case:
	if (!fhandle && flags & (OPEN_READWRITE | OPEN_WRITE)) {
		// If yes, check if the file can be opened with Read-only access:
		//-- Modified 2012-07-24 by Alun Bestor to allow Boxer to shadow local file access
		//fhandle = fopen_wrap(newname, "rb");
		fhandle=boxer_openLocalFile(newname, this, "rb");
		//--End of modifications
		if (fhandle) {

#ifdef DEBUG
			open_msg = "wanted writes but opened read-only";
#else
			// Inform the user that the file is being protected against modification.
			// If the DOS program /really/ needs to write to the file, it will
			// crash/exit and this will be one of the last messages on the screen,
			// so the user can decide to un-write-protect the file if they wish.
			// We only print one message per file to eliminate redundant messaging.
			if (IsFirstEncounter(newname)) {
				// For brevity and clarity to the user, we show just the
				// filename instead of the more cluttered absolute path.
				LOG_MSG("FILESYSTEM: protected from modification: %s",
				        get_basename(newname).c_str());
			}
#endif
		}

#ifdef DEBUG
		else {
			open_msg += "failed desired and with read-only";
		}
#endif
	}

#ifdef DEBUG
	else {
		open_msg = "succeeded with desired flags";
	}
	LOG_MSG("FILESYSTEM: flags=%2s, %-12s %s",
	        flags_str.c_str(),
	        get_basename(newname).c_str(),
	        open_msg.c_str());
#endif

	if (fhandle) {
		*file = new localFile(name, fhandle, basedir);
		(*file)->flags = flags;  // for the inheritance flag and maybe check for others.
	} else {
		// Otherwise we really can't open the file.
		DOS_SetError(DOSERR_INVALID_HANDLE);
	}
	return (fhandle != NULL);
}

FILE * localDrive::GetSystemFilePtr(char const * const name, char const * const type) {

	char newname[CROSS_LEN];
	safe_strcpy(newname, basedir);
	safe_strcat(newname, name);
	CROSS_FILENAME(newname);
	dirCache.ExpandName(newname);

    //-- Modified 2012-07-24 by Alun Bestor to allow Boxer to shadow local file access
	//return fopen_wrap(newname,type);
    return boxer_openLocalFile(newname, this, type);
    //--End of modifications
}

bool localDrive::GetSystemFilename(char *sysName, char const * const dosName) {

	strcpy(sysName, basedir);
	strcat(sysName, dosName);
	CROSS_FILENAME(sysName);
	dirCache.ExpandName(sysName);
	return true;
}

// Attempt to delete the file name from our local drive mount
bool localDrive::FileUnlink(char * name) {
	char newname[CROSS_LEN];
	safe_strcpy(newname, basedir);
	safe_strcat(newname, name);
	CROSS_FILENAME(newname);
	const char *fullname = dirCache.GetExpandName(newname);
	//--Added 2010-12-29 by Alun Bestor to let Boxer selectively prevent file operations
	if (!boxer_shouldAllowWriteAccessToPath((const char *)fullname, this))
	{
		DOS_SetError(DOSERR_ACCESS_DENIED);
		return false;
	}
	//--End of modifications

	// Can we remove the file without issue?
	//-- Modified 2012-07-24 by Alun Bestor to allow Boxer to shadow local file access
	//if (remove(fullname) == 0) {
	if (!boxer_removeLocalFile(fullname, this)) {
		dirCache.DeleteEntry(newname);
		//--Added 2010-08-21 by Alun Bestor to let Boxer monitor DOSBox's file operations
		boxer_didRemoveLocalFile(fullname, this);
		//--End of modifications
		return true;
	}

	// Otherwise maybe the file's opened within our mount ...
	DOS_File *open_file = FindOpenFile(this, name);
	if (open_file) {
		size_t max = DOS_FILES;
		// then close and remove references (as many times as needed),
		while (open_file->IsOpen() && --max) {
			open_file->Close();
			if (open_file->RemoveRef() <= 0) {
				break;
			}
		}
		// and try removing it again.
		if (boxer_removeLocalFile(fullname, this) == 0) {
			dirCache.DeleteEntry(newname);
			
			//--Added 2010-08-21 by Alun Bestor to let Boxer monitor DOSBox's file operations
			boxer_didRemoveLocalFile(fullname, this);
			//--End of modifications
			return true;
		}
	}
	DEBUG_LOG_MSG("FS: Unable to remove file %s", fullname);
	return false;
}

bool localDrive::FindFirst(char * _dir,DOS_DTA & dta,bool fcb_findfirst) {
	char tempDir[CROSS_LEN];
	safe_strcpy(tempDir, basedir);
	safe_strcat(tempDir, _dir);
	CROSS_FILENAME(tempDir);

	if (allocation.mediaid==0xF0) {
		EmptyCache(); //rescan floppie-content on each findfirst
	}
    
	char end[2]={CROSS_FILESPLIT,0};
	if (tempDir[strlen(tempDir) - 1] != CROSS_FILESPLIT)
		safe_strcat(tempDir, end);

	Bit16u id;
	if (!dirCache.FindFirst(tempDir,id)) {
		DOS_SetError(DOSERR_PATH_NOT_FOUND);
		return false;
	}
	safe_strcpy(srchInfo[id].srch_dir, tempDir);
	dta.SetDirID(id);
	
	Bit8u sAttr;
	dta.GetSearchParams(sAttr,tempDir);

	if (this->isRemote() && this->isRemovable()) {
		// cdroms behave a bit different than regular drives
		if (sAttr == DOS_ATTR_VOLUME) {
			dta.SetResult(dirCache.GetLabel(),0,0,0,DOS_ATTR_VOLUME);
			return true;
		}
	} else {
		if (sAttr == DOS_ATTR_VOLUME) {
			if (is_empty(dirCache.GetLabel())) {
//				LOG(LOG_DOSMISC,LOG_ERROR)("DRIVELABEL REQUESTED: none present, returned  NOLABEL");
//				dta.SetResult("NO_LABEL",0,0,0,DOS_ATTR_VOLUME);
//				return true;
				DOS_SetError(DOSERR_NO_MORE_FILES);
				return false;
			}
			dta.SetResult(dirCache.GetLabel(),0,0,0,DOS_ATTR_VOLUME);
			return true;
		} else if ((sAttr & DOS_ATTR_VOLUME)  && (*_dir == 0) && !fcb_findfirst) { 
		//should check for a valid leading directory instead of 0
		//exists==true if the volume label matches the searchmask and the path is valid
			if (WildFileCmp(dirCache.GetLabel(),tempDir)) {
				dta.SetResult(dirCache.GetLabel(),0,0,0,DOS_ATTR_VOLUME);
				return true;
			}
		}
	}
	return FindNext(dta);
}

bool localDrive::FindNext(DOS_DTA & dta) {

	char * dir_ent;
	struct stat stat_block;
	char full_name[CROSS_LEN];
	char dir_entcopy[CROSS_LEN];

	Bit8u srch_attr;char srch_pattern[DOS_NAMELENGTH_ASCII];
	Bit8u find_attr;

	dta.GetSearchParams(srch_attr,srch_pattern);
	Bit16u id = dta.GetDirID();

again:
	if (!dirCache.FindNext(id,dir_ent)) {
		DOS_SetError(DOSERR_NO_MORE_FILES);
		return false;
	}
	if (!WildFileCmp(dir_ent,srch_pattern)) goto again;

	safe_strcpy(full_name, srchInfo[id].srch_dir);
	safe_strcat(full_name, dir_ent);

	//GetExpandName might indirectly destroy dir_ent (by caching in a new directory 
	//and due to its design dir_ent might be lost.)
	//Copying dir_ent first
	safe_strcpy(dir_entcopy, dir_ent);
	//Modified 2012-07-24 by Alun Bestor to wrap local file operations
	//if (stat(dirCache.GetExpandName(full_name),&stat_block)!=0) {
	if (!boxer_getLocalPathStats(dirCache.GetExpandName(full_name), this, &stat_block)) {
		//--End of modifications
		goto again;//No symlinks and such
	}	

	if (stat_block.st_mode & S_IFDIR) find_attr=DOS_ATTR_DIRECTORY;
	else find_attr=DOS_ATTR_ARCHIVE;
 	if (~srch_attr & find_attr & (DOS_ATTR_DIRECTORY | DOS_ATTR_HIDDEN | DOS_ATTR_SYSTEM)) goto again;
	
	/*file is okay, setup everything to be copied in DTA Block */
	char find_name[DOS_NAMELENGTH_ASCII] = "";
	uint16_t find_date;
	uint16_t find_time;
	uint32_t find_size;

	if (strlen(dir_entcopy)<DOS_NAMELENGTH_ASCII) {
		safe_strcpy(find_name, dir_entcopy);
		upcase(find_name);
	} 

	find_size=(Bit32u) stat_block.st_size;
	struct tm *time;
	if ((time=localtime(&stat_block.st_mtime))!=0) {
		find_date=DOS_PackDate((Bit16u)(time->tm_year+1900),(Bit16u)(time->tm_mon+1),(Bit16u)time->tm_mday);
		find_time=DOS_PackTime((Bit16u)time->tm_hour,(Bit16u)time->tm_min,(Bit16u)time->tm_sec);
	} else {
		find_time=6; 
		find_date=4;
	}
	dta.SetResult(find_name,find_size,find_date,find_time,find_attr);
	return true;
}

bool localDrive::GetFileAttr(char * name,Bit16u * attr) {
	char newname[CROSS_LEN];
	safe_strcpy(newname, basedir);
	safe_strcat(newname, name);
	CROSS_FILENAME(newname);
	dirCache.ExpandName(newname);

	struct stat status;
	
    //Modified 2012-07-24 by Alun Bestor to wrap local file operations
	//if (stat(newname,&status)==0) {
    if (boxer_getLocalPathStats(newname, this, &status)) {
    //--End of modifications
		*attr=DOS_ATTR_ARCHIVE;
		if (status.st_mode & S_IFDIR) *attr|=DOS_ATTR_DIRECTORY;
		return true;
	}
	*attr=0;
	return false; 
}

bool localDrive::MakeDir(char * dir) {
	char newdir[CROSS_LEN];
	safe_strcpy(newdir, basedir);
	safe_strcat(newdir, dir);
	CROSS_FILENAME(newdir);
	//--Modified 2010-12-29 by Alun Bestor to allow Boxer to selectively prevent file operations,
	//and to prevent DOSBox from creating folders with the wrong file permissions.
	/*
#if defined (WIN32)						// MS Visual C++
	int temp=mkdir(dirCache.GetExpandName(newdir));
#else
	int temp=mkdir(dirCache.GetExpandName(newdir),0775);
#endif
 	 */
	if (!boxer_shouldAllowWriteAccessToPath(dirCache.GetExpandName(newdir), this))
	{
		DOS_SetError(DOSERR_ACCESS_DENIED);
		return false;
	}
	
	//if (temp==0) dirCache.CacheOut(newdir,true);
	
	//return (temp==0);// || ((temp!=0) && (errno==EEXIST));
	
	bool created = boxer_createLocalDir(dirCache.GetExpandName(newdir), this);
	if (created) dirCache.CacheOut(newdir,true);
	return created;
	//--End of modifications
}

bool localDrive::RemoveDir(char * dir) {
	char newdir[CROSS_LEN];
	safe_strcpy(newdir, basedir);
	safe_strcat(newdir, dir);
	CROSS_FILENAME(newdir);
	int temp=rmdir(dirCache.GetExpandName(newdir));
	if (temp==0) dirCache.DeleteEntry(newdir,true);
	return (temp==0);
}

bool localDrive::TestDir(char * dir) {
	char newdir[CROSS_LEN];
	safe_strcpy(newdir, basedir);
	safe_strcat(newdir, dir);
	CROSS_FILENAME(newdir);
	dirCache.ExpandName(newdir);
	
	//--Modified 2012-04-27 by Alun Bestor to wrap local file operations
    /*
	// Skip directory test, if "\"
	size_t len = strlen(newdir);
	if (len && (newdir[len-1]!='\\')) {
		// It has to be a directory !
		struct stat test;
		if (stat(newdir,&test))			return false;
		if ((test.st_mode & S_IFDIR)==0)	return false;
	};
	int temp=access(newdir,F_OK);
	return (temp==0);
     */
    return boxer_localDirectoryExists(newdir, this);
	//--End of modifications
}

bool localDrive::Rename(char * oldname,char * newname) {
	char newold[CROSS_LEN];
	safe_strcpy(newold, basedir);
	safe_strcat(newold, oldname);
	CROSS_FILENAME(newold);
	dirCache.ExpandName(newold);
	
	char newnew[CROSS_LEN];
	safe_strcpy(newnew, basedir);
	safe_strcat(newnew, newname);
	CROSS_FILENAME(newnew);
	int temp=rename(newold,dirCache.GetExpandName(newnew));
	if (temp==0) dirCache.CacheOut(newnew);
	return (temp==0);

}

bool localDrive::AllocationInfo(Bit16u * _bytes_sector,Bit8u * _sectors_cluster,Bit16u * _total_clusters,Bit16u * _free_clusters) {
	*_bytes_sector=allocation.bytes_sector;
	*_sectors_cluster=allocation.sectors_cluster;
	*_total_clusters=allocation.total_clusters;
	*_free_clusters=allocation.free_clusters;
	return true;
}

bool localDrive::FileExists(const char* name) {
	char newname[CROSS_LEN];
	safe_strcpy(newname, basedir);
	safe_strcat(newname, name);
	CROSS_FILENAME(newname);
	dirCache.ExpandName(newname);
	//--Modified 2012-04-27 by Alun Bestor to wrap local file operations
	//struct stat temp_stat;
	//if (stat(newname,&temp_stat)!=0) return false;
	//if (temp_stat.st_mode & S_IFDIR) return false;
	//return true;
	return boxer_localFileExists(newname, this);
	//--End of modifications
}

bool localDrive::FileStat(const char* name, FileStat_Block * const stat_block) {
	char newname[CROSS_LEN];
	safe_strcpy(newname, basedir);
	safe_strcat(newname, name);
	CROSS_FILENAME(newname);
	dirCache.ExpandName(newname);
	struct stat temp_stat;
	
	//--Modified 2012-04-27 by Alun Bestor to wrap local file operations
	//if(stat(newname,&temp_stat)!=0) return false;
	if (!boxer_getLocalPathStats(newname, this, &temp_stat)) return false;
	//--End of modifications
	
	/* Convert the stat to a FileStat */
	struct tm *time;
	if ((time=localtime(&temp_stat.st_mtime))!=0) {
		stat_block->time=DOS_PackTime((Bit16u)time->tm_hour,(Bit16u)time->tm_min,(Bit16u)time->tm_sec);
		stat_block->date=DOS_PackDate((Bit16u)(time->tm_year+1900),(Bit16u)(time->tm_mon+1),(Bit16u)time->tm_mday);
	} else {

	}
	stat_block->size=(Bit32u)temp_stat.st_size;
	return true;
}


Bit8u localDrive::GetMediaByte(void) {
	return allocation.mediaid;
}

bool localDrive::isRemote(void) {
	return false;
}

bool localDrive::isRemovable(void) {
	return false;
}

Bits localDrive::UnMount(void) { 
	delete this;
	return 0; 
}

localDrive::localDrive(const char * startdir,
                       Bit16u _bytes_sector,
                       Bit8u _sectors_cluster,
                       Bit16u _total_clusters,
                       Bit16u _free_clusters,
                       Bit8u _mediaid)
	: write_protected_files{},
	  allocation{_bytes_sector,
	             _sectors_cluster,
	             _total_clusters,
	             _free_clusters,
	             _mediaid}
{
	safe_strcpy(basedir, startdir);
	sprintf(info,"local directory %s",startdir);
	//--Added 2009-10-25 by Alun Bestor to allow Boxer to track the system path for DOSBox drives
	safe_strcpy(systempath, startdir);
	//--End of modifications
	dirCache.SetBaseDir(basedir);
}

// Updates the internal file's current position
bool localFile::ftell_and_check()
{
	if (!fhandle)
		return false;

	stream_pos = ftell(fhandle);
	if (stream_pos >= 0)
		return true;

	DEBUG_LOG_MSG("FS: Failed obtaining position in file %s", name.c_str());
	return false;
}

// Seeks the internal file to the specified position relative to whence
bool localFile::fseek_to_and_check(long pos, int whence)
{
	if (!fhandle)
		return false;

	if (fseek(fhandle, pos, whence) == 0) {
		stream_pos = pos;
		return true;
	}
	DEBUG_LOG_MSG("FS: Failed seeking to byte %ld in file %s", stream_pos, name.c_str());
	return false;
}

// Seeks the internal file to the internal position relative to whence
void localFile::fseek_and_check(int whence)
{
	static_cast<void>(fseek_to_and_check(stream_pos, whence));
}

//TODO Maybe use fflush, but that seemed to fuck up in visual c
bool localFile::Read(uint8_t *data, uint16_t *size)
{
	// check if the file is opened in write-only mode
	if ((this->flags & 0xf) == OPEN_WRITE) {
		DOS_SetError(DOSERR_ACCESS_DENIED);
		return false;
	}
    
    //--Added 2011-11-03 by Alun Bestor to avoid errors on files
    //whose backing media has disappeared
    if (!fhandle)
    {
        *size = 0;
        //IMPLEMENTATION NOTE: you might think we ought to return false here,
        //but no! We return true to be consistent with DOSBox's behaviour,
        //which appears to be the behaviour expected by DOS.
        return true;
    }
    //--End of modifications
    
	// Seek if we last wrote
	if (last_action == WRITE)
		if (ftell_and_check())
			fseek_and_check(SEEK_SET);

	last_action = READ;
	const auto requested = *size;
	const auto actual = static_cast<uint16_t>(fread(data, 1, requested, fhandle));
	*size = actual; // always save the actual

	// if (actual != requested)
	//	DEBUG_LOG_MSG("FS: Only read %u of %u requested bytes from file %s",
	//	              actual, requested, name.c_str());

	/* Fake harddrive motion. Inspector Gadget with soundblaster compatible */
	/* Same for Igor */
	/* hardrive motion => unmask irq 2. Only do it when it's masked as
	 * unmasking is realitively heavy to emulate */
	Bit8u mask = IO_Read(0x21);
	if (mask & 0x4)
		IO_Write(0x21, mask & 0xfb);
	return true;
}

bool localFile::Write(uint8_t *data, uint16_t *size)
{
	Bit32u lastflags = this->flags & 0xf;
	if (lastflags == OPEN_READ || lastflags == OPEN_READ_NO_MOD) {	// check if file opened in read-only mode
		DOS_SetError(DOSERR_ACCESS_DENIED);
		return false;
	}
    
    //--Added 2011-11-03 by Alun Bestor to avoid errors on files
    //whose backing media has disappeared
    if (!fhandle)
    {
        *size = 0;
        //IMPLEMENTATION NOTE: you might think we ought to return false here,
        //but no! We return true to be consistent with DOSBox's behaviour,
        //which appears to be the behaviour expected by DOS.
        return true;
    }
    //--End of modifications
    
	// Seek if we last read
	if (last_action == READ)
		if (ftell_and_check())
			fseek_and_check(SEEK_SET);

	last_action = WRITE;

	// Truncate the file
	if (*size == 0) {
		const auto file = cross_fileno(fhandle);
		if (file == -1) {
			DEBUG_LOG_MSG("FS: Could not resolve file number for %s", name.c_str());
			return false;
		}
		if (!ftell_and_check()) {
			return false;
		}
		if (ftruncate(file, stream_pos) != 0) {
			DEBUG_LOG_MSG("FS: Failed truncating file %s", name.c_str());
			return false;
		}
		// Truncation succeeded if we made it here
		return true;
	}

	// Otherwise we have some data to write
	const auto requested = *size;
	const auto actual = static_cast<uint16_t>(fwrite(data, 1, requested, fhandle));
	if (actual != requested)
		DEBUG_LOG_MSG("FS: Only wrote %u of %u requested bytes to file %s",
		              actual, requested, name.c_str());
	*size = actual; // always save the actual
	return true;    // always return true, even if partially written
}

bool localFile::Seek(uint32_t *pos_addr, uint32_t type)
{
	int seektype;
	switch (type) {
	case DOS_SEEK_SET:seektype=SEEK_SET;break;
	case DOS_SEEK_CUR:seektype=SEEK_CUR;break;
	case DOS_SEEK_END:seektype=SEEK_END;break;
	default:
	//TODO Give some doserrorcode;
		return false;//ERROR
	}
	
	//--Added 2011-11-03 by Alun Bestor to avoid errors on files
	//whose backing media has disappeared
	if (!fhandle)
	{
		*pos_addr = 0;
		//IMPLEMENTATION NOTE: you might think we ought to return false here,
		//but no! We return true to be consistent with DOSBox's behaviour,
		//which appears to be the behaviour expected by DOS.
		return true;
	}
	//--End of modifications

	// The inbound position is actually an int32_t being passed through a
	// uint32_t* pointer (pos_addr), so reinterpret the underlying memory as
	// such to prevent rollover into the unsigned range.
	const auto pos = *reinterpret_cast<int32_t *>(pos_addr);
	if (!fseek_to_and_check(pos, seektype)) {
		// Failed to seek, but try again this time seeking to
		// the end of file, which satisfies Black Thorne.
		stream_pos = 0;
		fseek_and_check(SEEK_END);
	}
#if 0
	fpos_t temppos;
	fgetpos(fhandle,&temppos);
	Bit32u * fake_pos=(Bit32u*)&temppos;
	*pos_addr = *fake_pos;
#endif
	static_cast<void>(ftell_and_check());

	// The inbound position is actually an int32_t being passed through a
	// uint32_t* pointer (pos_addr), so before we save the seeked position
	// back into it we first ensure the current long stream_pos (which is a
	// signed 64-bit on some platforms + OSes) can fit within the int32_t
	// range before assigning it.
	assert(stream_pos >= std::numeric_limits<int32_t>::min() &&
	       stream_pos <= std::numeric_limits<int32_t>::max());
	*reinterpret_cast<int32_t *>(pos_addr) = static_cast<int32_t>(stream_pos);

	last_action = NONE;
	return true;
}

bool localFile::Close() {
	// only close if one reference left
	if (refCtr==1) {
		if (fhandle) fclose(fhandle);
		fhandle = 0;
		open = false;
	};

	if (newtime) {
		// backport from DOS_PackDate() and DOS_PackTime()
		tm tim = {0};
		tim.tm_sec = (time & 0x1f) * 2;
		tim.tm_min = (time >> 5) & 0x3f;
		tim.tm_hour = (time >> 11) & 0x1f;
		tim.tm_mday = date & 0x1f;
		tim.tm_mon = ((date >> 5) & 0x0f) - 1;
		tim.tm_year = (date >> 9) + 1980 - 1900;
		//  have the C run-time library code compute whether standard
		//  time or daylight saving time is in effect.
		tim.tm_isdst = -1;
		// serialize time
		mktime(&tim);

		utimbuf ftim;
		ftim.actime = ftim.modtime = mktime(&tim);

		char fullname[CROSS_LEN];
		snprintf(fullname, sizeof(fullname), "%s%s", basedir, name.c_str());
		CROSS_FILENAME(fullname);

		// FIXME: utime is deprecated, need a modern cross-platform
		// implementation.
		if (utime(fullname, &ftim)) {
			return false;
		}
	}

	return true;
}

Bit16u localFile::GetInformation(void) {
	return read_only_medium?0x40:0;
}

localFile::localFile(const char *_name, FILE *handle, const char *_basedir)
        : fhandle(handle),
          basedir(_basedir),
          read_only_medium(false),
          last_action(NONE)
{
	open=true;
	UpdateDateTimeFromHost();

	attr=DOS_ATTR_ARCHIVE;

	SetName(_name);
}

bool localFile::UpdateDateTimeFromHost()
{
	if (!open)
		return false;

	//--Added 2011-11-03 by Alun Bestor to avoid errors on closed files
	if (!fhandle) return false;
	//--End of modifications
	// Legal defaults if we're unable to populate them
	time = 1;
	date = 1;

	const auto file = cross_fileno(fhandle);
	if (file == -1)
		return true; // use defaults

	struct stat temp_stat;
	if (fstat(file, &temp_stat) == -1)
		return true; // use defaults

	const tm *ltime = localtime(&temp_stat.st_mtime);
	if (!ltime)
		return true; // use defaults

	time = DOS_PackTime(static_cast<uint16_t>(ltime->tm_hour),
	                    static_cast<uint16_t>(ltime->tm_min),
	                    static_cast<uint16_t>(ltime->tm_sec));

	date = DOS_PackDate(static_cast<uint16_t>(ltime->tm_year + 1900),
	                    static_cast<uint16_t>(ltime->tm_mon + 1),
	                    static_cast<uint16_t>(ltime->tm_mday));
	return true;
}

//--Added 2011-11-03 by Alun Bestor to let Boxer inform open file handles
//that their physical backing media will be removed.
void localFile::willBecomeUnavailable()
{
    //If the real file is about to become unavailable, then close
    //our file handle but leave the DOS file flagged as 'open'.
    if (fhandle)
    {
        fclose(fhandle);
        fhandle = 0;
    }
}
//--End of modification


void localFile::Flush()
{
	if (last_action != WRITE)
		return;

	if (ftell_and_check())
		fseek_and_check(SEEK_SET);

	// Always reset the state even if the file is broken
	last_action = NONE;
}

// ********************************************
// CDROM DRIVE
// ********************************************

cdromDrive::cdromDrive(const char _driveLetter,
                       const char * startdir,
                       Bit16u _bytes_sector,
                       Bit8u _sectors_cluster,
                       Bit16u _total_clusters,
                       Bit16u _free_clusters,
                       Bit8u _mediaid,
                       int& error)
	: localDrive(startdir,
	             _bytes_sector,
	             _sectors_cluster,
	             _total_clusters,
	             _free_clusters,
	             _mediaid),
	  subUnit(0),
	  driveLetter(_driveLetter)
{
	// Init mscdex
	error = MSCDEX_AddDrive(driveLetter,startdir,subUnit);
	safe_strcpy(info, "CDRom ");
	safe_strcat(info, startdir);
	// Get Volume Label
	char name[32];
	if (MSCDEX_GetVolumeName(subUnit,name)) dirCache.SetLabel(name,true,true);
}

bool cdromDrive::FileOpen(DOS_File * * file,char * name,Bit32u flags) {
	if ((flags&0xf)==OPEN_READWRITE) {
		flags &= ~static_cast<unsigned>(OPEN_READWRITE);
	} else if ((flags&0xf)==OPEN_WRITE) {
		DOS_SetError(DOSERR_ACCESS_DENIED);
		return false;
	}
	bool success = localDrive::FileOpen(file, name, flags);
	if (success)
		(*file)->SetFlagReadOnlyMedium();
	return success;
}

bool cdromDrive::FileCreate(DOS_File * * /*file*/,char * /*name*/,Bit16u /*attributes*/) {
	DOS_SetError(DOSERR_ACCESS_DENIED);
	return false;
}

bool cdromDrive::FileUnlink(char * /*name*/) {
	DOS_SetError(DOSERR_ACCESS_DENIED);
	return false;
}

bool cdromDrive::RemoveDir(char * /*dir*/) {
	DOS_SetError(DOSERR_ACCESS_DENIED);
	return false;
}

bool cdromDrive::MakeDir(char * /*dir*/) {
	DOS_SetError(DOSERR_ACCESS_DENIED);
	return false;
}

bool cdromDrive::Rename(char * /*oldname*/,char * /*newname*/) {
	DOS_SetError(DOSERR_ACCESS_DENIED);
	return false;
}

bool cdromDrive::GetFileAttr(char * name,Bit16u * attr) {
	bool result = localDrive::GetFileAttr(name,attr);
	if (result) *attr |= DOS_ATTR_READ_ONLY;
	return result;
}

bool cdromDrive::FindFirst(char * _dir,DOS_DTA & dta,bool /*fcb_findfirst*/) {
	// If media has changed, reInit drivecache.
	if (MSCDEX_HasMediaChanged(subUnit)) {
		dirCache.EmptyCache();
		// Get Volume Label
		char name[32];
		if (MSCDEX_GetVolumeName(subUnit,name)) dirCache.SetLabel(name,true,true);
	}
	return localDrive::FindFirst(_dir,dta);
}

void cdromDrive::SetDir(const char* path) {
	// If media has changed, reInit drivecache.
	if (MSCDEX_HasMediaChanged(subUnit)) {
		dirCache.EmptyCache();
		// Get Volume Label
		char name[32];
		if (MSCDEX_GetVolumeName(subUnit,name)) dirCache.SetLabel(name,true,true);
	}
	localDrive::SetDir(path);
}

bool cdromDrive::isRemote(void) {
	return true;
}

bool cdromDrive::isRemovable(void) {
	return true;
}

Bits cdromDrive::UnMount(void) {
	if (MSCDEX_RemoveDrive(driveLetter)) {
		delete this;
		return 0;
	}
	return 2;
}
