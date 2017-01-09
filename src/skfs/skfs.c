// skfs.c

#ifndef FUSE_USE_VERSION
#define FUSE_USE_VERSION 26
#endif

/////////////
// includes

////#include <config.h>//use only in conjunction with configure/makefile

#ifdef linux
/* For pread()/pwrite() */
#define _XOPEN_SOURCE 500
#endif

#include <argp.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif
#include <valgrind/valgrind.h>

#include <string>
#include <set>

#include "AttrReader.h"
#include "AttrWriter.h"
#include "FileBlockReader.h"
#include "FileBlockWriter.h"
#include "FileIDToPathMap.h"
//#include "NSKeySplit.h"
#include "OpenDirTable.h"
#include "PartialBlockReader.h"
#include "PathGroup.h"
#include "ReconciliationSet.h"
#include "ResponseTimeStats.h"
#include "skfs.h"
#include "SRFSConstants.h"
#include "SRFSDHT.h"
#include "Util.h"
#include "WritableFile.h"
#include "WritableFileTable.h"

#include <signal.h>
#include <sys/inotify.h>

////////////
// defines

// inotify-related defs
#define EVENT_SIZE  ( sizeof (struct inotify_event) )
#define INOTIFY_BUF_LEN     ( 1024 * ( EVENT_SIZE + 16 ) )

#define SO_VERBOSE 'v'
#define SO_HOST 'h'
#define SO_ZK_LOC 'z'
#define SO_GC_NAME 'G'
#define SO_MOUNT 'm'
#define SO_NFS_MAPPING 'n'
#define SO_PERMANENT_SUFFIXES 's'
#define SO_NO_ERROR_CACHE_PATHS 'e'
#define SO_NO_LINK_CACHE_PATHS 'o'
#define SO_FS_NATIVE_ONLY_FILE 'f'
#define SO_SNAPSHOT_ONLY_PATHS 'a'
#define SO_TASK_OUTPUT_PATHS 'g'
#define SO_COMPRESSED_PATHS 'C'
#define SO_NO_FBW_PATHS 'w'
#define SO_FBW_RELIABLE_QUEUE 'q'
#define SO_COMPRESSION 'c'
#define SO_CHECKSUM 'S'
#define SO_TRANSIENT_CACHE_SIZE_KB 'T'
#define SO_CACHE_CONCURRENCY 'y'
#define SO_LOG_LEVEL 'l'
#define SO_JVM_OPTIONS 'J'
#define SO_BIGWRITES 'B'
#define SO_ENTRY_TIMEOUT_SECS 'E'
#define SO_ATTR_TIMEOUT_SECS 'A'
#define SO_NEGATIVE_TIMEOUT_SECS 'N'

#define LO_VERBOSE "verbose"
#define LO_HOST "host"
//#define LO_PORT "port"
#define LO_ZK_LOC "zkLoc"
//#define LO_DHT_NAME "dhtName"
#define LO_GC_NAME "gcname"
#define LO_MOUNT "mount" 
#define LO_NFS_MAPPING "nfsMapping" 
#define LO_PERMANENT_SUFFIXES "permanentSuffixes" 
#define LO_NO_ERROR_CACHE_PATHS "noErrorCachePaths"
#define LO_NO_LINK_CACHE_PATHS "noLinkCachePaths"
#define LO_FS_NATIVE_ONLY_FILE "fsNativeOnlyFile"
#define LO_SNAPSHOT_ONLY_PATHS "snapshotOnlyPaths"
#define LO_TASK_OUTPUT_PATHS "taskOutputPaths"
#define LO_COMPRESSED_PATHS "compressedPaths"
#define LO_NO_FBW_PATHS "noFBWPaths"
#define LO_FBW_RELIABLE_QUEUE "fbwReliableQueue"
#define LO_COMPRESSION "compression"
#define LO_CHECKSUM "checksum"
#define LO_TRANSIENT_CACHE_SIZE_KB "transientCacheSizeKB"
#define LO_CACHE_CONCURRENCY "cacheConcurrency"
#define LO_LOG_LEVEL "logLevel"
#define LO_JVM_OPTIONS "jvmOptions"
#define LO_BIGWRITES "bigwrites"
#define LO_ENTRY_TIMEOUT_SECS "entryTimeoutSecs"
#define LO_ATTR_TIMEOUT_SECS "attrTimeoutSecs"
#define LO_NEGATIVE_TIMEOUT_SECS "negativeTimeoutSecs"
#
#define OPEN_MODE_FLAG_MASK 0x3

#define ODT_NAME "OpenDirTable"

#define SKFS_FUSE_OPTION_STRING_LENGTH	128
#define _SKFS_RENAME_RETRY_DELAY_MS    5
#define _SKFS_RENAME_MAX_ATTEMPTS   20
#define _SKFS_TIMEOUT_NEVER 0x0fffffff

//////////
// types

///////////////////////
// private prototypes

static error_t parse_opt(int key, char *arg, struct argp_state *state);
static void *stats_thread(void *);
static void * nativefile_watcher_thread(void * unused);
static void initPaths();
static void initReaders();
static void initDirs();
static void destroyReaders();
static void destroyPaths();
static int _skfs_unlink(const char *path, int deleteBlocks = TRUE);
static void add_fuse_option(char *option);
static int ensure_not_writable(char *path1, char *path2 = NULL);
static int modify_file_attr(const char *path, char *fnName, mode_t *mode, uid_t *uid, gid_t *gid);
static void skfs_destroy(void* private_data);
////////////
// globals

static struct argp_option options[] = {
       {LO_VERBOSE,  SO_VERBOSE, LO_VERBOSE,      0,  "Mode/test to run", 0 },
       {LO_HOST,  SO_HOST, LO_HOST,      0,  "Host name", 0 },
//       {LO_PORT,  SO_PORT, LO_PORT,      0,  "Port" , 0},
       {LO_ZK_LOC,  SO_ZK_LOC, LO_ZK_LOC,      0,  "Zookeeper Locations" , 0},
//       {LO_DHT_NAME,  SO_DHT_NAME, LO_DHT_NAME,      0,  "DHT Name", 0 },
       {LO_GC_NAME,  SO_GC_NAME, LO_GC_NAME,      0,  "GridConfig Name", 0 },
       {LO_MOUNT,  SO_MOUNT, LO_MOUNT,      0,  "Mount", 0 },
       {LO_NFS_MAPPING,  SO_NFS_MAPPING, LO_NFS_MAPPING,      0,  "NFS mapping", 0 },
       {LO_PERMANENT_SUFFIXES,   SO_PERMANENT_SUFFIXES,  LO_PERMANENT_SUFFIXES,       0,  "Permanent suffixes", 0 },
       {LO_NO_ERROR_CACHE_PATHS, SO_NO_ERROR_CACHE_PATHS, LO_NO_ERROR_CACHE_PATHS,    0,  "No error cache paths", 0 },
       {LO_NO_LINK_CACHE_PATHS,  SO_NO_LINK_CACHE_PATHS, LO_NO_LINK_CACHE_PATHS,      0,  "No link cache paths", 0 },
       {LO_FS_NATIVE_ONLY_FILE,  SO_FS_NATIVE_ONLY_FILE, LO_FS_NATIVE_ONLY_FILE,      0,  "fs native only file", 0 },
       {LO_SNAPSHOT_ONLY_PATHS,  SO_SNAPSHOT_ONLY_PATHS, LO_SNAPSHOT_ONLY_PATHS,      0,  "Snapshot only paths", 0 },
       {LO_COMPRESSED_PATHS,     SO_COMPRESSED_PATHS,    LO_COMPRESSED_PATHS,    OPTION_ARG_OPTIONAL,  "Compressed paths", 0 },
       {LO_NO_FBW_PATHS,         SO_NO_FBW_PATHS,        LO_NO_FBW_PATHS,        OPTION_ARG_OPTIONAL,  "no fbw paths", 0 },
       {LO_FBW_RELIABLE_QUEUE,   SO_FBW_RELIABLE_QUEUE,  LO_FBW_RELIABLE_QUEUE,  OPTION_ARG_OPTIONAL,  "fbwReliableQueue", 0 },
       {LO_TASK_OUTPUT_PATHS,    SO_TASK_OUTPUT_PATHS,   LO_TASK_OUTPUT_PATHS,        0,  "Task output path mappings", 0 },
       {LO_COMPRESSION,          SO_COMPRESSION,         LO_COMPRESSION,              0,  "Compression", 0 },
       {LO_CHECKSUM,             SO_CHECKSUM,            LO_CHECKSUM,                 0,  "Checksum", 0 },
       {LO_TRANSIENT_CACHE_SIZE_KB,  SO_TRANSIENT_CACHE_SIZE_KB, LO_TRANSIENT_CACHE_SIZE_KB,      0,  "transientCacheSizeKB", 0 },
       {LO_CACHE_CONCURRENCY,   SO_CACHE_CONCURRENCY, LO_CACHE_CONCURRENCY,           0,  "cacheConcurrency", 0 },
	   {LO_LOG_LEVEL,           SO_LOG_LEVEL,            LO_LOG_LEVEL,                0, "logLevel", 0},
	   {LO_JVM_OPTIONS,         SO_JVM_OPTIONS,          LO_JVM_OPTIONS,              0, "comma-separated jvmOptions", 0},
       {LO_BIGWRITES,           SO_BIGWRITES,            LO_BIGWRITES,           OPTION_ARG_OPTIONAL,  "enable big_writes", 0 },
       {LO_ENTRY_TIMEOUT_SECS, SO_ENTRY_TIMEOUT_SECS,    LO_ENTRY_TIMEOUT_SECS,  0,  "entry timeout seconds", 0 },
       {LO_ATTR_TIMEOUT_SECS, SO_ATTR_TIMEOUT_SECS,    LO_ATTR_TIMEOUT_SECS,  0,  "attr timeout seconds", 0 },       
       {LO_NEGATIVE_TIMEOUT_SECS, SO_NEGATIVE_TIMEOUT_SECS,    LO_NEGATIVE_TIMEOUT_SECS,  0,  "negative timeout seconds", 0 },       
       { 0, 0, 0, 0, 0, 0 }
};

static char doc[] = "skfs";
static char args_doc[] = "";
static struct argp argp = {options, parse_opt, args_doc, doc, 0, 0, 0 };

static CmdArgs _args;
CmdArgs *args = &_args;

static struct fuse_args fuseArgs = FUSE_ARGS_INIT(0, NULL);

static PartialBlockReader *pbr;
static FileBlockReader *fbr;
static AttrReader	*ar;
//static DirReader	*dr;
static OpenDirTable	*odt;
static FileIDToPathMap	*f2p;
static WritableFileTable	*wft;
static AttrWriter	*aw;
static AttrWriter	*awSKFS;
static FileBlockWriter	*fbwCompress;
static FileBlockWriter	*fbwRaw;
static FileBlockWriter	*fbwSKFS;
static SRFSDHT	*sd;
static ResponseTimeStats	*rtsAR_DHT;
static ResponseTimeStats	*rtsAR_NFS;
static ResponseTimeStats	*rtsFBR_DHT;
static ResponseTimeStats	*rtsFBR_NFS;
static ResponseTimeStats	*rtsODT;
static PathGroup volatile	*fsNativeOnlyPaths;
static char			*fsNativeOnlyFile;

static int			statsIntervalSeconds = 20;
static int			statsDetailIntervalSeconds = 300;
static pthread_t	statsThread;
static pthread_t	nativeFileWatcherThread;

static char	logFileName[SRFS_MAX_PATH_LENGTH];
static char	logFileNameDht[SRFS_MAX_PATH_LENGTH];
static FILE * dhtCliLog = NULL; 

static char	fuseEntryOption[SKFS_FUSE_OPTION_STRING_LENGTH];
static char	fuseAttrOption[SKFS_FUSE_OPTION_STRING_LENGTH];
static char	fuseACAttrOption[SKFS_FUSE_OPTION_STRING_LENGTH];
static char	fuseNegativeOption[SKFS_FUSE_OPTION_STRING_LENGTH];

SKSessionOptions	*sessOption;

static int  destroyCalled;
static pthread_spinlock_t	destroyLockInstance;
static pthread_spinlock_t	*destroyLock = &destroyLockInstance;

///////////////////
// implementation

// argument parsing

static int parseTimeout(char *arg) {
    int val;

    val = -1;
    if (arg != NULL) {
        val = atoi(arg);
        if (val < 0) {
            val = _SKFS_TIMEOUT_NEVER;
        }
    } else {
        fatalError("Unexpected NULL arg", __FILE__, __LINE__);
    }
    return val;
}

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
        /* Get the input argument from argp_parse, which we
           know is a pointer to our CmdArgs structure. */
		int cacheConcur = 0;
        CmdArgs *arguments = (struct CmdArgs *)state->input;
        switch (key) {
                case SO_HOST:
                        arguments->host = arg;
                        break;
                case SO_ZK_LOC:
                        arguments->zkLoc = arg;
                        break;
                case SO_GC_NAME:
                        arguments->gcname = arg;
                        break;
                case SO_NFS_MAPPING:
                        arguments->nfsMapping = arg;
                        break;
                case SO_PERMANENT_SUFFIXES:
                        arguments->permanentSuffixes = arg;
                        break;
                case SO_NO_ERROR_CACHE_PATHS:
                        arguments->noErrorCachePaths = arg;
                        break;
                case SO_NO_LINK_CACHE_PATHS:
                        arguments->noLinkCachePaths = arg;
                        break;
                case SO_FS_NATIVE_ONLY_FILE:
                        arguments->fsNativeOnlyFile = arg;
                        break;
                case SO_SNAPSHOT_ONLY_PATHS:
                        arguments->snapshotOnlyPaths = arg;
                        break;
                case SO_COMPRESSED_PATHS:
                        arguments->compressedPaths = arg;
                        break;
                case SO_NO_FBW_PATHS:
                        arguments->noFBWPaths = arg;
                        break;
                case SO_FBW_RELIABLE_QUEUE:
                        arguments->fbwReliableQueue = !strcmp(arg, "TRUE") || !strcmp(arg, "true");
                        break;
                case SO_TASK_OUTPUT_PATHS:
                        arguments->taskOutputPaths = arg;
                        break;
                case SO_COMPRESSION:
						if (!strcmp(arg, "NONE")) { 
							arguments->compression = SKCompression::NONE;
						} else if (!strcmp(arg, "ZIP")) { 
							arguments->compression = SKCompression::ZIP;
						} else if (!strcmp(arg, "BZIP2")) { 
							arguments->compression = SKCompression::BZIP2;
						} else if (!strcmp(arg, "SNAPPY")) { 
							arguments->compression = SKCompression::SNAPPY;
                        } else if (!strcmp(arg, "LZ4")) { 
                            arguments->compression = SKCompression::LZ4;
						} else {
							arguments->compression = SKCompression::NONE;
						}
                        break;
                case SO_CHECKSUM:
                    {
						if (!strcmp(arg, "NONE")) { 
                            arguments->checksum = SKChecksumType::NONE;
						} else if (!strcmp(arg, "MD5")) { 
							arguments->checksum = SKChecksumType::MD5;
						} else if (!strcmp(arg, "SHA_1")) { 
							arguments->checksum = SKChecksumType::SHA_1;
						} else if (!strcmp(arg, "MURMUR3_32")) { 
							arguments->checksum = SKChecksumType::MURMUR3_32;
						} else if (!strcmp(arg, "MURMUR3_128")) { 
							arguments->checksum = SKChecksumType::MURMUR3_128;
						} else {
							arguments->checksum = SKChecksumType::NONE;
						}
                        break;
                    }
				case SO_TRANSIENT_CACHE_SIZE_KB:
						arguments->transientCacheSizeKB = atoi(arg);
						break;
				case SO_CACHE_CONCURRENCY:
						cacheConcur = atoi(arg);
						if(cacheConcur > 0) arguments->cacheConcurrency = cacheConcur ;
						if(arguments->cacheConcurrency == 0) arguments->cacheConcurrency = 1;
						break;
                case SO_VERBOSE:
                        arguments->verbose = !strcmp(arg, "TRUE") || !strcmp(arg, "true");
                        break;
                case ARGP_KEY_ARG:
                        // if (state->arg_num >= 2) {
                        /* Too many arguments. */
                        //argp_usage (state);
                        //}
                        //arguments->args[state->arg_num] = arg;
			srfsLog(LOG_WARNING, "would add %d %s", state->arg_num, state->argv[state->arg_num]); fflush(stdout);
                        break;
                case ARGP_KEY_END:
                        //if (state->arg_num < 2) {
                        /* Not enough arguments. */
                        //argp_usage (state);
                        //}
                        break;
                case SO_MOUNT:
						srfsLog(LOG_WARNING, "mount: %s", arg);
                        add_fuse_option(arg);
                        arguments->mountPath = arg;
                        break;
				case SO_LOG_LEVEL:
						arguments->logLevel = arg;
						break;
				case SO_JVM_OPTIONS:
						arguments->jvmOptions = arg;
						break;
                case SO_BIGWRITES:
                    {
						if (strcmp(arg, "NO") || strcmp(arg, "FALSE") || strcmp(arg, "no") || strcmp(arg, "false") || strcmp(arg, "0")) { 
                            arguments->enableBigWrites = 0;
						} else { 
							arguments->enableBigWrites = 1;
						}
						break;
					}
				case SO_ENTRY_TIMEOUT_SECS:
						arguments->entryTimeoutSecs = parseTimeout(arg);
						break;
				case SO_ATTR_TIMEOUT_SECS:
						arguments->attrTimeoutSecs = parseTimeout(arg);
						break;
				case SO_NEGATIVE_TIMEOUT_SECS:
						arguments->negativeTimeoutSecs = parseTimeout(arg);
						break;
                default:
			//printf("Adding %d %s\n", state->arg_num, state->argv[state->arg_num]); fflush(stdout);
			//fuse_opt_add_arg(&fuseArgs, state->argv[state->arg_num]);
                        return ARGP_ERR_UNKNOWN;
			break;
        }
        return 0;
}

static void checkArguments(CmdArgs *arguments) {
    if (arguments->zkLoc == NULL) {
		fatalError(LO_ZK_LOC " not set", __FILE__, __LINE__);
    }
    if (arguments->gcname == NULL) {
		fatalError(LO_GC_NAME " not set", __FILE__, __LINE__);
    }
    if (arguments->host == NULL) {
		fatalError(LO_HOST " not set", __FILE__, __LINE__);
    }
}

void initDefaults(CmdArgs *arguments) {
    arguments->verbose = FALSE;
    arguments->host = "localhost";
    arguments->gcname = NULL;
    arguments->zkLoc = NULL;
    arguments->nfsMapping = NULL;
    arguments->permanentSuffixes = NULL;
	arguments->noErrorCachePaths = NULL;
	arguments->compression = SKCompression::NONE;
	arguments->checksum = SKChecksumType::NONE;
	arguments->transientCacheSizeKB = 0;
	arguments->cacheConcurrency = sysconf(_SC_NPROCESSORS_ONLN);
    arguments->jvmOptions = NULL;
	arguments->enableBigWrites = TRUE;
    arguments->entryTimeoutSecs = -1;
    arguments->attrTimeoutSecs = -1;
    arguments->negativeTimeoutSecs = -1;
}

static void displayArguments(CmdArgs *arguments) {
    printf("verbose %d\n", (int)arguments->verbose);
    printf("host %s\n", arguments->host);
    printf("gcname %s\n", arguments->gcname);
    printf("zkLoc %s\n", arguments->zkLoc);
    printf("nfsMapping %s\n", arguments->nfsMapping);
    printf("permanentSuffixes %s\n", arguments->permanentSuffixes);
    printf("noErrorCachePaths %s\n", arguments->noErrorCachePaths);
    printf("compression %d\n", arguments->compression);
    printf("checksum %d\n", arguments->checksum);
    printf("transientCacheSizeKB %d\n", arguments->transientCacheSizeKB);
    printf("cacheConcurrency %d\n", arguments->cacheConcurrency);
    printf("jvmOptions %s\n", arguments->jvmOptions);
	printf("enableBigWrites %d\n", arguments->enableBigWrites);
	printf("entryTimeoutSecs %d\n", arguments->entryTimeoutSecs);
	printf("attrTimeoutSecs %d\n", arguments->attrTimeoutSecs);
	printf("negativeTimeoutSecs %d\n", arguments->negativeTimeoutSecs);
}

// FUSE interface

static int skfs_getattr(const char *path, struct stat *stbuf) {
	srfsLogAsync(LOG_OPS, "_ga %s", path);
	if (fsNativeOnlyPaths != NULL && pg_matches(fsNativeOnlyPaths, path)) {
		char nativePath[SRFS_MAX_PATH_LENGTH];

		srfsLogAsync(LOG_OPS, "native lstat");
		ar_translate_path(ar, nativePath, path);
		srfsLog(LOG_FINE, "%s -> %s", path, nativePath);
		return lstat(nativePath, stbuf);
	} else {
		int	result;

        result = ENOENT;
		if (is_writable_path(path)) {
            WritableFileReference    *wf_ref;
            
			odt_record_get_attr(odt, (char *)path);
            
			wf_ref = wft_get(wft, path);
			srfsLog(LOG_FINE, "wf_ref %llx", wf_ref);
			if (wf_ref != NULL) {
                WritableFile	    *wf;
			
                wf = wfr_get_wf(wf_ref);
                srfsLog(LOG_FINE, "wf %llx", wf);
				memcpy(stbuf, &wf->fa.stat, sizeof(struct stat));
				result = 0;
                wfr_delete(&wf_ref, aw, fbwSKFS, ar->attrCache);
			} else {
				srfsLog(LOG_FINE, "fa couldn't find writable file in wft %s", path);
                // We will look in the kv store for this case
			}
		}
        if (result != 0) {
            result = ar_get_attr_stat(ar, (char *)path, stbuf);
            srfsLog(LOG_FINE, "result %d %s %d", result, __FILE__, __LINE__);
        }
		srfsLog(LOG_FINE, "stbuf->st_mode %o stbuf->st_nlink %d result %d", stbuf->st_mode, stbuf->st_nlink, result);
        //stat_display(stbuf, stderr);
		return -result;
	}
}

static int ensure_not_writable(char *path1, char *path2) {
    int fileOpen;
    int attemptIndex;
    
    attemptIndex = 0;    
    fileOpen = TRUE;
    do {
        if (wft_contains(wft, path1) || (path2 != NULL && wft_contains(wft, path2))) {
            if (attemptIndex + 1 < _SKFS_RENAME_MAX_ATTEMPTS) {
                srfsLog(LOG_INFO, "ensure_not_writable failed. Sleeping for retry.");
                usleep(_SKFS_RENAME_RETRY_DELAY_MS * 1000);
                ++attemptIndex;
            } else {
                srfsLog(LOG_ERROR, "ensure_not_writable failed/timed out.");
                return FALSE;
            }
        } else {
            fileOpen = FALSE;
        }
    } while (fileOpen);
    return TRUE;
}

static int modify_file_attr(const char *path, char *fnName, mode_t *mode, uid_t *uid, gid_t *gid) {
	if (!is_writable_path(path)) {
		return -EIO;
    } else {    
        WritableFileReference    *wf_ref;
        struct timespec tp;
        time_t  curEpochTimeSeconds;
        long    curTimeNanos;
        
        // get time outside of the critical section
        if (clock_gettime(CLOCK_REALTIME, &tp)) {
            fatalError("clock_gettime failed", __FILE__, __LINE__);
        }    
        curEpochTimeSeconds = tp.tv_sec;
        curTimeNanos = tp.tv_nsec;
        // FUTURE - best effort consistency; future, make rigorous
        //if (!ensure_not_writable((char *)path)) {
        //    srfsLog(LOG_ERROR, "Can't %s writable file", fnName);
        //    return -EIO;
        if (wft_contains(wft, path)) {
            // delay to make sure that there is a chance to release
            usleep(5 * 1000);
            wf_ref = wft_get(wft, path);
         } else {
            wf_ref = NULL;
         }
        
        if (wf_ref != NULL) { // file is currently open; modify writable
            WritableFile    *wf;
            int rc;
            
            wf = wfr_get_wf(wf_ref);
            rc = wf_modify_attr(wf, mode, uid, gid);
            wfr_delete(&wf_ref, aw, fbwSKFS, ar->attrCache);
            return rc;
        } else {
            FileAttr	fa;
            int			result;
        
            memset(&fa, 0, sizeof(FileAttr));
            result = ar_get_attr(ar, (char *)path, &fa);
            if (result != 0) {
                return -ENOENT;
            } else {
                SKOperationState::SKOperationState  writeResult;
            
                fa.stat.st_ctime = curEpochTimeSeconds;
                fa.stat.st_ctim.tv_nsec = curTimeNanos;
                if (mode != NULL) {
                    fa.stat.st_mode = *mode;
                }
                if (uid != NULL) {
                    fa.stat.st_uid = *uid;
                }
                if (gid != NULL) {
                    fa.stat.st_gid = *gid;
                }
                writeResult = aw_write_attr_direct(aw, path, &fa, ar->attrCache);
                if (writeResult != SKOperationState::SUCCEEDED) {
                    srfsLog(LOG_ERROR, "aw_write_attr_direct failed in %s %s", fnName, path);
                    return -EIO;
                } else {
                    return 0;
                }
            }
        }
    }
}

static int skfs_chmod(const char *path, mode_t mode) {
	srfsLogAsync(LOG_OPS, "_cm %s %x", path, mode);
    return modify_file_attr(path, "chmod", &mode, NULL, NULL);
}

static int skfs_chown(const char *path, uid_t uid, gid_t gid) {
	srfsLogAsync(LOG_OPS, "_co %s %d %d", path, uid, gid);
    return modify_file_attr(path, "chmod", NULL, &uid, &gid);
}

static int skfs_truncate(const char *path, off_t size) {
    WritableFile            *wf;
    WritableFileReference   *wf_ref;
    int openedForTruncation;
    int rc;
    
	srfsLogAsync(LOG_OPS, "_t %s %lu", path, size);
    wf_ref = wft_get(wft, path);
	if (wf_ref != NULL) { // file is currently open; we're good
        openedForTruncation = FALSE;
    } else {              // file is not open; open it
        FileAttr	fa;
        int         statResult;
        
        openedForTruncation = TRUE;
        // Check if file exists
        memset(&fa, 0, sizeof(FileAttr));
        statResult = ar_get_attr(ar, (char *)path, &fa);
        if (statResult != 0) {
            return -statResult;
        } else {
            wf_ref = wft_create_new_file(wft, path, S_IFREG | 0666, &fa, pbr);
            if (wf_ref == NULL) {
                return -EIO;
            }
        }
    }    
    wf = wfr_get_wf(wf_ref);
    rc = wf_truncate(wf, size, fbwSKFS, pbr);
    wfr_delete(&wf_ref, aw, fbwSKFS, ar->attrCache);
    
    /*
    This is not needed with current approach as the reference deletion
    will remove the file from the table
    if (openedForTruncation) {
        WritableFileReference    *wf_tableRef;
        int frc;
    
        // If the file wasn't originally open, we need to close it
        // as we just opened it above
        wf_tableRef = wft_remove(wft, path);
        frc = -wf_flush(wf, aw, fbwSKFS, ar->attrCache);
		rc = -wf_close(wf, aw, fbwSKFS, ar->attrCache);
        if (rc == 0 && frc != 0) {
            rc = frc;
        }
        wfr_delete(&wf_tableRef);
    }
    */
    
    return rc;
}

static int skfs_utimens(const char *path, const struct timespec ts[2]) {
	srfsLogAsync(LOG_OPS, "_ut %s", path);
	return 0;
}

static int skfs_access(const char *path, int mask) {
	int	result;
	struct stat stbuf;

	srfsLogAsync(LOG_OPS, "_a %s", path);
	memset(&stbuf, 0, sizeof(struct stat));
	result = ar_get_attr_stat(ar, (char *)path, &stbuf);
	// for now we ignore the mask and just return success
	// if we could stat it
	return -result;
}

static int skfs_readlink(const char *path, char *buf, size_t size) {
	char	linkPath[SRFS_MAX_PATH_LENGTH];
	int		numRead;
	int		fallbackToNFS;
	int		returnSize;

	srfsLogAsync(LOG_OPS, "_rl %s %d", path, size);

	fallbackToNFS = FALSE;
	srfsLog(LOG_FINE, "skfs_readlink %s %u", path, size);
	memset(linkPath, 0, SRFS_MAX_PATH_LENGTH);
	
	if (fsNativeOnlyPaths != NULL && pg_matches(fsNativeOnlyPaths, path)) {
		srfsLogAsync(LOG_OPS, "native readlink");
		fallbackToNFS = TRUE;
	} else {
		if (!ar_is_no_link_cache_path(ar, (char *)path)) {
			numRead = pbr_read(pbr, path, linkPath, SRFS_MAX_PATH_LENGTH, 0);

			if (numRead > 0 && numRead < SRFS_MAX_PATH_LENGTH) {
				char	appendBuf[SRFS_MAX_PATH_LENGTH + SRFS_MAX_PATH_LENGTH + 1];
				char	resultBuf[SRFS_MAX_PATH_LENGTH + SRFS_MAX_PATH_LENGTH + 1];
				char	*fullLinkPath;
				int		pathSimplifyResult;

				memset(appendBuf, 0, SRFS_MAX_PATH_LENGTH + SRFS_MAX_PATH_LENGTH + 1);
				memset(resultBuf, 0, SRFS_MAX_PATH_LENGTH + SRFS_MAX_PATH_LENGTH + 1);
				srfsLog(LOG_FINE, "link %s. linkPath %s", path, linkPath);
				if (linkPath[0] == '/') {
					srfsLog(LOG_FINE, "absolute symlink");
					fullLinkPath = linkPath;
				} else {
					char	pathBase[SRFS_MAX_PATH_LENGTH];
					char	*lastSlash;
					
					strcpy(pathBase, path);
					lastSlash = strrchr(pathBase, '/');
					if (lastSlash != NULL) {
						*lastSlash = '\0';
					}
					srfsLog(LOG_FINE, "relative symlink");
					sprintf(appendBuf, "%s/%s/%s", args->mountPath, pathBase, linkPath);
					fullLinkPath = appendBuf;
				}
				srfsLog(LOG_FINE, "fullLinkPath %s", fullLinkPath);
				pathSimplifyResult = path_simplify(fullLinkPath, resultBuf, SRFS_MAX_PATH_LENGTH + SRFS_MAX_PATH_LENGTH + 1);
				srfsLog(LOG_FINE, "pathSimplifyResult %d", pathSimplifyResult);
				if (pathSimplifyResult == FALSE) {
					fallbackToNFS = TRUE;
				} else {
					srfsLog(LOG_FINE, "simplified path %s", resultBuf);
					returnSize = strlen(resultBuf) + 1;
					if ((size_t)returnSize >= size) {
						returnSize = size - 1;
					}
					memcpy(buf, resultBuf, returnSize);
					buf[returnSize] = '\0';
					returnSize = 0;
				}
			} else {
				srfsLog(LOG_WARNING, "error reading link %s. falling back to nfs", path);
				fallbackToNFS = TRUE;
			}
		} else {
			fallbackToNFS = TRUE;
			srfsLog(LOG_WARNING, "no link cache path %s", path);
		}
	}

	if (fallbackToNFS) {
		char nfsPath[SRFS_MAX_PATH_LENGTH];
		//char readLinkPath[SRFS_MAX_PATH_LENGTH];

		memset(nfsPath, 0, SRFS_MAX_PATH_LENGTH);
		ar_translate_path(ar, nfsPath, path);
		srfsLog(LOG_WARNING, "skfs_readlink fallback nfsPath %s %u", nfsPath);
		returnSize = readlink(nfsPath, buf, size - 1);
		buf[returnSize] = '\0';

		//res = readlink(nfsPath, readLinkPath, size - 1);
		if (returnSize == -1) {
			return -errno;
		}
		//readLinkPath[res] = '\0';

		//translateReversePath(buf, readLinkPath);
	} else {
		srfsLog(LOG_FINE, "skfs_readlink no fallback");
	}
	//return returnSize;
	srfsLog(LOG_FINE, "skfs_readlink complete %s", path);
	return 0;
}


static int nfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                       off_t offset, struct fuse_file_info *fi) {
    DIR *dp;
    struct dirent *de;
    (void) offset;
    (void) fi;
    char nfsPath[SRFS_MAX_PATH_LENGTH];

	ar_translate_path(ar, nfsPath, path);
	srfsLog(LOG_FINE, "opendir %s\t%s", nfsPath, path);
    dp = opendir(nfsPath);
	if (dp == NULL) {
        return -errno;
	}

    while ((de = readdir(dp)) != NULL) {
        struct stat st;
        memset(&st, 0, sizeof(st));
        st.st_ino = de->d_ino;
        st.st_mode = de->d_type << 12;
		if (filler(buf, de->d_name, &st, 0)) {
            break;
		}
    }

    closedir(dp);
    return 0;
}

static int skfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                       off_t offset, struct fuse_file_info *fi) {
	srfsLogAsync(LOG_OPS, "_rd %s %ld", path, offset);
	if (!is_writable_path(path)) {
		if (path[1] != '\0') {
			return nfs_readdir(path, buf, filler, offset, fi);
		} else {
			return -ENOENT;
		}
	} else {
		return odt_readdir(odt, path, buf, filler, offset, fi);
	}
}

static int skfs_opendir(const char* path, struct fuse_file_info* fi) {
	srfsLogAsync(LOG_OPS, "_od %s", path);
	if (!is_writable_path(path)) {
		return 0;
	} else {
		return odt_opendir(odt, path, fi);
	}
}

static int skfs_releasedir(const char* path, struct fuse_file_info *fi) {
	srfsLogAsync(LOG_OPS, "_red %s", path);
	return odt_releasedir(odt, path, fi);
}

static int skfs_mkdir(const char *path, mode_t mode) {
	srfsLogAsync(LOG_OPS, "_mkd%s", path);
	return odt_mkdir(odt, (char *)path, mode);
}

static int skfs_rmdir(const char *path) {
	srfsLogAsync(LOG_OPS, "_rmd%s", path);
	return odt_rmdir(odt, (char *)path);
}

// FUTURE - Need to check/enforce atomicity of operations,
//          particularly mixed metadata/data operations.
//          For now, we perform best-effort rejection of
//          overlapped operations.

static int skfs_rename(const char *oldpath, const char *newpath) {
	srfsLogAsync(LOG_OPS, "_rn %s %s", oldpath, newpath);
	if (!is_writable_path(oldpath) || !is_writable_path(newpath)) {
		return -EIO;
    } else {    
        struct timespec tp;
        FileAttr	fa;
        int			result;
        time_t  curEpochTimeSeconds;
        long    curTimeNanos;

        // get time outside of the critical section
        if (clock_gettime(CLOCK_REALTIME, &tp)) {
            fatalError("clock_gettime failed", __FILE__, __LINE__);
        }    
        curEpochTimeSeconds = tp.tv_sec;
        curTimeNanos = tp.tv_nsec;
        memset(&fa, 0, sizeof(FileAttr));
        result = ar_get_attr(ar, (char *)oldpath, &fa);
        if (result != 0) {
            return -ENOENT;
        } else {
            if (S_ISDIR(fa.stat.st_mode)) {
                return -ENOTSUP;
            } else {
                SKOperationState::SKOperationState  writeResult;
                
                // FUTURE - below is best-effort. enforce
                if (!ensure_not_writable((char *)oldpath, (char *)newpath)) {
                    WritableFileReference   *wfr;

                    wfr = wft_get(wft, oldpath);
                    if (wfr != NULL) {
                        WritableFile    *wf;
                        
                        wf = wfr_get_wf(wfr);
                        wf_set_pending_rename(wf, newpath);
                        wfr_delete(&wfr, aw, fbwSKFS, ar->attrCache);
                        return 0;
                        //srfsLog(LOG_ERROR, "Can't rename writable file");
                        //return -EIO;
                    } else {
                        srfsLog(LOG_ERROR, "Unexpected NULL wfr");
                        return -EIO;
                    }
                } else {
                    fa.stat.st_ctime = curEpochTimeSeconds;
                    fa.stat.st_ctim.tv_nsec = curTimeNanos;
                    // Link the new file to the old file blocks
                    writeResult = aw_write_attr_direct(aw, newpath, &fa, ar->attrCache);
                    if (writeResult != SKOperationState::SUCCEEDED) {
                        srfsLog(LOG_ERROR, "aw_write_attr_direct failed in skfs_rename %s", newpath);
                        return -EIO;
                    } else {
                        // Create a directory entry for the new file
                        if (odt_add_entry_to_parent_dir(odt, (char *)newpath)) {
                            srfsLog(LOG_WARNING, "Couldn't create new entry in parent for %s", newpath);
                            return -EIO;
                        } else {
                            // Delete the old file
                            return _skfs_unlink(oldpath, FALSE);
                        }
                    }
                }
            }
        }
    }
}

static int skfs_associate_new_file(const char *path, struct fuse_file_info *fi, WritableFileReference *wf_ref) {
	if (wf_ref == NULL) {
        srfsLog(LOG_FINE, "skfs_associate_new_file %s NULL ref", path);
        return -ENOENT;
	} else {
		fi->fh = (uint64_t)wf_ref;
        if (srfsLogLevelMet(LOG_FINE)) {
            srfsLog(LOG_FINE, "skfs_associate_new_file %s %llx %llx", path, wf_ref, wfr_get_wf(wf_ref));
        }
		return 0;
	}
}

static int skfs_open(const char *path, struct fuse_file_info *fi) {
    // Below line is LOG_INFO as we don't want to log this for r/o files
	srfsLogAsync(LOG_INFO, "_O %s %x %s%s%s", path, fi->flags,
        ((fi->flags & OPEN_MODE_FLAG_MASK) == O_RDONLY ? "R" : ""),
        ((fi->flags & OPEN_MODE_FLAG_MASK) == O_WRONLY ? "W" : ""),
        ((fi->flags & OPEN_MODE_FLAG_MASK) == O_RDWR ? "RW" : ""),
        ((fi->flags & O_CREAT) ? "c" : ""),
        ((fi->flags & O_EXCL) ? "x" : ""),
        ((fi->flags & O_TRUNC) ? "t" : "")
        );
    
	if (!is_writable_path(path)) {
        /*
        // eventually use this check, but stricter than prev code, so need to test
        if ((fi->flags & OPEN_MODE_FLAG_MASK) == O_RDONLY) {
            return 0;
        } else {
            return -EACCES;
        }
        */
        return 0;
    } else {
        if ((fi->flags & OPEN_MODE_FLAG_MASK) == O_RDONLY) {
            fi->fh = (uint64_t)NULL;
            // for already open writable files, we ignore the ongoing write
            // and use what exists in the key-value store
            return 0;
        } else if ((fi->flags & OPEN_MODE_FLAG_MASK) == O_WRONLY
                    || (fi->flags & OPEN_MODE_FLAG_MASK) == O_RDWR) {
            int	result;
            WritableFileReference    *existing_wf_ref;
            
            srfsLogAsync(LOG_OPS, "_o %s %x", path, fi->flags);
            // FIXME - do we need this in mknod also, or can we remove it there?
            if (odt_add_entry_to_parent_dir(odt, (char *)path)) {
                srfsLog(LOG_WARNING, "Couldn't create new entry in parent for %s", path);
            }
            existing_wf_ref = wft_get(wft, path);
            if (existing_wf_ref == NULL) {
                // File does not exist in the wft
                if ((fi->flags & O_TRUNC) != 0) {
                    WritableFileReference    *wf_ref;
                    
                    // O_TRUNC specified; use a new wf since we don't care about the old data blocks

                    if (((fi->flags & O_CREAT) != 0) && ((fi->flags & O_EXCL) != 0)) {
                        FileAttr	fa;
                        int statResult;

                        // Check if file exists...
                        memset(&fa, 0, sizeof(FileAttr));
                        statResult = ar_get_attr(ar, (char *)path, &fa);
                        if (!statResult) {
                            // Exclusive creation requested, but file already exists => fail
                            return -EEXIST;
                        }
                    }
                    wf_ref = wft_create_new_file(wft, path, S_IFREG | 0666);
                    if (wf_ref != NULL) {
                        result = skfs_associate_new_file(path, fi, wf_ref);
                    } else {
                        srfsLog(LOG_ERROR, "EIO from %s %d", __FILE__, __LINE__);
                        return -EIO;
                    }
                } else {
                    FileAttr	fa;
                    int statResult;

                    // O_TRUNC *not* specified; we need to keep the old data blocks
                    
                    // Check to see if file exists...
                    memset(&fa, 0, sizeof(FileAttr));
                    statResult = ar_get_attr(ar, (char *)path, &fa);
                    if (statResult != 0 && statResult != ENOENT) {
                        // Error checking for existing attr => fail
                        srfsLog(LOG_ERROR, "statResult %d caused EIO from %s %d", statResult, __FILE__, __LINE__);
                        return -EIO;
                    } else {
                        if (statResult != ENOENT) {
                            // File exists
                            if (((fi->flags & O_CREAT) != 0) && ((fi->flags & O_EXCL) != 0)) {
                                // Exclusive creation requested, but file already exists => fail
                                return -EEXIST;
                            } else {
                                WritableFileReference    *wf_ref;
                                
                                wf_ref = wft_create_new_file(wft, path, S_IFREG | 0666, &fa, pbr);
                                if (wf_ref != NULL) {
                                    result = skfs_associate_new_file(path, fi, wf_ref);
                                } else {
                                    return -EIO;
                                }
                            }
                        } else {
                            WritableFileReference    *wf_ref;
                            
                            // File does not exist; create new file
                            wf_ref = wft_create_new_file(wft, path, S_IFREG | 0666);
                            if (wf_ref != NULL) {
                                result = skfs_associate_new_file(path, fi, wf_ref);
                            } else {
                                srfsLog(LOG_ERROR, "EIO from %s %d", __FILE__, __LINE__);
                                return -EIO;
                            }
                        }
                    }
                }
            } else {
                // wf already exists in wft...(file is currently being written on this node)
                // in this clause, we must delete the existing reference if anything goes wrong
                if (((fi->flags & O_CREAT) != 0) && ((fi->flags & O_EXCL) != 0)) {
                    // Exclusive creation requested, but file already exists => fail
                    wfr_delete(&existing_wf_ref, aw, fbwSKFS, ar->attrCache);
                    fi->fh = (uint64_t)NULL;
                    srfsLog(LOG_ERROR, "EEXIST from %s %d", __FILE__, __LINE__);
                    return -EEXIST;
                }
                if ((fi->flags & O_TRUNC) != 0) {
                    // Can't open an already open file with O_TRUNC
                    wfr_delete(&existing_wf_ref, aw, fbwSKFS, ar->attrCache);
                    fi->fh = (uint64_t)NULL;
                    srfsLog(LOG_ERROR, "EIO from %s %d", __FILE__, __LINE__);
                    return -EIO;
                }
                result = skfs_associate_new_file(path, fi, existing_wf_ref);
            }
            return result;
        } else {
            srfsLog(LOG_ERROR, "EACCES from %s %d", __FILE__, __LINE__);
            return -EACCES;
        }
    }
}

static int skfs_mknod(const char *path, mode_t mode, dev_t rdev) {
    WritableFileReference    *wf_ref;
	
    srfsLogAsync(LOG_OPS, "_mn %s %x %x", path, mode, rdev);
	wf_ref = wft_create_new_file(wft, path, mode);
	if (wf_ref != NULL) {
		if (odt_add_entry_to_parent_dir(odt, (char *)path)) {
			srfsLog(LOG_WARNING, "Couldn't create new entry in parent for %s", path);
		}
        wfr_delete(&wf_ref, aw, fbwSKFS, ar->attrCache);
		return 0;
	} else {
		return -EEXIST;
	}
}

static int _skfs_unlink(const char *path, int deleteBlocks) {
    // FUTURE - make parent removal and wft deletion locally atomic
    if (odt_rm_entry_from_parent_dir(odt, (char *)path)) {
        srfsLog(LOG_WARNING, "Couldn't rm entry in parent for %s", path);
        return -EIO;
    } else {
        return wft_delete_file(wft, path, deleteBlocks);
    }
}

static int skfs_unlink(const char *path) {
	srfsLogAsync(LOG_OPS, "_ul %s", path);
    // FUTURE - below is best-effort. enforce
    if (wft_contains(wft, path)) {
        srfsLog(LOG_ERROR, "Can't unlink writable file");
        return -EIO;
    } else {
        return _skfs_unlink(path, TRUE);
    }
}

/*
static void md5ErrorDebug(const char *path, SKVal *pRVal) {
    char fileName[SRFS_MAX_PATH_LENGTH];
    char modPath[SRFS_MAX_PATH_LENGTH];
    int	fd;
    int	i;
    int	modPathLength;

    strcpy(modPath, path);
    modPathLength = strlen(modPath);
    for (i = 0; i < modPathLength; i++) {
		if (modPath[i] == '/') {
			modPath[i] = '_';
		}
    }
    sprintf(fileName, "/tmp/%s.%d.%d", modPath, pRVal->m_len, clock());
    fd = open(fileName, O_CREAT | O_WRONLY);
    if (fd == -1) {
		printf("Unable to create md5 debug file\n");
    } else {
		int totalWritten;

		totalWritten = 0;
		while (totalWritten < pRVal->m_len) {
			int written;

			written = write(fd, &((char *)pRVal->m_pVal)[totalWritten], pRVal->m_len - totalWritten);	
			if (written == -1) {
				printf("Error writing md5 debug file\n");
				close(fd);
				return;
			}
			totalWritten += written;
		}
		close(fd);
    }
}
*/

static int skfs_read(const char *path, char *dest, size_t readSize, off_t readOffset,
                    struct fuse_file_info *fi) {
	int	totalRead;
	
	srfsLogAsync(LOG_OPS, "_r %s %d %ld", path, readSize, readOffset);
	if (fsNativeOnlyPaths != NULL && pg_matches(fsNativeOnlyPaths, path)) {
		char nativePath[SRFS_MAX_PATH_LENGTH];

		srfsLogAsync(LOG_OPS, "native read");
		ar_translate_path(ar, nativePath, path);
		srfsLog(LOG_FINE, "%s -> %s", path, nativePath);
		return file_read_partial(nativePath, dest, readSize, readOffset);
	} else {
        if (fi->fh != (uint64_t)NULL) {
            // We currently do not support reading of files opened r/w
            return -EIO;
        } else {
            totalRead = pbr_read(pbr, path, dest, readSize, readOffset);
#if 0
            srfsLog(LOG_WARNING, "\nFile read stats");
            ac_display_stats(ar->attrCache);
            fbc_display_stats(fbr->fileBlockCache);
#endif
        }
	}
	return totalRead;
}

int skfs_write(const char *path, const char *src, size_t writeSize, off_t writeOffset, 
          struct fuse_file_info *fi) {
    WritableFile *wf;
    size_t  totalBytesWritten;
    
	srfsLogAsync(LOG_OPS, "_w %s %d %ld", path, writeSize, writeOffset);
	wf = wf_fuse_fi_fh_to_wf(fi);
	if (wf == NULL) {
		srfsLog(LOG_ERROR, "skfs_write. No valid fi->fh found for %s", path);			
		return -EIO;
	} else {
		return wf_write(wf, src, writeSize, writeOffset, fbwSKFS, pbr, fbr->fileBlockCache);
	}
}

int skfs_release(const char *path, struct fuse_file_info *fi) {
    WritableFileReference *wf_ref;

    wf_ref = (WritableFileReference *)fi->fh;
	if (wf_ref == NULL) {
		srfsLog(LOG_FINE, "skfs_release. Ignoring. No valid fi->fh found for %s", path);
		return 0;
	} else {
        WritableFile    *wf;
        
		srfsLogAsync(LOG_OPS, "_re %s %lx", path, fi);	
        wfr_sanity_check(wf_ref);
        wf = wfr_get_wf(wf_ref);
        if (wf->pendingRename != NULL) {
            int     rc;
            char    _old[SRFS_MAX_PATH_LENGTH];
            char    _new[SRFS_MAX_PATH_LENGTH];
            
            strncpy(_old, wf->path, SRFS_MAX_PATH_LENGTH);
            strncpy(_new, wf->pendingRename, SRFS_MAX_PATH_LENGTH);
            rc = wfr_delete(&wf_ref, aw, fbwSKFS, ar->attrCache);
            srfsLog(LOG_WARNING, "release found pending rename %s --> %s", _old, _new);
            skfs_rename(_old, _new);
            return rc;
        } else {
            return wfr_delete(&wf_ref, aw, fbwSKFS, ar->attrCache);
        }
        
	}
}

int skfs_flush(const char *path, struct fuse_file_info *fi) {
    WritableFile *wf;
	
	wf = wf_fuse_fi_fh_to_wf(fi);
	if (wf == NULL) {
		srfsLog(LOG_FINE, "skfs_release. Ignoring. No valid fi->fh found for %s", path);			
		return 0;
	} else {
		srfsLogAsync(LOG_OPS, "_f %s", path);
		return -wf_flush(wf, aw, fbwSKFS, ar->attrCache);
	}
    return 0;
}

static int skfs_statfs(const char *path, struct statvfs *stbuf) {
    int res;

	srfsLogAsync(LOG_OPS, "_s %s", path);
    res = statvfs(path, stbuf);
    if (res == -1)
        return -errno;

    return 0;
}

static int skfs_symlink(const char* to, const char* from) {
	srfsLogAsync(LOG_OPS, "_sl %s %s", to, from);
	if (!is_writable_path(from)) {
		return -EIO;
	} else {
		if (odt_add_entry_to_parent_dir(odt, (char *)from)) {
			srfsLog(LOG_WARNING, "Couldn't create new entry in parent for %s", from);
			return -EIO;
		} else {
            WritableFileReference *wf_ref;
            
            wf_ref = wft_create_new_file(wft, from, S_IFLNK | 0666);
            if (wf_ref == NULL) {
                return -EIO;
            } else {
                WritableFile *wf;
                int          writeResult;
                int          wfrDeletionResult;
                
                wf = wfr_get_wf(wf_ref);
                writeResult = wf_write(wf, to, strlen(to) + 1, 0, fbwSKFS, pbr, fbr->fileBlockCache);
                srfsLog(LOG_FINE, "writeResult %d", writeResult);
                wfrDeletionResult = wfr_delete(&wf_ref, aw, fbwSKFS, ar->attrCache);
                srfsLog(LOG_FINE, "wfrDeletionResult %d", wfrDeletionResult);
                if (writeResult != -1) {
                    return 0;
                } else {
                    return -1;
                }
            }
            /*
			wf = wf_new(from, S_IFLNK | 0666, aw);
			writeResult = wf_write(wf, to, strlen(to) + 1, 0, fbwSKFS, pbr, fbr->fileBlockCache);
			if (writeResult > 0) {
				int	flushResult;
				int	closeResult;
				
                flushResult = wf_flush(wf, aw, fbwSKFS, ar->attrCache);
                if (flushResult != 0) {
                    return -flushResult;
                } else {
                    closeResult = wf_close(wf, aw, fbwSKFS, ar->attrCache); // close also deletes wf
                    return -closeResult;
                }
			} else {
				wf_delete(&wf);
				return writeResult;
			}
            */
		}
	}
}

#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
/*
static int skfs_getxattr(const char *path, const char *name, char *value,
                    size_t size) {
    int res = lgetxattr(path, name, value, size);
    if (res == -1)
        return -errno;
    return res;
}

static int skfs_listxattr(const char *path, char *list, size_t size) {
    int res = llistxattr(path, list, size);
    if (res == -1)
        return -errno;
    return res;
}
*/
#endif /* HAVE_SETXATTR */

static void read_fs_native_only_paths() {
	if (fsNativeOnlyFile != NULL) {
		char    *pgDef;	
        size_t  length;
		
		srfsLog(LOG_WARNING, "Reading fsNativeOnlyFile %s", fsNativeOnlyFile);
        length = 0;
		pgDef = file_read(fsNativeOnlyFile, &length);
		if (pgDef != NULL) {
			PathGroup	*newPG;
			
			newPG = (PathGroup *)pg_new("fsNativeOnlyPaths");
            trim_in_place(pgDef, length);
			pg_parse_paths(newPG, pgDef);
			fsNativeOnlyPaths = newPG;
			// NOTE - we intentionally leak the old pg as the size of
			// the lost memory is not worth the reference counting
			mem_free((void **)&pgDef, __FILE__, __LINE__);
			srfsLog(LOG_WARNING, "Done reading fsNativeOnlyFile %s", fsNativeOnlyFile);
		} else {
			srfsLog(LOG_ERROR, "Unable to read fsNativeOnlyFile %s", fsNativeOnlyFile);
		}		
	}
}

static void signal_handler(int signal) {
	srfsLog(LOG_WARNING, "Received signal %d", signal);
	//read_fs_native_only_paths();
}


static void segv_handler(int, siginfo_t*, void*) {
	print_stacktrace("segv_handler");
    exit(1);
}

static void abrt_handler(int, siginfo_t*, void*) {
	print_stacktrace("abrt_handler");
    exit(1);
}

static void term_handler() {
	print_stacktrace("terminate_handler");
}

static void unexpct_handler() {
	print_stacktrace("unexpected_handler");
    //skfs_destroy(NULL);
    //exit(1);
}

static int install_handler() {
	struct sigaction sigact;
	sigact.sa_flags = SA_SIGINFO | SA_ONSTACK;
/*
	sigact.sa_sigaction = segv_handler;
	if (sigaction(SIGSEGV, &sigact, (struct sigaction *)NULL) != 0) {
		fprintf(stderr, "error setting signal handler for %d (%s)\n",
		SIGSEGV, strsignal(SIGSEGV));
	}

    std::set_terminate(term_handler);
*/
/*
	sigact.sa_sigaction = abrt_handler;
	if (sigaction(SIGABRT, &sigact, (struct sigaction *)NULL) != 0) {
		fprintf(stderr, "error setting signal handler for %d (%s)\n",
		SIGABRT, strsignal(SIGABRT));
	}
*/
    std::set_unexpected(unexpct_handler);
	return 0;
}

static void *skfs_init(struct fuse_conn_info *conn) {
	pthread_spin_init(destroyLock, 0);
	srfsLogInitAsync(); // Important - this must be initialized here and not in main() due to fuse process configuration
	srfsLogAsync(LOG_OPS, "LOG_OPS async check");
	srfsLog(LOG_WARNING, "skfs_init()");
    install_handler();
	initDHT();
	fid_module_init();
	initReaders();
	initDirs();
	wft = wft_new("WritableFileTable", aw, ar->attrCache, ar, fbwSKFS);
	initPaths();
	pthread_create(&statsThread, NULL, stats_thread, NULL);
	if(fsNativeOnlyFile) {
		//if nativeOnlyFile name is supplied, then create this thread 
		pthread_create(&nativeFileWatcherThread, NULL, nativefile_watcher_thread, NULL);
	}
	read_fs_native_only_paths();
	srfsRedirectStdio();
	srfsLog(LOG_WARNING, "skfs_init() complete");
	return NULL;
}

static void skfs_destroy(void* private_data) {
    int doDestroy;
    
	exitSignalReceived = TRUE;
    
    pthread_spin_lock(destroyLock);
    if (!destroyCalled) {
        destroyCalled = TRUE;
        doDestroy = TRUE;
    } else {
        doDestroy = FALSE;
    }
    pthread_spin_unlock(destroyLock);

    if (doDestroy) {
        srfsLog(LOG_WARNING, "skfs_destroy()");
        //srfsRedirectStdio(); //TODO: think about this
        /*
        srfsLog(LOG_WARNING, "skfs_destroy() waiting for threads");
        pthread_join(statsThread, NULL);
        if (fsNativeOnlyFile) {
            pthread_join(nativeFileWatcherThread, NULL);
        }
        srfsLog(LOG_WARNING, "skfs_destroy() done waiting for threads");
        */
        destroyPaths();
        destroyReaders();
        destroyDHT();
        srfsLog(LOG_WARNING, "skfs_destroy() complete");
    }
}

static struct fuse_operations skfs_oper = {
	/* getattr */ NULL,
	/* readlink */ NULL,
	/* getdir */ NULL,
	/* mknod */ NULL,
	/* mkdir */ NULL,
	/* unlink */ NULL,
	/* rmdir */ NULL,
	/* symlink */ NULL,
	/* rename */ NULL,
	/* link */ NULL,
	/* chmod */ NULL,
	/* chown */ NULL,
	/* truncate */ NULL,
	/* utime */ NULL,
	/* open */ NULL, //skfs_open,
	/* read */ NULL, //skfs_read,
	/* write */ NULL,
	/* statfs */ NULL,
	/* flush */ NULL,
	/* release */ NULL,
	/* fsync */ NULL,
	/* setxattr */ NULL,
	/* getxattr */ NULL,
	/* listxattr */ NULL,
	/* removexattr */ NULL,
	/* opendir */ NULL,
	/* readdir */ NULL,
	/* releaseddir */ NULL,
	/* fsyncdir */ NULL,
	/* init */ NULL,    //skfs_init,
	/* destroy */ NULL, //skfs_destroy,
	/* access */ NULL,
	/* create */ NULL,
	/* ftruncate */ NULL,
	/* fgetattr */ NULL,
	/* lock */ NULL,
	/* utimens */ NULL,
	/* bmap */ NULL
#if  FUSE_USE_VERSION > 27
	, /*flag_nullpath_ok*/ 0
	, /*flag_reserved*/ 0
	, /*ioctl*/ NULL
	, /*poll*/ NULL
#endif	
};

static void *stats_thread(void *) {
	int	detailPeriod;
	int	detailPhase;
	
	detailPeriod = statsDetailIntervalSeconds / statsIntervalSeconds;
	detailPhase = 0;
	while (!exitSignalReceived) {
		int	detailFlag;
		
		detailFlag = detailPhase == 0;
		sleep(statsIntervalSeconds);
		srfsLog(LOG_WARNING, "\n\t** stats **");
		ar_display_stats(ar, detailFlag);
		fbr_display_stats(fbr, detailFlag);
		detailPhase = (detailPhase + 1) % detailPeriod;
	}
	return NULL;
}


static void * nativefile_watcher_thread(void * unused) {
  //passed params are unused currently 

  // do forever in loop:
  //  - if file not exist, keep checking until it appears
  //  - then, set native only paths
  //  - set watcher
  //  - process events accordingly

  int length, i = 0;
  char buffer[INOTIFY_BUF_LEN];

  while ( !exitSignalReceived ){
	  int fd = -1;
	  int wd = -1;
	  // if cannot open file, then sleep and re-try.
	  FILE* fh = NULL;
	  while( (fh = fopen(fsNativeOnlyFile, "r")) == NULL  ) {
		srfsLog(LOG_WARNING, "cannot find or open native only file %s, sleeping \n", fsNativeOnlyFile );
		sleep( 20 );
	  }
	  fclose(fh); fh = NULL; //close handle opened above

	  read_fs_native_only_paths();

	  fd = inotify_init();

	  if ( fd < 0 ) {
		perror( "inotify_init" );
	  }
  
	  srfsLog(LOG_INFO, "inotify_add_watch %d \n", fd );

	  wd = inotify_add_watch( fd, fsNativeOnlyFile, IN_MODIFY | IN_CREATE | IN_MOVE_SELF | IN_DELETE_SELF );
	  if ( wd < 0 ) {
		perror( "inotify_add_watch" );
	  }

	  bool oldFileIsValid = 1;  //indicates validity/obsoletion of the watched file 
	  while  ( oldFileIsValid ) {  // READ loop
		i = 0;
		//srfsLog(LOG_FINE, "calling read %d %d \n", wd, fd  );
		length = read( fd, buffer, INOTIFY_BUF_LEN );  

		if ( length < 0 ) {
		  perror( "read" );
		}
  
		//srfsLog(LOG_FINE, "processing read ... %d\n", length );
		while ( i < length ) {  // EVENT loop
		  struct inotify_event *event = ( struct inotify_event * ) &buffer[ i ];
		  srfsLog(LOG_FINE, "event  %d : %d : %s\n", event->len, event->mask, event->name );
		  //if ( event->len ) {
			if ( event->mask & IN_CREATE ) {
				srfsLog(LOG_INFO, "The file %s was created.\n", event->name );
				read_fs_native_only_paths();
			}
			else if ( event->mask & IN_DELETE_SELF ) {
				srfsLog(LOG_INFO, "The file %s was deleted.\n", event->name );
				oldFileIsValid = 0;
				//break;  //file's gone 
			}
			else if (  event->mask & IN_MOVE_SELF ) {
				srfsLog(LOG_INFO, "The file %s was moved.\n", event->name );
				oldFileIsValid = 0;
				//break;
			}
			else if ( event->mask & IN_MODIFY ) {
				srfsLog(LOG_INFO, "IN_MODIFY.\n", event->name );
				read_fs_native_only_paths();
			}
		  //}
		  i += EVENT_SIZE + event->len;
		}  // end of EVENT loop

	  } // end of READ loop

	  if(wd >= 0)
		( void ) inotify_rm_watch( fd, wd );
	  if(fd >= 0)
		( void ) close( fd );

  }  // end of main loop

}

void initFuse() {
    skfs_oper.getattr = skfs_getattr;
    skfs_oper.read = skfs_read;
    skfs_oper.readlink = skfs_readlink;
    skfs_oper.readdir = skfs_readdir;
    skfs_oper.flush = skfs_flush;
	skfs_oper.init = skfs_init;
    skfs_oper.access = skfs_access;
	skfs_oper.destroy = skfs_destroy;
	
    skfs_oper.open = skfs_open;
    skfs_oper.release = skfs_release;
    skfs_oper.mknod = skfs_mknod;
    skfs_oper.write = skfs_write;
	skfs_oper.chmod = skfs_chmod;
	skfs_oper.chown = skfs_chown;
	skfs_oper.truncate = skfs_truncate;
	skfs_oper.utimens = skfs_utimens;
	skfs_oper.releasedir = skfs_releasedir;
    skfs_oper.opendir = skfs_opendir;
    skfs_oper.symlink = skfs_symlink;
	
	skfs_oper.mkdir = skfs_mkdir;

	skfs_oper.rmdir = skfs_rmdir;
	skfs_oper.unlink = skfs_unlink;	
    
    skfs_oper.rename = skfs_rename;
}

void initDHT() {
    try {
        defaultChecksum    = args->checksum;
        defaultCompression = args->compression;

        LoggingLevel lvl = LVL_WARNING;
        if (!strcmp(args->logLevel, "FINE")) {
            lvl = LVL_ALL;
        } else if (!strcmp(args->logLevel, "INFO")) {
            lvl = LVL_INFO;
        } else if (!strcmp(args->logLevel, "OPS")) {
            lvl = LVL_WARNING;
        //} else {
        //	lvl = LVL_INFO;
        }

        pClient = SKClient::getClient(lvl, args->jvmOptions);
        if (!pClient) {
            srfsLog(LOG_WARNING, "dht client init failed. Will continue with NFS-only");
        }

        SKGridConfiguration * pGC = SKGridConfiguration::parseFile(args->gcname);
        SKClientDHTConfiguration * pCdc = pGC->getClientDHTConfiguration();
        sessOption = new SKSessionOptions(pCdc, args->host) ;
        pGlobalSession = pClient->openSession(sessOption);
        if (!pGlobalSession ) {
            srfsLog(LOG_WARNING, "dht session init failed. Will continue with NFS-only\n");
        }

        delete pGC;
        delete pCdc;
        srfsLog(LOG_FINE, "out initDHT");
    } catch(std::exception& e) { 
        srfsLog(LOG_WARNING, "initDHT() exception: %s", e.what()); 
    }
}

void destroyDHT() {
	delete pGlobalSession; pGlobalSession = NULL;
	delete pClient; pClient = NULL;
    delete sessOption; sessOption = NULL;
	srfsLog(LOG_WARNING, "SKClient shutdown");
	SKClient::shutdown();
}

// FUTURE: probably remove task output support
PathGroup *initTaskOutputPaths(int* taskOutputPort ) {
	PathGroup	*taskOutputPaths;

	srfsLog(LOG_WARNING, "initTaskOutputPaths %s", (char *)args->taskOutputPaths);
	taskOutputPaths = pg_new("taskOutputPaths");
	pg_parse_paths(taskOutputPaths, (char *)args->taskOutputPaths);
	if (pg_size(taskOutputPaths) == 1) {
		char	*delimiter;
		char	*gcName;
		char	*path;

		path = pg_get_member(taskOutputPaths, 0);
		delimiter = strchr(pg_get_member(taskOutputPaths, 0), '#');
		if (delimiter != NULL) {
			*delimiter = '\0';
			gcName = delimiter + 1;
			srfsLog(LOG_WARNING, "Setting grid configuration %s", gcName);
			//*taskOutputPort = dht_set_grid_configuration(gcName, NULL, args->host);
			ar_store_dir_attribs(ar, path);
		}
	} else if (pg_size(taskOutputPaths) > 1) {
		srfsLog(LOG_WARNING, "Too many taskOutputPaths for first-cut implementation\n");
	}
	return taskOutputPaths;
}

void initPaths() {
	ar_parse_native_aliases(ar, (char *)args->nfsMapping);
	ar_parse_no_error_cache_paths(ar, (char *)args->noErrorCachePaths);
	ar_parse_no_link_cache_paths(ar, (char *)args->noLinkCachePaths);
	fsNativeOnlyFile = (char *)args->fsNativeOnlyFile;
	ar_parse_snapshot_only_paths(ar, (char *)args->snapshotOnlyPaths);
	fbr_parse_compressed_paths(fbr, (char *)args->compressedPaths);
	fbr_parse_no_fbw_paths(fbr, (char *)args->noFBWPaths);
	fbr_parse_permanent_suffixes(fbr, (char *)args->permanentSuffixes);
	//Namespaces for SKFS must be created by external scripts ; 
}

void destroyPaths() {
}

void initReaders() {
	uint64_t	transientCacheSizeBlocks;

	sd = sd_new((char *)args->host, (char *)args->gcname, NULL, args->compression, 
				SRFS_DHT_OP_MIN_TIMEOUT_MS, SRFS_DHT_OP_MAX_TIMEOUT_MS, 
				SRFS_DHT_OP_DEV_WEIGHT, SRFS_DHT_OP_DHT_WEIGHT);

	f2p = f2p_new();
	aw = aw_new(sd);
	awSKFS = aw_new(sd);
	fbwCompress = fbw_new(sd, TRUE, args->fbwReliableQueue);
	fbwRaw = fbw_new(sd, FALSE, args->fbwReliableQueue);
	fbwSKFS = fbw_new(sd, TRUE, args->fbwReliableQueue);
	rtsAR_DHT = rts_new(SRFS_RTS_DHT_ALPHA, SRFS_RTS_DHT_OP_TIME_INITIALIZER);
	rtsAR_NFS = rts_new(SRFS_RTS_NFS_ALPHA, SRFS_RTS_NFS_OP_TIME_INITIALIZER);
	rtsFBR_DHT = rts_new(SRFS_RTS_DHT_ALPHA, SRFS_RTS_DHT_OP_TIME_INITIALIZER);
	rtsFBR_NFS = rts_new(SRFS_RTS_NFS_ALPHA, SRFS_RTS_NFS_OP_TIME_INITIALIZER);
	rtsODT = rts_new(SRFS_RTS_DHT_ALPHA, SRFS_RTS_DHT_OP_TIME_INITIALIZER);
	ar = ar_new(f2p, sd, aw, rtsAR_DHT, rtsAR_NFS, args->cacheConcurrency, args->attrTimeoutSecs * 1000);
	ar_store_dir_attribs(ar, SKFS_WRITE_BASE, 0777);
	int taskOutputPort = -1;
	//PathGroup * pg = initTaskOutputPaths(&taskOutputPort);
	//srfsLog(LOG_WARNING, "taskOutputPort %d", taskOutputPort);
	ar_set_g2tor(ar, NULL);
	transientCacheSizeBlocks = (uint64_t)args->transientCacheSizeKB * (uint64_t)1024 / (uint64_t)SRFS_BLOCK_SIZE;
	fbr = fbr_new(f2p, fbwCompress, fbwRaw, sd, rtsFBR_DHT, rtsFBR_NFS, args->cacheConcurrency, transientCacheSizeBlocks);
	pbr = pbr_new(ar, fbr, NULL);

#if 0
    wft = wft_new("WritableFileTable");
#endif
}

// Initialization writable directory structure
void initDirs() {
	rcst_init();
	odt = odt_new(ODT_NAME, sd, aw, ar, rtsODT);
	if (odt_mkdir_base(odt)) {
		fatalError("odt_mkdir_base skfs failed", __FILE__, __LINE__);
	}
	if (odt_opendir(odt, SKFS_WRITE_BASE, NULL) != 0) {
		fatalError("odt_openDir skfs failed", __FILE__, __LINE__);
	}
}

void destroyReaders(){
	//nft_delete(&nft);
	pbr_delete(&pbr);
	fbr_delete(&fbr);
	ar_delete(&ar);
	odt_delete(&odt);
	rts_delete(&rtsFBR_NFS);
	rts_delete(&rtsFBR_DHT);
	rts_delete(&rtsAR_NFS);
	rts_delete(&rtsAR_DHT);
	fbw_delete(&fbwRaw);
	fbw_delete(&fbwCompress);
	fbw_delete(&fbwSKFS);
	aw_delete(&aw);
	aw_delete(&awSKFS);
	f2p_delete(&f2p);
	sd_delete(&sd);
}

static void initLogging() {
	sprintf(logFileName, "%s/logs/fuse.log.%d", SRFS_HOME, getpid());
	srfsLogSetFile(logFileName);
	srfsLog(LOG_WARNING, "skfs block size %d", SRFS_BLOCK_SIZE);
	if (args->logLevel != NULL) {
		if (!strcmp(args->logLevel, "FINE")) {
			setSRFSLogLevel(LOG_FINE);
		} else if (!strcmp(args->logLevel, "OPS")) {
			setSRFSLogLevel(LOG_OPS);
		} else if (!strcmp(args->logLevel, "INFO")) {
			setSRFSLogLevel(LOG_INFO);
		} else {
			setSRFSLogLevel(LOG_WARNING);
		}
	} else {
		setSRFSLogLevel(LOG_WARNING);
	}
	srfsLog(LOG_FINE, "LOG_FINE check");
	srfsLog(LOG_OPS, "LOG_OPS check");
	srfsLog(LOG_WARNING, "LOG_WARNING check");
}

bool addEnvToFuseArg(const char * envVarName, const char * fuseOpt, const char *defaultFuseOpt) {
	char * pEnd = NULL;
	long envLong = 0;
	const char * pEnvStr = getenv(envVarName);
	if(pEnvStr)
		envLong = strtol(pEnvStr, &pEnd, 10);
	std::string entryStr = ( !pEnvStr || envLong < 1 || envLong == LONG_MAX ) ?
		defaultFuseOpt : std::string(fuseOpt) + std::string(pEnvStr, pEnd - pEnvStr);
	fuse_opt_add_arg(&fuseArgs, entryStr.c_str());
	
	srfsLog( LOG_WARNING, envVarName );
	srfsLog( LOG_WARNING, entryStr.c_str() );
	return TRUE;
}

void parseArgs(int argc, char *argv[], CmdArgs *arguments) {
    argp_parse(&argp, argc, argv, 0, 0, arguments);
}

static void add_fuse_option(char *option) {
    int result;
    
    result = fuse_opt_add_arg(&fuseArgs, option);
    srfsLog(LOG_WARNING, "%s", option);
    fflush(0);
    if (result != 0) {
        srfsLog(LOG_WARNING, "%s failed: %d", option, result);
        fflush(0);
        fatalError("Adding option failed", __FILE__, __LINE__);
    }
}

#ifndef UNIT_TESTS
int main(int argc, char *argv[]) {        
    umask(022);
    pClient = NULL; pGlobalSession = NULL;

	initFuse();

    fuse_opt_add_arg(&fuseArgs, argv[0]);
    initDefaults(&_args);
    //argp_parse(&argp, argc, argv, 0, 0, &_args);
	parseArgs(argc, argv, &_args);
    displayArguments(&_args);
    //checkArguments(&_args);

	initLogging();

	if (RUNNING_ON_VALGRIND) {
		fprintf(stderr,
			"**** WARNING : Valgrind has been detected. If this fails to run in Valgrind\n"
			"**** then lookup procedures for valgrind-and-fuse-file-systems \n"
			"**** Sleeping for 5 seconds \n");
		sleep(5);
		fprintf(stderr, "\n");
	}
	if (args->verbose || RUNNING_ON_VALGRIND) {  //if either on Valgrind or Debug env var was specified
		add_fuse_option("-d");
	    srfsLog(LOG_WARNING, "Verbose on");
	} else {
	    srfsLog(LOG_WARNING, "Verbose off");
	}

	if (!access(FUSE_CONF_FILE, R_OK)) {
	    srfsLog(LOG_WARNING, "Found %s", FUSE_CONF_FILE);
	    add_fuse_option("-oallow_other");
	} else {
	    srfsLog(LOG_WARNING, "No %s", FUSE_CONF_FILE);
	}
	
	if (args->enableBigWrites) {
        srfsLog(LOG_WARNING, "Enabling big writes");
		add_fuse_option("-obig_writes");
	}
    
    //add_fuse_option("-odirect_io"); // for debugging only    
    
	add_fuse_option("-ononempty");
    add_fuse_option("-ouse_ino");
    add_fuse_option("-oauto_cache");
    
	sprintf(fuseEntryOption, "-oentry_timeout=%d", args->entryTimeoutSecs >= 0 ? args->entryTimeoutSecs : SKFS_DEF_ENTRY_TIMEOUT_SECS);
	sprintf(fuseAttrOption, "-oattr_timeout=%d", args->attrTimeoutSecs >= 0 ? args->attrTimeoutSecs : SKFS_DEF_ATTR_TIMEOUT_SECS);
	sprintf(fuseACAttrOption, "-oac_attr_timeout=%d", args->attrTimeoutSecs >= 0 ? args->attrTimeoutSecs : SKFS_DEF_ATTR_TIMEOUT_SECS);
	sprintf(fuseNegativeOption, "-onegative_timeout=%d", args->negativeTimeoutSecs >= 0 ? args->negativeTimeoutSecs : SKFS_DEF_NEGATIVE_TIMEOUT_SECS);
	
	addEnvToFuseArg("SKFS_ENTRY_TIMEOUT", "-oentry_timeout=", fuseEntryOption);
	addEnvToFuseArg("SKFS_ATTR_TIMEOUT", "-oattr_timeout=", fuseAttrOption);
	addEnvToFuseArg("SKFS_AC_ATTR_TIMEOUT", "-oac_attr_timeout=", fuseACAttrOption);
	addEnvToFuseArg("SKFS_NEGATIVE_TIMEOUT", "-onegative_timeout=", fuseNegativeOption);
	addEnvToFuseArg("SKFS_MAX_READAHEAD", "-omax_readahead=", "-omax_readahead=1048576");
    
	//fuse_opt_add_arg(&fuseArgs, "-osubtype=SKFS");
	srfsLog(LOG_WARNING, "Calling fuse_main");
	int retCode = fuse_main(fuseArgs.argc, fuseArgs.argv, &skfs_oper, NULL);
	fprintf(stderr, "fuse_main() returned %d\n", retCode);
	//fuseArgs are not freed
	return retCode;
}
#endif  //UNIT_TESTS
