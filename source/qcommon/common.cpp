/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// common.c -- misc functions used in client and server
#include "qcommon.h"
#include "loglines.h"
#include "freelistallocator.h"
#include "wswstaticvector.h"

#include <clocale>

#if ( defined( _MSC_VER ) && ( defined( _M_IX86 ) || defined( _M_AMD64 ) || defined( _M_X64 ) ) )
// For __cpuid() intrinsic
#include <intrin.h>
#endif

#ifndef _WIN32
#include <unistd.h>
#	ifdef __APPLE__
#	include <sys/sysctl.h>
#	endif
#else
#define NOMINMAX
#include <windows.h>
#endif

#include "wswcurl.h"
#include "steam.h"
#include "glob.h"
#include "md5.h"
#include "../qcommon/cjson.h"
#include "mmcommon.h"
#include "compression.h"

#include <setjmp.h>
#include <mutex>

#define MAX_NUM_ARGVS   50

static bool commands_intialized = false;

static int com_argc;
static char *com_argv[MAX_NUM_ARGVS + 1];
static char com_errormsg[MAX_PRINTMSG];

static bool com_quit;

static jmp_buf abortframe;     // an ERR_DROP occured, exit the entire frame

cvar_t *host_speeds;
cvar_t *developer;
cvar_t *timescale;
cvar_t *dedicated;
cvar_t *versioncvar;
cvar_t *com_logCategoryMask;
cvar_t *com_logSeverityMask;

static cvar_t *fixedtime;
static cvar_t *logconsole = NULL;
static cvar_t *logconsole_append;
static cvar_t *logconsole_flush;
static cvar_t *logconsole_timestamp;
static cvar_t *com_introPlayed3;

static qmutex_t *com_print_mutex;

// TODO: Use magic_enum to get the enum cardinality?
static const char *kLogLineColorForSeverity[4] {
	S_COLOR_GREY, S_COLOR_WHITE, S_COLOR_YELLOW, S_COLOR_RED
};

static const char *kLogLinePrefixForCategory[10] {
	"COM", " SV", " CL", "  S", "  R", " UI", " GS", " CG", "  G", " AI"
};

class alignas( 16 ) LogLineStreamsAllocator {
	std::recursive_mutex m_mutex;
	wsw::HeapBasedFreelistAllocator m_allocator;

	static constexpr size_t kSize = MAX_PRINTMSG + sizeof( wsw::LogLineStream );
	static constexpr size_t kCapacity = 1024;

	static constexpr size_t kSeveritiesCount = std::size( kLogLineColorForSeverity );
	static constexpr size_t kCategoriesCount = std::size( kLogLinePrefixForCategory );

	wsw::StaticVector<wsw::LogLineStream, kSeveritiesCount * kCategoriesCount> m_nullStreams;
public:
	LogLineStreamsAllocator() : m_allocator( kSize, kCapacity ) {
		// TODO: This is very flaky, but alternatives aren't perfect either...
		for( unsigned i = 0; i < kCategoriesCount; ++i ) {
			assert( std::strlen( kLogLinePrefixForCategory[i] ) == 3 );
			const auto category( ( wsw::LogLineCategory( i ) ) );
			for( unsigned j = 0; j < kSeveritiesCount; ++j ) {
				const auto severity( ( wsw::LogLineSeverity )j );
				new( m_nullStreams.unsafe_grow_back() )wsw::LogLineStream( nullptr, 0, category, severity );
			}
		}
	}

	[[nodiscard]]
	auto nullStreamFor( wsw::LogLineCategory category, wsw::LogLineSeverity severity ) -> wsw::LogLineStream * {
		return std::addressof( m_nullStreams[(unsigned)category * kSeveritiesCount + (unsigned)severity] );
	}

	[[nodiscard]]
	bool isANullStream( wsw::LogLineStream *stream ) {
		return (size_t)( stream - m_nullStreams.data() ) < std::size( m_nullStreams );
	}

	[[nodiscard]]
	auto alloc( wsw::LogLineCategory category, wsw::LogLineSeverity severity ) -> wsw::LogLineStream * {
		[[maybe_unused]] volatile std::lock_guard guard( m_mutex );
		if( !m_allocator.isFull() ) [[likely]] {
			uint8_t *mem = m_allocator.allocOrNull();
			auto *buffer = (char *)( mem + sizeof( wsw::LogLineStream ) );
			return new( mem )wsw::LogLineStream( buffer, MAX_PRINTMSG, category, severity );
		} else if( auto *mem = (uint8_t *)::malloc( kSize ) ) {
			auto *buffer = (char *)( mem + sizeof( wsw::LogLineStream ) );
			return new( mem )wsw::LogLineStream( buffer, MAX_PRINTMSG, category, severity );
		} else {
			return nullStreamFor( category, severity );
		}
	}

	[[nodiscard]]
	auto free( wsw::LogLineStream *stream ) {
		if( !isANullStream( stream ) ) [[likely]] {
			[[maybe_unused]] volatile std::lock_guard guard( m_mutex );
			stream->~LogLineStream();
			if( m_allocator.mayOwn( stream ) ) [[likely]] {
				m_allocator.free( stream );
			} else {
				::free( stream );
			}
		}
	}
};

static LogLineStreamsAllocator logLineStreamsAllocator;

static int log_file = 0;

static int server_state = CA_UNINITIALIZED;
static int client_state = CA_UNINITIALIZED;
static bool demo_playing = false;

struct cmodel_state_s *server_cms = NULL;
unsigned server_map_checksum = 0;

// host_speeds times
int64_t time_before_game;
int64_t time_after_game;
int64_t time_before_ref;
int64_t time_after_ref;

/*
============================================================================

CLIENT / SERVER interactions

============================================================================
*/

static int rd_target;
static char *rd_buffer;
static int rd_buffersize;
static void ( *rd_flush )( int target, const char *buffer, const void *extra );
static const void *rd_extra;

void Com_BeginRedirect( int target, char *buffer, int buffersize,
						void ( *flush )( int, const char*, const void* ), const void *extra ) {
	if( !target || !buffer || !buffersize || !flush ) {
		return;
	}

	QMutex_Lock( com_print_mutex );

	rd_target = target;
	rd_buffer = buffer;
	rd_buffersize = buffersize;
	rd_flush = flush;
	rd_extra = extra;

	*rd_buffer = 0;
}

void Com_EndRedirect( void ) {
	rd_flush( rd_target, rd_buffer, rd_extra );

	rd_target = 0;
	rd_buffer = NULL;
	rd_buffersize = 0;
	rd_flush = NULL;
	rd_extra = NULL;

	QMutex_Unlock( com_print_mutex );
}

void Com_DeferConsoleLogReopen( void ) {
	if( logconsole != NULL ) {
		logconsole->modified = true;
	}
}

static void Com_CloseConsoleLog( bool lock, bool shutdown ) {
	if( shutdown ) {
		lock = true;
	}

	if( lock ) {
		QMutex_Lock( com_print_mutex );
	}

	if( log_file ) {
		FS_FCloseFile( log_file );
		log_file = 0;
	}

	if( shutdown ) {
		logconsole = NULL;
	}

	if( lock ) {
		QMutex_Unlock( com_print_mutex );
	}
}

static void Com_ReopenConsoleLog( void ) {
	char errmsg[MAX_PRINTMSG] = { 0 };

	QMutex_Lock( com_print_mutex );

	Com_CloseConsoleLog( false, false );

	if( logconsole && logconsole->string && logconsole->string[0] ) {
		size_t name_size;
		char *name;

		name_size = strlen( logconsole->string ) + strlen( ".log" ) + 1;
		name = ( char* )Q_malloc( name_size );
		Q_strncpyz( name, logconsole->string, name_size );
		COM_DefaultExtension( name, ".log", name_size );

		if( FS_FOpenFile( name, &log_file, ( logconsole_append && logconsole_append->integer ? FS_APPEND : FS_WRITE ) ) == -1 ) {
			log_file = 0;
			Q_snprintfz( errmsg, MAX_PRINTMSG, "Couldn't open: %s\n", name );
		}

		Q_free( name );
	}

	QMutex_Unlock( com_print_mutex );

	if( errmsg[0] ) {
		Com_Printf( "%s", errmsg );
	}
}

auto wsw::createLogLineStream( wsw::LogLineCategory category, wsw::LogLineSeverity severity ) -> wsw::LogLineStream * {
	if( !com_logCategoryMask || !com_logSeverityMask ) [[unlikely]] {
		wsw::failWithRuntimeError( "Can't use log line streams prior to CVar initialization" );
	}
	if( !( com_logCategoryMask->integer & ( 1 << (unsigned)category ) ) ) {
		return ::logLineStreamsAllocator.nullStreamFor( category, severity );
	}
	if( !( com_logSeverityMask->integer & ( 1 << (unsigned)severity ) ) ) {
		return ::logLineStreamsAllocator.nullStreamFor( category, severity );
	}
	return ::logLineStreamsAllocator.alloc( category, severity );
}

void wsw::submitLogLineStream( LogLineStream *stream ) {
	// It wasn't permitted to be created, a stub was forcefully supplied instead
	if( !( com_logCategoryMask->integer & ( 1 << (unsigned)stream->m_category ) ) ) {
		return;
	}
	if( !( com_logSeverityMask->integer & ( 1 << (unsigned)stream->m_severity ) ) ) {
		return;
	}
	// TODO: Eliminate Com_Printf()
	if( !::logLineStreamsAllocator.isANullStream( stream ) ) {
		stream->m_data[wsw::min( stream->m_limit, stream->m_offset )] = '\0';
		const char *color = kLogLineColorForSeverity[(unsigned)stream->m_severity];
		const char *prefix = kLogLinePrefixForCategory[(unsigned)stream->m_category];
		Com_Printf( "%s[%s] %s\n", color, prefix, stream->m_data );
	} else {
		Com_Printf( S_COLOR_RED "A null line stream was used. The line content was discarded\n" );
	}
	::logLineStreamsAllocator.free( stream );
}

/*
* Com_Printf
*
* Both client and server can use this, and it will output
* to the apropriate place.
*/
void Com_Printf( const char *format, ... ) {
	va_list argptr;
	char msg[MAX_PRINTMSG];

	time_t timestamp;
	char timestamp_str[MAX_PRINTMSG];
	struct tm *timestampptr;
	timestamp = time( NULL );
	timestampptr = gmtime( &timestamp );
	strftime( timestamp_str, MAX_PRINTMSG, "%Y-%m-%dT%H:%M:%SZ ", timestampptr );

	va_start( argptr, format );
	Q_vsnprintfz( msg, sizeof( msg ), format, argptr );
	va_end( argptr );

	QMutex_Lock( com_print_mutex );

	if( rd_target ) {
		if( (int)( strlen( msg ) + strlen( rd_buffer ) ) > ( rd_buffersize - 1 ) ) {
			rd_flush( rd_target, rd_buffer, rd_extra );
			*rd_buffer = 0;
		}
		strcat( rd_buffer, msg );

		QMutex_Unlock( com_print_mutex );
		return;
	}

	// also echo to debugging console
	Sys_ConsoleOutput( msg );

	Con_Print( msg );

	if( log_file ) {
		if( logconsole_timestamp && logconsole_timestamp->integer ) {
			FS_Printf( log_file, "%s", timestamp_str );
		}
		FS_Printf( log_file, "%s", msg );
		if( logconsole_flush && logconsole_flush->integer ) {
			FS_Flush( log_file ); // force it to save every time
		}
	}

	QMutex_Unlock( com_print_mutex );
}


/*
* Com_DPrintf
*
* A Com_Printf that only shows up if the "developer" cvar is set
*/
void Com_DPrintf( const char *format, ... ) {
	va_list argptr;
	char msg[MAX_PRINTMSG];

	if( !developer || !developer->integer ) {
		return; // don't confuse non-developers with techie stuff...

	}
	va_start( argptr, format );
	Q_vsnprintfz( msg, sizeof( msg ), format, argptr );
	va_end( argptr );

	Com_Printf( "%s", msg );
}


/*
* Com_Error
*
* Both client and server can use this, and it will
* do the apropriate things.
*/
void Com_Error( com_error_code_t code, const char *format, ... ) {
	va_list argptr;
	char *msg = com_errormsg;
	const size_t sizeof_msg = sizeof( com_errormsg );
	static bool recursive = false;

	if( recursive ) {
		Com_Printf( "recursive error after: %s", msg ); // wsw : jal : log it
		Sys_Error( "recursive error after: %s", msg );
	}
	recursive = true;

	va_start( argptr, format );
	Q_vsnprintfz( msg, sizeof_msg, format, argptr );
	va_end( argptr );

	if( code == ERR_DROP ) {
		Com_Printf( "********************\nERROR: %s\n********************\n", msg );
		SV_ShutdownGame( va( "Server crashed: %s\n", msg ), false );
		CL_Disconnect( msg );
		recursive = false;
		longjmp( abortframe, -1 );
	} else {
		Com_Printf( "********************\nERROR: %s\n********************\n", msg );
		SV_Shutdown( va( "Server fatal crashed: %s\n", msg ) );
		CL_Shutdown();
		MM_Shutdown();
	}

	if( log_file ) {
		FS_FCloseFile( log_file );
		log_file = 0;
	}

	Sys_Error( "%s", msg );
}

/*
* Com_DeferQuit
*/
void Com_DeferQuit( void ) {
	com_quit = true;
}

/*
* Com_Quit
*
* Both client and server can use this, and it will
* do the apropriate things.
*/
void Com_Quit( void ) {
	SV_Shutdown( "Server quit\n" );
	CL_Shutdown();
	MM_Shutdown();

	Sys_Quit();
}


/*
* Com_ServerState
*/
int Com_ServerState( void ) {
	return server_state;
}

/*
* Com_SetServerState
*/
void Com_SetServerState( int state ) {
	server_state = state;
}

/*
* Com_ServerCM
*/
struct cmodel_state_s *Com_ServerCM( unsigned *checksum ) {
	*checksum = server_map_checksum;
	return server_cms;
}

/*
* Com_SetServerCM
*/
void Com_SetServerCM( struct cmodel_state_s *cms, unsigned checksum ) {
	server_cms = cms;
	server_map_checksum = checksum;
}

int Com_ClientState( void ) {
	return client_state;
}

void Com_SetClientState( int state ) {
	client_state = state;
}

bool Com_DemoPlaying( void ) {
	return demo_playing;
}

void Com_SetDemoPlaying( bool state ) {
	demo_playing = state;
}

unsigned int Com_DaysSince1900( void ) {
	time_t long_time;
	struct tm *newtime;

	// get date from system
	time( &long_time );
	newtime = localtime( &long_time );

	return ( newtime->tm_year * 365 ) + newtime->tm_yday;
}

//============================================================================

/*
* COM_CheckParm
*
* Returns the position (1 to argc-1) in the program's argument list
* where the given parameter apears, or 0 if not present
*/
int COM_CheckParm( char *parm ) {
	int i;

	for( i = 1; i < com_argc; i++ ) {
		if( !strcmp( parm, com_argv[i] ) ) {
			return i;
		}
	}

	return 0;
}

int COM_Argc( void ) {
	return com_argc;
}

const char *COM_Argv( int arg ) {
	if( arg < 0 || arg >= com_argc || !com_argv[arg] ) {
		return "";
	}
	return com_argv[arg];
}

void COM_ClearArgv( int arg ) {
	if( arg < 0 || arg >= com_argc || !com_argv[arg] ) {
		return;
	}
	com_argv[arg][0] = '\0';
}


/*
* COM_InitArgv
*/
void COM_InitArgv( int argc, char **argv ) {
	int i;

	if( argc > MAX_NUM_ARGVS ) {
		Com_Error( ERR_FATAL, "argc > MAX_NUM_ARGVS" );
	}
	com_argc = argc;
	for( i = 0; i < argc; i++ ) {
		if( !argv[i] || strlen( argv[i] ) >= MAX_TOKEN_CHARS ) {
			com_argv[i][0] = '\0';
		} else {
			com_argv[i] = argv[i];
		}
	}
}

/*
* COM_AddParm
*
* Adds the given string at the end of the current argument list
*/
void COM_AddParm( char *parm ) {
	if( com_argc == MAX_NUM_ARGVS ) {
		Com_Error( ERR_FATAL, "COM_AddParm: MAX_NUM_ARGVS" );
	}
	com_argv[com_argc++] = parm;
}

int Com_GlobMatch( const char *pattern, const char *text, const bool casecmp ) {
	return glob_match( pattern, text, casecmp );
}

void Info_Print( char *s ) {
	char key[512];
	char value[512];
	char *o;
	int l;

	if( *s == '\\' ) {
		s++;
	}
	while( *s ) {
		o = key;
		while( *s && *s != '\\' )
			*o++ = *s++;

		l = o - key;
		if( l < 20 ) {
			memset( o, ' ', 20 - l );
			key[20] = 0;
		} else {
			*o = 0;
		}
		Com_Printf( "%s", key );

		if( !*s ) {
			Com_Printf( "MISSING VALUE\n" );
			return;
		}

		o = value;
		s++;
		while( *s && *s != '\\' )
			*o++ = *s++;
		*o = 0;

		if( *s ) {
			s++;
		}
		Com_Printf( "%s\n", value );
	}
}

//============================================================================

/*
* Com_AddPurePakFile
*/
void Com_AddPakToPureList( purelist_t **purelist, const char *pakname, const unsigned checksum ) {
	purelist_t *purefile;
	const size_t len = strlen( pakname ) + 1;

	purefile = ( purelist_t* )Q_malloc( sizeof( purelist_t ) + len );
	purefile->filename = ( char * )( ( uint8_t * )purefile + sizeof( *purefile ) );
	memcpy( purefile->filename, pakname, len );
	purefile->checksum = checksum;
	purefile->next = *purelist;
	*purelist = purefile;
}

/*
* Com_CountPureListFiles
*/
unsigned Com_CountPureListFiles( purelist_t *purelist ) {
	unsigned numpure;
	purelist_t *iter;

	numpure = 0;
	iter = purelist;
	while( iter ) {
		numpure++;
		iter = iter->next;
	}

	return numpure;
}

/*
* Com_FindPakInPureList
*/
purelist_t *Com_FindPakInPureList( purelist_t *purelist, const char *pakname ) {
	purelist_t *purefile = purelist;

	while( purefile ) {
		if( !strcmp( purefile->filename, pakname ) ) {
			break;
		}
		purefile = purefile->next;
	}

	return purefile;
}

/*
* Com_FreePureList
*/
void Com_FreePureList( purelist_t **purelist ) {
	purelist_t *purefile = *purelist;

	while( purefile ) {
		purelist_t *next = purefile->next;
		Q_free( purefile );
		purefile = next;
	}

	*purelist = NULL;
}

//============================================================================

/**
 * Keeping all this stuff in a single object in a single file
 * feels much better than scattering it over platform files
 * that should be included for compilation via CMake.
 */
class SystemFeaturesHolder {
	friend void Qcommon_Init( int argc, char **argv );

	unsigned processorFeatures { 0 };
	unsigned physicalProcessorsNumber { 0 };
	unsigned logicalProcessorsNumber { 0 };
	bool initialized { false };

	void EnsureInitialized();
	unsigned TestProcessorFeatures();
	void TestNumberOfProcessors( unsigned *physical, unsigned *logical );

#ifdef __linux__
	bool TestUsingLscpu( unsigned *physical, unsigned *logical );
	static const char *SkipWhiteSpace( const char *p );
	static const char *StringForKey( const char *key, const char *line );
	static double NumberForKey( const char *key, const char *line );
#endif

#ifdef _WIN32
	void TestUsingLogicalProcessorInformation( unsigned *physical, unsigned *logical );
#endif

public:
	unsigned GetProcessorFeatures();
	bool GetNumberOfProcessors( unsigned *physical, unsigned *logical );
};

static SystemFeaturesHolder systemFeaturesHolder;

//========================================================

void Key_Init( void );
void Key_Shutdown( void );
void SCR_EndLoadingPlaque( void );

/*
* Com_Error_f
*
* Just throw a fatal error to
* test error shutdown procedures
*/
#ifndef PUBLIC_BUILD
static void Com_Error_f( void ) {
	Com_Error( ERR_FATAL, "%s", Cmd_Argv( 1 ) );
}
#endif

/*
* Com_Lag_f
*/
#ifndef PUBLIC_BUILD
static void Com_Lag_f( void ) {
	int msecs;

	if( Cmd_Argc() != 2 || atoi( Cmd_Argv( 1 ) ) <= 0 ) {
		Com_Printf( "Usage: %s <milliseconds>\n", Cmd_Argv( 0 ) );
	}

	msecs = atoi( Cmd_Argv( 1 ) );
	Sys_Sleep( msecs );
	Com_Printf( "Lagged %i milliseconds\n", msecs );
}
#endif

void *Q_malloc( size_t size ) {
	// TODO: Ensure 16-byte alignment
	// Zero memory as lots of old stuff rely on the old mempool behaviour
	void *buf = std::calloc( size, 1 );

	if( !buf ) {
		wsw::failWithBadAlloc();
	}

	return buf;
}

void *Q_realloc( void *buf, size_t newsize ) {
	void *newbuf = realloc( buf, newsize );

	if( !newbuf && newsize ) {
		wsw::failWithBadAlloc();
	}

	// TODO: Zero memory too? There's no portable way of doing that

	return newbuf;
}

void Q_free( void *buf ) {
	std::free( buf );
}

char *Q_strdup( const char *str ) {
	auto len = std::strlen( str );
	auto *result = (char *)Q_malloc( len + 1 );
	std::memcpy( result, str, len + 1 );
	return result;
}

/*
* Qcommon_InitCommands
*/
void Qcommon_InitCommands( void ) {
	assert( !commands_intialized );

#ifndef PUBLIC_BUILD
	Cmd_AddCommand( "error", Com_Error_f );
	Cmd_AddCommand( "lag", Com_Lag_f );
#endif

	if( dedicated->integer ) {
		Cmd_AddCommand( "quit", Com_Quit );
	}

	commands_intialized = true;
}

/*
* Qcommon_ShutdownCommands
*/
void Qcommon_ShutdownCommands( void ) {
	if( !commands_intialized ) {
		return;
	}

#ifndef PUBLIC_BUILD
	Cmd_RemoveCommand( "error" );
	Cmd_RemoveCommand( "lag" );
#endif

	if( dedicated->integer ) {
		Cmd_RemoveCommand( "quit" );
	}

	commands_intialized = false;
}

/*
* Qcommon_Init
*/
void Qcommon_Init( int argc, char **argv ) {
	(void)std::setlocale( LC_ALL, "C" );

	if( setjmp( abortframe ) ) {
		Sys_Error( "Error during initialization: %s", com_errormsg );
	}

	// reset hooks to malloc and free
	cJSON_InitHooks( NULL );

	QThreads_Init();

	com_print_mutex = QMutex_Create();

	// Force doing this early as this could fork for executing shell commands on UNIX.
	// Required being able to call Com_Printf().
	systemFeaturesHolder.EnsureInitialized();

	// prepare enough of the subsystems to handle
	// cvar and command buffer management
	COM_InitArgv( argc, argv );

	Cbuf_Init();

	// initialize cmd/cvar tries
	Cmd_PreInit();
	Cvar_PreInit();

	// create basic commands and cvars
	Cmd_Init();
	Cvar_Init();

	wswcurl_init();

	Key_Init();

	// we need to add the early commands twice, because
	// a basepath or cdpath needs to be set before execing
	// config files, but we want other parms to override
	// the settings of the config files
	Cbuf_AddEarlyCommands( false );
	Cbuf_Execute();

#ifdef DEDICATED_ONLY
	dedicated =     Cvar_Get( "dedicated", "1", CVAR_NOSET );
	Cvar_ForceSet( "dedicated", "1" );
#else
	dedicated =     Cvar_Get( "dedicated", "0", CVAR_NOSET );
#endif
	developer =     Cvar_Get( "developer", "0", 0 );

	if( developer->integer ) {
		com_logCategoryMask = Cvar_Get( "com_logCategoryMask", "-1", 0 );
		com_logSeverityMask = Cvar_Get( "com_logSeverityMask", "-1", 0 );
	} else {
		com_logCategoryMask = Cvar_Get( "com_logCategoryMask", "-1", CVAR_NOSET );
		com_logSeverityMask = Cvar_Get( "com_logSeverityMask", "14", CVAR_NOSET );
	}

	Com_LoadCompressionLibraries();

	FS_Init();

	Cbuf_AddText( "exec default.cfg\n" );
	if( !dedicated->integer ) {
		Cbuf_AddText( "exec config.cfg\n" );
		Cbuf_AddText( "exec autoexec.cfg\n" );
	} else {
		Cbuf_AddText( "exec dedicated_autoexec.cfg\n" );
	}

	Cbuf_AddEarlyCommands( true );
	Cbuf_Execute();

	//
	// init commands and vars
	//
	Qcommon_InitCommands();

	host_speeds =       Cvar_Get( "host_speeds", "0", 0 );
	timescale =     Cvar_Get( "timescale", "1.0", CVAR_CHEAT );
	fixedtime =     Cvar_Get( "fixedtime", "0", CVAR_CHEAT );
	if( dedicated->integer ) {
		logconsole =        Cvar_Get( "logconsole", "wswconsole.log", CVAR_ARCHIVE );
	} else {
		logconsole =        Cvar_Get( "logconsole", "", CVAR_ARCHIVE );
	}
	logconsole_append = Cvar_Get( "logconsole_append", "1", CVAR_ARCHIVE );
	logconsole_flush =  Cvar_Get( "logconsole_flush", "0", CVAR_ARCHIVE );
	logconsole_timestamp =  Cvar_Get( "logconsole_timestamp", "0", CVAR_ARCHIVE );

	com_introPlayed3 =   Cvar_Get( "com_introPlayed3", "0", CVAR_ARCHIVE );

	Cvar_Get( "gamename", APPLICATION, CVAR_READONLY );
	versioncvar = Cvar_Get( "version", APP_VERSION_STR " " CPUSTRING " " __DATE__ " " BUILDSTRING, CVAR_SERVERINFO | CVAR_READONLY );

	Sys_Init();

	NET_Init();
	Netchan_Init();

	CM_Init();

#if APP_STEAMID
	Steam_LoadLibrary();
#endif

	MM_Init();

	SV_Init();
	CL_Init();

	SCR_EndLoadingPlaque();

	if( !dedicated->integer ) {
		Cbuf_AddText( "exec autoexec_postinit.cfg\n" );
	} else {
		Cbuf_AddText( "exec dedicated_autoexec_postinit.cfg\n" );
	}

	// add + commands from command line
	if( !Cbuf_AddLateCommands() ) {
		// if the user didn't give any commands, run default action

		if( !dedicated->integer ) {
			// only play the introduction sequence once
			if( !com_introPlayed3->integer ) {
				Cvar_ForceSet( com_introPlayed3->name, "1" );
#if ( !defined( __ANDROID__ ) || defined ( __i386__ ) || defined ( __x86_64__ ) )
				Cbuf_AddText( "cinematic intro.roq\n" );
#endif
			}
		}
	} else {
		// the user asked for something explicit
		// so drop the loading plaque
		SCR_EndLoadingPlaque();
	}

	Com_Printf( "\n====== %s Initialized ======\n", APPLICATION );

	Cbuf_Execute();
}

/*
* Qcommon_Frame
*/
void Qcommon_Frame( unsigned int realMsec ) {
	char *s;
	int time_before = 0, time_between = 0, time_after = 0;
	static unsigned int gameMsec;

	if( com_quit ) {
		Com_Quit();
	}

	if( setjmp( abortframe ) ) {
		return; // an ERR_DROP was thrown

	}

	if( logconsole && logconsole->modified ) {
		logconsole->modified = false;
		Com_ReopenConsoleLog();
	}

	if( fixedtime->integer > 0 ) {
		gameMsec = fixedtime->integer;
	} else if( timescale->value >= 0 ) {
		static float extratime = 0.0f;
		gameMsec = extratime + (float)realMsec * timescale->value;
		extratime = ( extratime + (float)realMsec * timescale->value ) - (float)gameMsec;
	} else {
		gameMsec = realMsec;
	}

	wswcurl_perform();

	FS_Frame();

	Steam_RunFrame();

	if( dedicated->integer ) {
		do {
			s = Sys_ConsoleInput();
			if( s ) {
				Cbuf_AddText( va( "%s\n", s ) );
			}
		} while( s );

		Cbuf_Execute();
	}

	// keep the random time dependent
	rand();

	if( host_speeds->integer ) {
		time_before = Sys_Milliseconds();
	}

	SV_Frame( realMsec, gameMsec );

	if( host_speeds->integer ) {
		time_between = Sys_Milliseconds();
	}

	CL_Frame( realMsec, gameMsec );

	if( host_speeds->integer ) {
		time_after = Sys_Milliseconds();
	}

	if( host_speeds->integer ) {
		int all, sv, gm, cl, rf;

		all = time_after - time_before;
		sv = time_between - time_before;
		cl = time_after - time_between;
		gm = time_after_game - time_before_game;
		rf = time_after_ref - time_before_ref;
		sv -= gm;
		cl -= rf;
		Com_Printf( "all:%3i sv:%3i gm:%3i cl:%3i rf:%3i\n",
					all, sv, gm, cl, rf );
	}

	MM_Frame( realMsec );
}

/*
* Qcommon_Shutdown
*/
void Qcommon_Shutdown( void ) {
	static bool isdown = false;

	if( isdown ) {
		printf( "Recursive shutdown\n" );
		return;
	}
	isdown = true;

	CM_Shutdown();
	Netchan_Shutdown();
	NET_Shutdown();
	Key_Shutdown();

	Steam_UnloadLibrary();

	Qcommon_ShutdownCommands();

	Com_CloseConsoleLog( true, true );

	FS_Shutdown();

	Com_UnloadCompressionLibraries();

	wswcurl_cleanup();

	Cvar_Shutdown();
	Cmd_Shutdown();
	Cbuf_Shutdown();

	QMutex_Destroy( &com_print_mutex );

	QThreads_Shutdown();
}

unsigned Sys_GetProcessorFeatures() {
	return ::systemFeaturesHolder.GetProcessorFeatures();
}

bool Sys_GetNumberOfProcessors( unsigned *physical, unsigned *logical ) {
	return ::systemFeaturesHolder.GetNumberOfProcessors( physical, logical );
}

void SystemFeaturesHolder::EnsureInitialized() {
	if( initialized ) {
		return;
	}

	processorFeatures = TestProcessorFeatures();
	TestNumberOfProcessors( &physicalProcessorsNumber, &logicalProcessorsNumber );
	initialized = true;
}

bool SystemFeaturesHolder::GetNumberOfProcessors( unsigned *physical, unsigned *logical ) {
	EnsureInitialized();
	if( !physicalProcessorsNumber || !logicalProcessorsNumber ) {
		*physical = 1;
		*logical = 1;
		return false;
	}

	*physical = physicalProcessorsNumber;
	*logical = logicalProcessorsNumber;
	return true;
}

unsigned SystemFeaturesHolder::GetProcessorFeatures() {
	EnsureInitialized();
	return processorFeatures;
}

unsigned SystemFeaturesHolder::TestProcessorFeatures() {
	unsigned features = 0;

#if ( defined ( __i386__ ) || defined ( __x86_64__ ) || defined( _M_IX86 ) || defined( _M_AMD64 ) || defined( _M_X64 ) )
#ifdef _MSC_VER
	int cpuInfo[4];
	__cpuid( cpuInfo, 0 );
	// Check whether CPUID is supported at all (is it relevant nowadays?)
	if( cpuInfo[0] == 0 ) {
		return 0;
	}
	// Get standard feature bits (look for description here https://en.wikipedia.org/wiki/CPUID)
	__cpuid( cpuInfo, 1 );
	const int ECX = cpuInfo[2];
	const int EDX = cpuInfo[3];
	if( ECX & ( 1 << 28 ) ) {
		features |= Q_CPU_FEATURE_AVX;
	} else if( ECX & ( 1 << 20 ) ) {
		features |= Q_CPU_FEATURE_SSE42;
	} else if( ECX & ( 1 << 19 ) ) {
		features |= Q_CPU_FEATURE_SSE41;
	} else if( EDX & ( 1 << 26 ) ) {
		features |= Q_CPU_FEATURE_SSE2;
	}
#else // not MSVC
#ifndef __clang__
	// Clang does not even have this intrinsic, executables work fine without it.
	__builtin_cpu_init();
#endif // clang-specific code
	if( __builtin_cpu_supports( "avx" ) ) {
		features |= Q_CPU_FEATURE_AVX;
	} else if( __builtin_cpu_supports( "sse4.2" ) ) {
		features |= Q_CPU_FEATURE_SSE42;
	} else if( __builtin_cpu_supports( "sse4.1" ) ) {
		features |= Q_CPU_FEATURE_SSE41;
	} else if( __builtin_cpu_supports( "sse2" ) ) {
		features |= Q_CPU_FEATURE_SSE2;
	}
#endif // gcc/clang - specific code
#endif // x86-specific code

	// We have only set the most significant feature bit for code clarity.
	// Since this bit implies all least-significant bits presence, set these bits
	if( features ) {
		// Check whether it's a power of 2
		assert( !( features & ( features - 1 ) ) );
		features |= ( features - 1 );
	}

	return features;
}

void SystemFeaturesHolder::TestNumberOfProcessors( unsigned *physical, unsigned *logical ) {
	*physical = 0;
	*logical = 0;

#ifdef __linux__
	if( TestUsingLscpu( physical, logical ) ) {
		return;
	}

	Com_Printf( S_COLOR_YELLOW "Warning: `lscpu` command can't be executed. Falling back to inferior methods\n" );

	// This is quite poor.
	// We hope that `lscpu` works for the most client installations
	long confValue = sysconf( _SC_NPROCESSORS_ONLN );
	// Sanity/error checks
	Q_clamp( confValue, 1, 256 );
	*physical = (unsigned)confValue;
	*logical = (unsigned)confValue;
#endif

#ifdef _WIN32
	TestUsingLogicalProcessorInformation( physical, logical );
#endif

#ifdef __APPLE__
	// They should not get changed but lets ensure no surprises
	size_t len1 = sizeof( *physical ), len2 = sizeof( *logical );
	// Never fails if parameters are correct
	assert( !sysctlbyname( "hw.physicalcpu", physical, &len1, nullptr, 0 ) );
	assert( !sysctlbyname( "hw.logicalcpu", logical, &len2, nullptr, 0 ) );
#endif
}

#ifdef __linux__

/**
 * An utility to read lines from stdout of a spawned command
 */
class ProcessPipeReader {
	FILE *fp;
public:
	explicit ProcessPipeReader( const char *command ) {
		fp = ::popen( command, "r" );
	}

	~ProcessPipeReader() {
		if( fp ) {
			(void)::pclose( fp );
		}
	}

	char *ReadNext( char *buffer, size_t bufferSize ) {
		if( !fp || ::feof( fp ) ) {
			return nullptr;
		}

		assert( bufferSize <= (size_t)std::numeric_limits<int>::max() );
		char *result = fgets( buffer, (int)bufferSize, fp );
		if( !result && ::ferror( fp ) ) {
			(void)pclose( fp );
			fp = nullptr;
		}
		return result;
	}
};

const char *SystemFeaturesHolder::SkipWhiteSpace( const char *p ) {
	while( ::isspace( *p ) ) {
		p++;
	}
	return p;
}

const char *SystemFeaturesHolder::StringForKey( const char *key, const char *line ) {
	// Skip a whitespace before line contents
	const char *p = SkipWhiteSpace( line );
	if( !*p ) {
		return nullptr;
	}
	// Skip a whitespace before key characters (ignoring this could lead to hard-to-find bugs)
	const char *k = SkipWhiteSpace( key );
	if( !*k ) {
		return nullptr;
	}

	// Match a line part by the key
	while( *k && ( ::tolower( *k ) == ::tolower( *p ) ) ) {
		k++, p++;
	}

	// If there is an unmatched key part
	if( *k ) {
		return nullptr;
	}

	// Skip a whitespace before the separating colon
	p = SkipWhiteSpace( p );
	if( *p++ != ':' ) {
		return nullptr;
	}

	// Skip a whitespace before contents
	return SkipWhiteSpace( p );
}

double SystemFeaturesHolder::NumberForKey( const char *key, const char *line ) {
	if( const char *s = StringForKey( key, line ) ) {
		char *endptr;
		long value = strtol( s, &endptr, 10 );
		if( !*endptr || ::isspace( *endptr ) ) {
			static_assert( sizeof( long ) >= sizeof( int ), "incorrect strtol() result checks" );
			const auto min = std::numeric_limits<int>::min();
			const auto max = std::numeric_limits<int>::max();
			if( value >= min && value <= max ) {
				return value;
			}
		}
	}
	return std::numeric_limits<double>::quiet_NaN();
}

bool SystemFeaturesHolder::TestUsingLscpu( unsigned *physical, unsigned *logical ) {
	// We could try parsing "/proc/cpuinfo" but it is really complicated.
	// This utility provides much more convenient output and IPC details is hidden anyway.
	ProcessPipeReader reader( "lscpu" );
	char buffer[3072];

	unsigned cpus = 0;
	unsigned threadsPerCore = 0;
	double n;
	while( !cpus || !threadsPerCore ) {
		const char *line = reader.ReadNext( buffer, sizeof( buffer ) );
		if( !line ) {
			return false;
		}
		if( !cpus ) {
			n = NumberForKey( "CPU(s)", line );
			if( !std::isnan( n ) && n > 0 ) {
				cpus = (unsigned)n;
			}
		}
		if( !threadsPerCore ) {
			n = NumberForKey( "Thread(s) per core", line );
			if( !std::isnan( n ) && n > 0 ) {
				threadsPerCore = (unsigned)n;
			}
		}
	}

	if( cpus % threadsPerCore ) {
		Com_Printf( S_COLOR_YELLOW "Weird number of CPU(s) for threads(s) per core: %d for %d", cpus, threadsPerCore );
		return false;
	}

	*physical = cpus / threadsPerCore;
	*logical = cpus;
	return true;
}

#endif

#ifdef _WIN32
void SystemFeaturesHolder::TestUsingLogicalProcessorInformation( unsigned *physical, unsigned *logical ) {
	assert( !*physical );
	assert( !*logical );

	DWORD bufferLen;
	::GetLogicalProcessorInformation( nullptr, &bufferLen );
	if( ::GetLastError() != ERROR_INSUFFICIENT_BUFFER ) {
		return;
	}

	auto *const buffer = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)::malloc( bufferLen );
	if( !::GetLogicalProcessorInformation( buffer, &bufferLen ) ) {
		::free( buffer );
		return;
	}

	for( int i = 0; i < bufferLen / sizeof( *buffer ); ++i ) {
		if( buffer[i].Relationship != RelationProcessorCore ) {
			continue;
		}
		( *physical )++;

		const ULONG_PTR processorMask = buffer[i].ProcessorMask;
		static_assert( sizeof( processorMask ) == sizeof( ptrdiff_t ), "" );
		// We can't rely on popcnt instruction support
		for( uint64_t j = 0, bitMask = 1; j < sizeof( ptrdiff_t ) * 8; ++j, bitMask <<= 1 ) {
			if( processorMask & bitMask ) {
				( *logical )++;
			}
		}
	}

	::free( buffer );
}
#endif