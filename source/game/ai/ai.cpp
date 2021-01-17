#include "bot.h"
#include "manager.h"
#include "groundtracecache.h"
#include "teamplay/objectivebasedteam.h"
#include "combat/tacticalspotsregistry.h"
#include "frameentitiescache.h"

const cvar_t *ai_evolution;
const cvar_t *ai_debugOutput;
const cvar_t *ai_shareRoutingCache;

ai_weapon_aim_type BuiltinWeaponAimType( int builtinWeapon, int fireMode ) {
	assert( fireMode == FIRE_MODE_STRONG || fireMode == FIRE_MODE_WEAK );
	switch( builtinWeapon ) {
		case WEAP_GUNBLADE:
			// This is not logically correct but produces better behaviour
			// than using AI_WEAPON_AIM_TYPE_PREDICTION_EXPLOSIVE.
			// Otherwise bots carrying only GB are an easy prey losing many chances for attacking.
			// TODO: Introduce "melee" aim type for GB blade attack
			return AI_WEAPON_AIM_TYPE_PREDICTION;
		case WEAP_ROCKETLAUNCHER:
			return AI_WEAPON_AIM_TYPE_PREDICTION_EXPLOSIVE;
		case WEAP_GRENADELAUNCHER:
			return AI_WEAPON_AIM_TYPE_DROP;
		case WEAP_PLASMAGUN:
		case WEAP_SHOCKWAVE:
			return AI_WEAPON_AIM_TYPE_PREDICTION;
		case WEAP_ELECTROBOLT:
			return ( fireMode == FIRE_MODE_STRONG ) ? AI_WEAPON_AIM_TYPE_INSTANT_HIT : AI_WEAPON_AIM_TYPE_PREDICTION;
		default:
			return AI_WEAPON_AIM_TYPE_INSTANT_HIT;
	}
}

int BuiltinWeaponTier( int builtinWeapon ) {
	switch( builtinWeapon ) {
		case WEAP_INSTAGUN:
			return 4;
		case WEAP_ELECTROBOLT:
		case WEAP_LASERGUN:
		case WEAP_PLASMAGUN:
		case WEAP_ROCKETLAUNCHER:
			return 3;
		case WEAP_MACHINEGUN:
		case WEAP_RIOTGUN:
		// Bots can't use it properly
		case WEAP_SHOCKWAVE:
			return 2;
		case WEAP_GRENADELAUNCHER:
			return 1;
		default:
			return 0;
	}
}

int FindBestWeaponTier( const gclient_t *client ) {
	const auto *inventory = client->ps.inventory;
	constexpr int ammoShift = AMMO_GUNBLADE - WEAP_GUNBLADE;
	constexpr int weakAmmoShift = AMMO_WEAK_GUNBLADE - WEAP_GUNBLADE;

	int maxTier = 0;
	for( int weapon = WEAP_GUNBLADE; weapon < WEAP_TOTAL; ++weapon ) {
		if( !inventory[weapon] || ( !inventory[weapon + ammoShift] && !inventory[weapon + weakAmmoShift] ) ) {
			continue;
		}
		int tier = BuiltinWeaponTier( weapon );
		if( tier <= maxTier ) {
			continue;
		}
		maxTier = tier;
	}

	return maxTier;
}

static void EscapePercent( const char *string, char *buffer, int bufferLen ) {
	int j = 0;
	for( const char *s = string; *s && j < bufferLen - 1; ++s ) {
		if( *s != '%' ) {
			buffer[j++] = *s;
		} else if( j < bufferLen - 2 ) {
			buffer[j++] = '%', buffer[j++] = '%';
		}
	}
	buffer[j] = '\0';
}

static void AI_PrintToBufferv( char *outputBuffer, size_t bufferSize, const char *nick, const char *format, va_list va ) {
	char concatBuffer[1024];

	int prefixLen = sprintf( concatBuffer, "t=%09" PRIi64 " %s: ", level.time, nick );
	Q_vsnprintfz( concatBuffer + prefixLen, 1024 - prefixLen, format, va );

	// concatBuffer may contain player names such as "%APPDATA%"
	EscapePercent( concatBuffer, outputBuffer, 2048 );
}

void AI_Debug( const char *nick, const char *format, ... ) {
#ifndef PUBLIC_BUILD
	va_list va;
	va_start( va, format );
	AI_Debugv( nick, format, va );
	va_end( va );
#endif
}

void AI_Debugv( const char *nick, const char *format, va_list va ) {
	if( !ai_debugOutput->integer ) {
		return;
	}

// Allow bot debug output in public build but require "developer" mode too.
// Do not control debug output only by "developer" mode though.
#ifdef PUBLIC_BUILD
	if( !developer->integer ) {
		return;
	}
#endif

	char outputBuffer[2048];
	AI_PrintToBufferv( outputBuffer, 2048, nick, format, va );
	G_Printf( "%s", outputBuffer );
}

void AI_FailWith( const char *tag, const char *format, ... ) {
	va_list va;
	va_start( va, format );
	AI_FailWithv( tag, format, va );
	va_end( va );
}

void AI_FailWithv( const char *tag, const char *format, va_list va ) {
	char outputBuffer[2048];
	AI_PrintToBufferv( outputBuffer, 2048, tag, format, va );
	G_Printf( "%s\n", outputBuffer );
	abort();
}

void AITools_DrawLine( const vec3_t origin, const vec3_t dest ) {
	edict_t *event;

	event = G_SpawnEvent( EV_GREEN_LASER, 0, const_cast<float *>( origin ) );
	VectorCopy( dest, event->s.origin2 );
	G_SetBoundsForSpanEntity( event, 8 );
	GClip_LinkEntity( event );
}

void AITools_DrawColorLine( const vec3_t origin, const vec3_t dest, int color, int parm ) {
	edict_t *event;

	event = G_SpawnEvent( EV_PNODE, parm, const_cast<float *>( origin ) );
	event->s.colorRGBA = color;
	VectorCopy( dest, event->s.origin2 );
	G_SetBoundsForSpanEntity( event, 8 );
	GClip_LinkEntity( event );
}

static wsw::StaticVector<int, 16> hubAreas;

//==========================================
// AI_InitLevel
// Inits Map local parameters
//==========================================
void AI_InitLevel( void ) {
	ai_evolution = trap_Cvar_Get( "ai_evolution", "0", CVAR_ARCHIVE );
	ai_debugOutput = trap_Cvar_Get( "ai_debugOutput", "0", CVAR_ARCHIVE );
	// We think values for this var should not be archived
	ai_shareRoutingCache = trap_Cvar_Get( "ai_shareRoutingCache", "1", 0 );

	AiAasWorld::Init( level.mapname );
	AiAasRouteCache::Init( *AiAasWorld::Instance() );
	TacticalSpotsRegistry::Init( level.mapname );
	AiGroundTraceCache::Init();
	HazardsSelectorCache::Init();

	AiManager::Init( g_gametype->string, level.mapname );

	NavEntitiesRegistry::Init();
	wsw::ai::FrameEntitiesCache::init();
}

void AI_Shutdown( void ) {
	hubAreas.clear();

	AI_AfterLevelScriptShutdown();
}

void AI_BeforeLevelLevelScriptShutdown() {
	if( auto aiManager = AiManager::Instance() ) {
		aiManager->BeforeLevelScriptShutdown();
	}
}

void AI_AfterLevelScriptShutdown() {
	if( auto aiManager = AiManager::Instance() ) {
		aiManager->AfterLevelScriptShutdown();
		AiManager::Shutdown();
	}

	wsw::ai::FrameEntitiesCache::shutdown();
	NavEntitiesRegistry::Shutdown();
	HazardsSelectorCache::Shutdown();
	AiGroundTraceCache::Shutdown();
	TacticalSpotsRegistry::Shutdown();
	AiAasRouteCache::Shutdown();
	AiAasWorld::Shutdown();
}

void AI_JoinedTeam( edict_t *ent, int team ) {
	AiManager::Instance()->OnBotJoinedTeam( ent, team );
}

void AI_CommonFrame() {
	AiAasWorld::Instance()->Frame();

	EntitiesPvsCache::Instance()->Update();

	NavEntitiesRegistry::Instance()->Update();

	wsw::ai::FrameEntitiesCache::instance()->update();

	AiManager::Instance()->Update();
}

static inline void ExtendDimension( float *mins, float *maxs, int dimension ) {
	float side = maxs[dimension] - mins[dimension];
	if( side < 48.0f ) {
		maxs[dimension] += 0.5f * ( 48.0f - side );
		mins[dimension] -= 0.5f * ( 48.0f - side );
	}
}

static int FindGoalAASArea( edict_t *ent ) {
	AiAasWorld *aasWorld = AiAasWorld::Instance();
	if( !aasWorld->IsLoaded() ) {
		return 0;
	}

	Vec3 mins( ent->r.mins ), maxs( ent->r.maxs );
	// Extend AABB XY dimensions
	ExtendDimension( mins.Data(), maxs.Data(), 0 );
	ExtendDimension( mins.Data(), maxs.Data(), 1 );
	// Z needs special extension rules
	float presentHeight = maxs.Z() - mins.Z();
	float playerHeight = playerbox_stand_maxs[2] - playerbox_stand_mins[2];
	if( playerHeight > presentHeight ) {
		maxs.Z() += playerHeight - presentHeight;
	}

	// Find all areas in bounds
	int areas[16];
	// Convert bounds to absolute ones
	mins += ent->s.origin;
	maxs += ent->s.origin;
	const int numAreas = aasWorld->BBoxAreas( mins, maxs, areas, 16 );

	int bestArea = 0;
	float bestScore = 0.0f;
	for( int i = 0; i < numAreas; ++i ) {
		const int areaNum = areas[i];
		float score = 0.0f;
		if( AiManager::Instance()->IsAreaReachableFromHubAreas( areaNum, &score ) ) {
			if( score > bestScore ) {
				bestScore = score;
				bestArea = areaNum;
			}
		}
	}
	if( bestArea ) {
		return bestArea;
	}

	// Fall back to a default method and hope it succeeds
	return aasWorld->FindAreaNum( ent );
}

void AI_AddNavEntity( edict_t *ent, ai_nav_entity_flags flags ) {
	if( !flags ) {
		G_Printf( S_COLOR_RED "AI_AddNavEntity(): flags are empty" );
		return;
	}
	int onlyMutExFlags = flags & ( AI_NAV_REACH_AT_TOUCH | AI_NAV_REACH_AT_RADIUS | AI_NAV_REACH_ON_EVENT );
	// Valid mutual exclusive flags give a power of two
	if( onlyMutExFlags & ( onlyMutExFlags - 1 ) ) {
		G_Printf( S_COLOR_RED "AI_AddNavEntity(): illegal flags %x for nav entity %s", flags, ent->classname );
		return;
	}

	if( ( flags & AI_NAV_NOTIFY_SCRIPT ) && !( flags & ( AI_NAV_REACH_AT_TOUCH | AI_NAV_REACH_AT_RADIUS ) ) ) {
		G_Printf( S_COLOR_RED
				  "AI_AddNavEntity(): NOTIFY_SCRIPT flag may be combined only with REACH_AT_TOUCH or REACH_AT_RADIUS" );
		return;
	}

	NavEntityFlags navEntityFlags = NavEntityFlags::NONE;
	if( flags & AI_NAV_REACH_AT_TOUCH ) {
		navEntityFlags = navEntityFlags | NavEntityFlags::REACH_AT_TOUCH;
	}
	if( flags & AI_NAV_REACH_AT_RADIUS ) {
		navEntityFlags = navEntityFlags | NavEntityFlags::REACH_AT_RADIUS;
	}
	if( flags & AI_NAV_REACH_ON_EVENT ) {
		navEntityFlags = navEntityFlags | NavEntityFlags::REACH_ON_EVENT;
	}
	if( flags & AI_NAV_REACH_IN_GROUP ) {
		navEntityFlags = navEntityFlags | NavEntityFlags::REACH_IN_GROUP;
	}
	if( flags & AI_NAV_DROPPED ) {
		navEntityFlags = navEntityFlags | NavEntityFlags::DROPPED_ENTITY;
	}
	if( flags & AI_NAV_MOVABLE ) {
		navEntityFlags = navEntityFlags | NavEntityFlags::MOVABLE;
	}
	if( flags & AI_NAV_NOTIFY_SCRIPT ) {
		navEntityFlags = navEntityFlags | NavEntityFlags::NOTIFY_SCRIPT;
	}

	int areaNum = FindGoalAASArea( ent );
	// Allow addition of temporary unreachable goals based on movable entities
	if( areaNum || ( flags & AI_NAV_MOVABLE ) ) {
		NavEntitiesRegistry::Instance()->AddNavEntity( ent, areaNum, navEntityFlags );
		return;
	}
	constexpr const char *format = S_COLOR_RED "AI_AddNavEntity(): Can't find an area num for %s @ %.3f %.3f %.3f\n";
	G_Printf( format, ent->classname, ent->s.origin[0], ent->s.origin[1], ent->s.origin[2] );
}

void AI_RemoveNavEntity( edict_t *ent ) {
	auto *const navEntitiesRegistry = NavEntitiesRegistry::Instance();
	if( !navEntitiesRegistry ) {
		return;
	}

	NavEntity *const navEntity = navEntitiesRegistry->NavEntityForEntity( ent );
	// (An nav. item absence is not an error, this function is called for each entity in game)
	if( !navEntity ) {
		return;
	}

	if( auto *const aiManager = AiManager::Instance() ) {
		aiManager->NavEntityReachedBy( navEntity, nullptr );
	}
	navEntitiesRegistry->RemoveNavEntity( navEntity );
}

void AI_NavEntityReached( edict_t *ent ) {
	AiManager::Instance()->NavEntityReachedSignal( ent );
}

//==========================================
// G_FreeAI
// removes the AI handle from memory
//==========================================
void G_FreeAI( edict_t *ent ) {
	if( !ent->ai ) {
		return;
	}

	AiManager::Instance()->UnlinkAi( ent->ai );

	// Perform a virtual destructor call
	ent->ai->~Ai();

	Q_free( ent->ai );
	ent->ai = nullptr;
}

void AI_TouchedEntity( edict_t *self, edict_t *ent ) {
	if( Ai *ai = self->ai ) {
		ai->TouchedEntity( ent );
	}
}

void AI_DamagedEntity( edict_t *self, edict_t *ent, int damage ) {
	if( Bot *bot = self->bot ) {
		bot->OnEnemyDamaged( ent, damage );
	}
}

void AI_Pain( edict_t *self, edict_t *attacker, int kick, int damage ) {
	if( Bot *bot = self->bot ) {
		bot->OnPain( attacker, kick, damage );
	}
}

void AI_Knockback( edict_t *self, edict_t *attacker, const vec3_t basedir, int kick, int dflags ) {
	if( Bot *bot = self->bot ) {
		bot->OnKnockback( attacker, basedir, kick, dflags );
	}
}

void AI_Think( edict_t *self ) {
	if( Ai *ai = self->ai ) {
		ai->Update();
	}
}

void AI_RegisterEvent( edict_t *ent, int event, int parm ) {
	AiManager::Instance()->RegisterEvent( ent, event, parm );
}

bool AI_CanSpawnBots() {
	return AiAasWorld::Instance()->IsLoaded();
}

void AI_SpawnBot( const char *team ) {
	AiManager::Instance()->SpawnBot( team );
}

void AI_Respawn( edict_t *ent ) {
	AiManager::Instance()->RespawnBot( ent );
}

void AI_RemoveBot( const char *name ) {
	AiManager::Instance()->RemoveBot( name );
}

void AI_RemoveBots() {
	AiManager::Instance()->AfterLevelScriptShutdown();
}

void AI_Cheat_NoTarget( edict_t *ent ) {
	if( !sv_cheats->integer ) {
		return;
	}

	ent->flags ^= FL_NOTARGET;
	if( ent->flags & FL_NOTARGET ) {
		G_PrintMsg( ent, "Bot Notarget ON\n" );
	} else {
		G_PrintMsg( ent, "Bot Notarget OFF\n" );
	}
}
