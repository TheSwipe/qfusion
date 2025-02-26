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

#include "snd_local.h"
#include "snd_env_sampler.h"
#include "../client/client.h"
#include "../qcommon/singletonholder.h"
#include "alsystemfacade.h"

class NullSoundSystem : public SoundSystem {
public:
	explicit NullSoundSystem( client_state_s *client_ ) : SoundSystem( client_ ) {}

	void deleteSelf( bool ) override;

	void postInit() override {}

	void beginRegistration() override {}
	void endRegistration() override {}

	void stopAllSounds( unsigned ) override {}

	void clear() override {}
	void updateListener( const float *, const float *, const mat3_t ) override {}
	void activate( bool ) override {}

	void processFrameUpdates() override {}

	void setEntitySpatialParams( int, const float *, const float * ) override {};

	[[nodiscard]]
	auto registerSound( const char * ) -> sfx_s * override { return nullptr; }

	void startFixedSound( sfx_s *, const float *, int, float, float ) override {}
	void startRelativeSound( sfx_s *, int, int, float, float ) override {}
	void startGlobalSound( sfx_s *, int, float ) override {}
	void startLocalSound( const char *, float ) override {}
	void startLocalSound( sfx_s *, float ) override {}
	void addLoopSound( sfx_s *, int, uintptr_t, float, float ) override {}

	void startBackgroundTrack( const char *, const char *, int ) override {}
	void stopBackgroundTrack() override {}
	void nextBackgroundTrack() override {}
	void prevBackgroundTrack() override {}
	void pauseBackgroundTrack() override {}
};

static SingletonHolder<NullSoundSystem> nullSoundSystemHolder;

void NullSoundSystem::deleteSelf( bool ) {
	::nullSoundSystemHolder.shutdown();
}

cvar_t *s_volume;
cvar_t *s_musicvolume;

cvar_t *s_doppler;
cvar_t *s_sound_velocity;
cvar_t *s_environment_effects;
cvar_t *s_environment_sampling_quality;
cvar_t *s_effects_number_threshold;
cvar_t *s_hrtf;
cvar_t *s_stereo2mono;
cvar_t *s_globalfocus;

static void SF_Music_f() {
	if( Cmd_Argc() == 2 ) {
		SoundSystem::instance()->startBackgroundTrack( Cmd_Argv( 1 ), Cmd_Argv( 1 ), 0 );
	} else if( Cmd_Argc() == 3 ) {
		SoundSystem::instance()->startBackgroundTrack( Cmd_Argv( 1 ), Cmd_Argv( 2 ), 0 );
	} else {
		Com_Printf( "music <intro|playlist> [loop|shuffle]\n" );
		return;
	}
}

static void SF_StopBackgroundTrack() {
	SoundSystem::instance()->stopBackgroundTrack();
}

static void SF_PrevBackgroundTrack() {
	SoundSystem::instance()->prevBackgroundTrack();
}

static void SF_NextBackgroundTrack() {
	SoundSystem::instance()->nextBackgroundTrack();
}

static void SF_PauseBackgroundTrack() {
	SoundSystem::instance()->pauseBackgroundTrack();
}

bool SoundSystem::init( client_state_t *client, const InitOptions &options ) {
	s_volume         = Cvar_Get( "s_volume", "0.8", CVAR_ARCHIVE );
	s_musicvolume    = Cvar_Get( "s_musicvolume", "0.05", CVAR_ARCHIVE );
	s_doppler        = Cvar_Get( "s_doppler", "1.0", CVAR_ARCHIVE );
	s_sound_velocity = Cvar_Get( "s_sound_velocity", "8500", CVAR_DEVELOPER );
	s_stereo2mono    = Cvar_Get( "s_stereo2mono", "0", CVAR_ARCHIVE );
	s_globalfocus    = Cvar_Get( "s_globalfocus", "0", CVAR_ARCHIVE );

	s_environment_effects          = Cvar_Get( "s_environment_effects", "1", CVAR_ARCHIVE | CVAR_LATCH_SOUND );
	s_environment_sampling_quality = Cvar_Get( "s_environment_sampling_quality", "0.5", CVAR_ARCHIVE );
	s_effects_number_threshold     = Cvar_Get( "s_effects_number_threshold", "15", CVAR_ARCHIVE );
	s_hrtf                         = Cvar_Get( "s_hrtf", "1", CVAR_ARCHIVE | CVAR_LATCH_SOUND );

	Cmd_AddCommand( "music", SF_Music_f );
	Cmd_AddCommand( "stopmusic", SF_StopBackgroundTrack );
	Cmd_AddCommand( "prevmusic", SF_PrevBackgroundTrack );
	Cmd_AddCommand( "nextmusic", SF_NextBackgroundTrack );
	Cmd_AddCommand( "pausemusic", SF_PauseBackgroundTrack );

	if( !options.useNullSystem ) {
		s_instance = wsw::snd::ALSoundSystem::tryCreate( client, options.verbose );
		if( s_instance ) {
			s_instance->postInit();
			return true;
		}
	}

	::nullSoundSystemHolder.init( client );
	s_instance = nullSoundSystemHolder.instance();
	s_instance->postInit();
	return options.useNullSystem;
}

void SoundSystem::shutdown( bool verbose ) {
	Cmd_RemoveCommand( "music" );
	Cmd_RemoveCommand( "stopmusic" );
	Cmd_RemoveCommand( "prevmusic" );
	Cmd_RemoveCommand( "nextmusic" );
	Cmd_RemoveCommand( "pausemusic" );

	if( s_instance ) {
		s_instance->deleteSelf( verbose );
		s_instance = nullptr;
	}
}

ALuint S_SoundFormat( int width, int channels ) {
	if( width == 1 ) {
		if( channels == 1 ) {
			return AL_FORMAT_MONO8;
		} else if( channels == 2 ) {
			return AL_FORMAT_STEREO8;
		}
	} else if( width == 2 ) {
		if( channels == 1 ) {
			return AL_FORMAT_MONO16;
		} else if( channels == 2 ) {
			return AL_FORMAT_STEREO16;
		}
	}

	Com_Printf( "Unknown sound format: %i channels, %i bits.\n", channels, width * 8 );
	return AL_FORMAT_MONO16;
}

/*
* S_GetBufferLength
*
* Returns buffer length expressed in milliseconds
*/
ALuint S_GetBufferLength( ALuint buffer ) {
	ALint size, bits, channels, freq;

	alGetBufferi( buffer, AL_SIZE, &size );
	alGetBufferi( buffer, AL_BITS, &bits );
	alGetBufferi( buffer, AL_FREQUENCY, &freq );
	alGetBufferi( buffer, AL_CHANNELS, &channels );

	if( alGetError() != AL_NO_ERROR ) {
		return 0;
	}
	return (ALuint)( (ALfloat)( size / ( bits / 8 ) / channels ) * 1000.0 / freq + 0.5f );
}

const char *S_ErrorMessage( ALenum error ) {
	switch( error ) {
		case AL_NO_ERROR:
			return "No error";
		case AL_INVALID_NAME:
			return "Invalid name";
		case AL_INVALID_ENUM:
			return "Invalid enumerator";
		case AL_INVALID_VALUE:
			return "Invalid value";
		case AL_INVALID_OPERATION:
			return "Invalid operation";
		case AL_OUT_OF_MEMORY:
			return "Out of memory";
		default:
			return "Unknown error";
	}
}

void S_Trace( trace_t *tr, const vec3_t start,
			  const vec3_t end, const vec3_t mins,
			  const vec3_t maxs, int mask, int topNodeHint ) {
	if( const auto *cms = SoundSystem::instance()->getClient()->cms ) {
		CM_TransformedBoxTrace( cms, tr, start, end, mins, maxs, nullptr, mask, nullptr, nullptr, topNodeHint );
		return;
	}

	::memset( tr, 0, sizeof( trace_t ) );
	tr->fraction = 1.0f;
}

wsw::StringView S_ShaderrefName( int shaderNum ) {
	const char *s = nullptr;
	if( const auto *cms = SoundSystem::instance()->getClient()->cms ) {
		s = CM_ShaderrefName( cms, shaderNum );
	}
	// Should really always return a valid name ref
	return s ? wsw::StringView( s ) : wsw::StringView();
}

int S_PointContents( const float *p, int topNodeHint ) {
	if( const auto *cms = SoundSystem::instance()->getClient()->cms ) {
		return CM_TransformedPointContents( cms, p, nullptr, nullptr, nullptr, topNodeHint );
	}
	return 0;
}

int S_PointLeafNum( const vec3_t p, int topNodeHint ) {
	if( const auto *cms = SoundSystem::instance()->getClient()->cms ) {
		return CM_PointLeafnum( cms, p, topNodeHint );
	}
	return 0;
}

int S_NumLeafs() {
	if( const auto *cms = SoundSystem::instance()->getClient()->cms ) {
		return CM_NumLeafs( cms );
	}
	return 0;
}

const vec3_t *S_GetLeafBounds( int leafnum ) {
	if( const auto *cms = SoundSystem::instance()->getClient()->cms ) {
		return CM_GetLeafBounds( cms, leafnum );
	}
	return nullptr;
}

bool S_LeafsInPVS( int leafNum1, int leafNum2 ) {
	if( const auto *cms = SoundSystem::instance()->getClient()->cms ) {
		return ( leafNum1 == leafNum2 ) || CM_LeafsInPVS( cms, leafNum1, leafNum2 );
	}
	return true;
}

int S_FindTopNodeForBox( const vec3_t mins, const vec3_t maxs ) {
	if( const auto *cms = SoundSystem::instance()->getClient()->cms ) {
		return CM_FindTopNodeForBox( cms, mins, maxs );
	}
	return 0;
}

int S_FindTopNodeForSphere( const vec3_t center, float radius ) {
	if( const auto *cms = SoundSystem::instance()->getClient()->cms ) {
		return CM_FindTopNodeForSphere( cms, center, radius );
	}
	return 0;
}

const char *S_GetConfigString( int index ) {
	return cl.configStrings.get( index ).value_or( wsw::StringView() ).data();
}