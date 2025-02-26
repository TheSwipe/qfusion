/*
Copyright (C) 2002-2003 Victor Luchits

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

#include "cg_local.h"

#include "../qcommon/wswtonum.h"
#include "../qcommon/qcommon.h"
#include "../client/snd_public.h"
#include "../client/client.h"
#include "../ui/uisystem.h"

/*
==========================================================================

SERVER COMMANDS

==========================================================================
*/

/*
* CG_SC_Print
*/
static void CG_SC_Print( void ) {
	CG_LocalPrint( "%s", Cmd_Argv( 1 ) );
}

/*
* CG_SC_ChatPrint
*/
static void CG_SC_ChatPrint( void ) {
	const wsw::StringView commandName( Cmd_Argv( 0 ) );
	const bool teamonly = commandName.startsWith( 't' );
	std::optional<uint64_t> sendCommandNum;
	int whoArgNum = 1;
	if( commandName.endsWith( 'a' ) ) {
		if( ( sendCommandNum = wsw::toNum<uint64_t>( wsw::StringView( Cmd_Argv( 1 ) ) ) ) ) {
			whoArgNum = 2;
		} else {
			// TODO??? What to do in this case?
			return;
		}
	}

	const int who = atoi( Cmd_Argv( whoArgNum ) );
	const char *name = ( who && who == bound( 1, who, MAX_CLIENTS ) ? cgs.clientInfo[who - 1].name : "console" );
	const char *text = Cmd_Argv( whoArgNum + 1 );

	const wsw::StringView nameView( name );
	const wsw::StringView textView( text );

	if( teamonly ) {
		CG_LocalPrint( S_COLOR_YELLOW "[%s]" S_COLOR_WHITE "%s" S_COLOR_YELLOW ": %s\n",
					   cg.frame.playerState.stats[STAT_REALTEAM] == TEAM_SPECTATOR ? "SPEC" : "TEAM", name, text );
		wsw::ui::UISystem::instance()->addToTeamChat( {nameView, textView, sendCommandNum } );
	} else {
		CG_LocalPrint( "%s" S_COLOR_GREEN ": %s\n", name, text );
		wsw::ui::UISystem::instance()->addToChat( {nameView, textView, sendCommandNum } );
	}

	if( cg_chatBeep->integer ) {
		SoundSystem::instance()->startLocalSound( cgs.media.sfxChat, 1.0f );
	}
}

static void CG_SC_IgnoreCommand() {
	const char *firstArg = Cmd_Argv( 1 );
	// TODO: Is there a more generic method of setting client vars?
	// In fact this is actually a safer alternative so it should be kept
	if( !Q_stricmp( "setVar", firstArg ) ) {
		Cvar_ForceSet( cg_chatFilter->name, Cmd_Argv( 2 ) );
		return;
	}

	if( !cg_chatShowIgnored->integer ) {
		return;
	}

	const int who = ::atoi( firstArg );
	if( !who ) {
		return;
	}

	if( who != bound( 1, who, MAX_CLIENTS ) ) {
		return;
	}

	const char *format = S_COLOR_GREY "A message from " S_COLOR_WHITE "%s" S_COLOR_GREY " was ignored\n";
	CG_LocalPrint( format, cgs.clientInfo[who - 1].name );
}

static void CG_SC_MessageFault() {
	const char *const commandNumString = Cmd_Argv( 1 );
	const char *const faultKindString  = Cmd_Argv( 2 );
	const char *const timeoutString    = Cmd_Argv( 3 );
	if( commandNumString && faultKindString && timeoutString ) {
		if( const auto maybeCommandNum = wsw::toNum<uint64_t>( commandNumString ) ) {
			if( const auto maybeFaultKind = wsw::toNum<unsigned>( wsw::StringView( faultKindString ) ) ) {
				// Check timeout values for sanity by specifying a lesser type
				if( const auto maybeTimeoutValue = wsw::toNum<uint16_t>( wsw::StringView( timeoutString ) ) ) {
					if( *maybeFaultKind >= MessageFault::kMaxKind && *maybeFaultKind <= MessageFault::kMaxKind ) {
						const auto kind    = (MessageFault::Kind)*maybeFaultKind;
						const auto timeout = *maybeTimeoutValue;
						if( kind == MessageFault::Flood ) {
							const auto secondsLeft = (int)( *maybeTimeoutValue / 1000 ) + 1;
							CG_LocalPrint( "Flood protection. You can talk again in %d second(s)\n", secondsLeft );
						} else if( kind == MessageFault::Muted ) {
							CG_LocalPrint( "You are muted on this server\n" );
						} else {
							wsw::failWithLogicError( "unreachable" );
						}
						wsw::ui::UISystem::instance()->handleMessageFault( { *maybeCommandNum, kind, timeout } );
					}
				}
			}
		}
	}
}

/*
* CG_SC_CenterPrint
*/
static void CG_SC_CenterPrint( void ) {
	CG_CenterPrint( Cmd_Argv( 1 ) );
}

/*
* CG_SC_CenterPrintFormat
*/
static void CG_SC_CenterPrintFormat( void ) {
	if( Cmd_Argc() == 8 ) {
		CG_CenterPrint( va( Cmd_Argv( 1 ), Cmd_Argv( 2 ), Cmd_Argv( 3 ), Cmd_Argv( 4 ), Cmd_Argv( 5 ), Cmd_Argv( 6 ), Cmd_Argv( 7 ) ) );
	} else if( Cmd_Argc() == 7 ) {
		CG_CenterPrint( va( Cmd_Argv( 1 ), Cmd_Argv( 2 ), Cmd_Argv( 3 ), Cmd_Argv( 4 ), Cmd_Argv( 5 ), Cmd_Argv( 6 ) ) );
	} else if( Cmd_Argc() == 6 ) {
		CG_CenterPrint( va( Cmd_Argv( 1 ), Cmd_Argv( 2 ), Cmd_Argv( 3 ), Cmd_Argv( 4 ), Cmd_Argv( 5 ) ) );
	} else if( Cmd_Argc() == 5 ) {
		CG_CenterPrint( va( Cmd_Argv( 1 ), Cmd_Argv( 2 ), Cmd_Argv( 3 ), Cmd_Argv( 4 ) ) );
	} else if( Cmd_Argc() == 4 ) {
		CG_CenterPrint( va( Cmd_Argv( 1 ), Cmd_Argv( 2 ), Cmd_Argv( 3 ) ) );
	} else if( Cmd_Argc() == 3 ) {
		CG_CenterPrint( va( Cmd_Argv( 1 ), Cmd_Argv( 2 ) ) );
	} else if( Cmd_Argc() == 2 ) {
		CG_CenterPrint( Cmd_Argv( 1 ) ); // theoretically, shouldn't happen
	}
}

static const wsw::StringView kCorrectionSubstring( "correction/" );
static const wsw::StringView kGametypeMenu( "gametypemenu" );

/*
* CG_ConfigString
*/
void CG_ConfigString( int i, const wsw::StringView &string ) {
	cgs.configStrings.set( i, string );

	// do something apropriate
	if( i == CS_MAPNAME ) {
		CG_RegisterLevelMinimap();
	} else if( i == CS_GAMETYPETITLE ) {
	} else if( i == CS_GAMETYPENAME ) {
		GS_SetGametypeName( string.data() );
	} else if( i == CS_AUTORECORDSTATE ) {
		CG_SC_AutoRecordAction( string.data() );
	} else if( i >= CS_MODELS && i < CS_MODELS + MAX_MODELS ) {
		if( string.startsWith( '$' ) ) {  // indexed pmodel
			cgs.pModelsIndex[i - CS_MODELS] = CG_RegisterPlayerModel( string.data() + 1 );
		} else {
			cgs.modelDraw[i - CS_MODELS] = CG_RegisterModel( string.data() );
		}
	} else if( i >= CS_SOUNDS && i < CS_SOUNDS + MAX_SOUNDS ) {
		if( !string.startsWith( '*' ) ) {
			cgs.soundPrecache[i - CS_SOUNDS] = SoundSystem::instance()->registerSound( string.data() );
		}
	} else if( i >= CS_IMAGES && i < CS_IMAGES + MAX_IMAGES ) {
		if( string.indexOf( kCorrectionSubstring ) != std::nullopt ) { // HACK HACK HACK -- for color correction LUTs
			cgs.imagePrecache[i - CS_IMAGES] = R_RegisterLinearPic( string.data() );
		} else {
			cgs.imagePrecache[i - CS_IMAGES] = R_RegisterPic( string.data() );
		}
	} else if( i >= CS_SKINFILES && i < CS_SKINFILES + MAX_SKINFILES ) {
		cgs.skinPrecache[i - CS_SKINFILES] = R_RegisterSkinFile( string.data() );
	} else if( i >= CS_LIGHTS && i < CS_LIGHTS + MAX_LIGHTSTYLES ) {
		CG_SetLightStyle( i - CS_LIGHTS, string );
	} else if( i >= CS_ITEMS && i < CS_ITEMS + MAX_ITEMS ) {
		CG_ValidateItemDef( i - CS_ITEMS, string.data() );
	} else if( i >= CS_PLAYERINFOS && i < CS_PLAYERINFOS + MAX_CLIENTS ) {
		CG_LoadClientInfo( i - CS_PLAYERINFOS, string );
	} else if( i >= CS_GAMECOMMANDS && i < CS_GAMECOMMANDS + MAX_GAMECOMMANDS ) {
		if( !cgs.demoPlaying ) {
			Cmd_AddCommand( string.data(), NULL );
			if( string.equalsIgnoreCase( kGametypeMenu ) ) {
				cgs.hasGametypeMenu = true;
			}
		}
	} else if( i >= CS_WEAPONDEFS && i < CS_WEAPONDEFS + MAX_WEAPONDEFS ) {
		CG_OverrideWeapondef( i - CS_WEAPONDEFS, string.data() );
	}

	// Let the UI system decide whether it could handle the config string as well
	wsw::ui::UISystem::instance()->handleConfigString( i, string );
}

/*
* CG_SC_AutoRecordName
*/
static const char *CG_SC_AutoRecordName( void ) {
	time_t long_time;
	struct tm *newtime;
	static char name[MAX_STRING_CHARS];
	char mapname[MAX_QPATH];
	const char *cleanplayername, *cleanplayername2;

	// get date from system
	time( &long_time );
	newtime = localtime( &long_time );

	if( cg.view.POVent <= 0 ) {
		cleanplayername2 = "";
	} else {
		// remove color tokens from player names (doh)
		cleanplayername = COM_RemoveColorTokens( cgs.clientInfo[cg.view.POVent - 1].name );

		// remove junk chars from player names for files
		cleanplayername2 = COM_RemoveJunkChars( cleanplayername );
	}

	// lowercase mapname
	Q_strncpyz( mapname, cgs.configStrings.getMapName()->data(), sizeof( mapname ) );
	Q_strlwr( mapname );

	// make file name
	// duel_year-month-day_hour-min_map_player
	Q_snprintfz( name, sizeof( name ), "%s_%04d-%02d-%02d_%02d-%02d_%s_%s_%04i",
				 gs.gametypeName,
				 newtime->tm_year + 1900, newtime->tm_mon + 1, newtime->tm_mday,
				 newtime->tm_hour, newtime->tm_min,
				 mapname,
				 cleanplayername2,
				 (int)brandom( 0, 9999 )
				 );

	return name;
}

/*
* CG_SC_AutoRecordAction
*/
void CG_SC_AutoRecordAction( const char *action ) {
	static bool autorecording = false;
	const char *name;
	bool spectator;

	if( !action[0] ) {
		return;
	}

	// filter out autorecord commands when playing a demo
	if( cgs.demoPlaying ) {
		return;
	}

	// let configstrings and other stuff arrive before taking any action
	if( !cgs.precacheDone ) {
		return;
	}

	if( cg.frame.playerState.pmove.pm_type == PM_SPECTATOR || cg.frame.playerState.pmove.pm_type == PM_CHASECAM ) {
		spectator = true;
	} else {
		spectator = false;
	}

	name = CG_SC_AutoRecordName();

	if( !Q_stricmp( action, "start" ) ) {
		if( cg_autoaction_demo->integer && ( !spectator || cg_autoaction_spectator->integer ) ) {
			Cbuf_ExecuteText( EXEC_NOW, "stop silent" );
			Cbuf_ExecuteText( EXEC_NOW, va( "record autorecord/%s/%s silent",
												gs.gametypeName, name ) );
			autorecording = true;
		}
	} else if( !Q_stricmp( action, "altstart" ) ) {
		if( cg_autoaction_demo->integer && ( !spectator || cg_autoaction_spectator->integer ) ) {
			Cbuf_ExecuteText( EXEC_NOW, va( "record autorecord/%s/%s silent",
												gs.gametypeName, name ) );
			autorecording = true;
		}
	} else if( !Q_stricmp( action, "stop" ) ) {
		if( autorecording ) {
			Cbuf_ExecuteText( EXEC_NOW, "stop silent" );
			autorecording = false;
		}

		if( cg_autoaction_screenshot->integer && ( !spectator || cg_autoaction_spectator->integer ) ) {
			Cbuf_ExecuteText( EXEC_NOW, va( "screenshot autorecord/%s/%s silent",
												gs.gametypeName, name ) );
		}
	} else if( !Q_stricmp( action, "cancel" ) ) {
		if( autorecording ) {
			Cbuf_ExecuteText( EXEC_NOW, "stop cancel silent" );
			autorecording = false;
		}
	} else if( developer->integer ) {
		Com_Printf( "CG_SC_AutoRecordAction: Unknown action: %s\n", action );
	}
}

/**
 * Returns the English match state message.
 *
 * @param mm match message ID
 * @return match message text
 */
static const char *CG_MatchMessageString( matchmessage_t mm ) {
	switch( mm ) {
		case MATCHMESSAGE_CHALLENGERS_QUEUE:
			return "'ESC' for in-game menu or 'ENTER' for in-game chat.\n"
				   "You are inside the challengers queue waiting for your turn to play.\n"
				   "Use the in-game menu to exit the queue.\n"
				   "\nUse the mouse buttons for switching spectator modes.";

		case MATCHMESSAGE_ENTER_CHALLENGERS_QUEUE:
			return "'ESC' for in-game menu or 'ENTER' for in-game chat.\n"
				   "Use the in-game menu or press 'F3' to enter the challengers queue.\n"
				   "Only players in the queue will have a turn to play against the last winner.\n"
				   "\nUse the mouse buttons for switching spectator modes.";

		case MATCHMESSAGE_SPECTATOR_MODES:
			return "'ESC' for in-game menu or 'ENTER' for in-game chat.\n"
				   "Mouse buttons for switching spectator modes.\n"
				   "This message can be hidden by disabling 'help' in player setup menu.";

		case MATCHMESSAGE_GET_READY:
			return "Set yourself READY to start the match!\n"
				   "You can use the in-game menu or simply press 'F4'.\n"
				   "'ESC' for in-game menu or 'ENTER' for in-game chat.";

		case MATCHMESSAGE_WAITING_FOR_PLAYERS:
			return "Waiting for players.\n"
				   "'ESC' for in-game menu.";

		default:
			return "";
	}

	return "";
}

/*
* CG_SC_MatchMessage
*/
static void CG_SC_MatchMessage( void ) {
	matchmessage_t mm;
	const char *matchmessage;

	cg.matchmessage = NULL;

	mm = (matchmessage_t)atoi( Cmd_Argv( 1 ) );
	matchmessage = CG_MatchMessageString( mm );
	if( !matchmessage || !matchmessage[0] ) {
		return;
	}

	cg.matchmessage = matchmessage;
}

/*
* CG_SC_HelpMessage
*/
static void CG_SC_HelpMessage( void ) {
	cg.helpmessage[0] = '\0';

	unsigned index = atoi( Cmd_Argv( 1 ) );
	if( !index || index > MAX_HELPMESSAGES ) {
		return;
	}

	const auto maybeConfigString = cgs.configStrings.getHelpMessage( index - 1 );
	if( !maybeConfigString ) {
		return;
	}

	unsigned outlen = 0;
	int c;
	const char *helpmessage = maybeConfigString->data();
	while( ( c = helpmessage[0] ) && ( outlen < MAX_HELPMESSAGE_CHARS - 1 ) ) {
		helpmessage++;

		if( c == '{' ) { // template
			int t = *( helpmessage++ );
			switch( t ) {
				case 'B': // key binding
				{
					char cmd[MAX_STRING_CHARS];
					unsigned cmdlen = 0;
					while( ( c = helpmessage[0] ) != '\0' ) {
						helpmessage++;
						if( c == '}' ) {
							break;
						}
						if( cmdlen < MAX_STRING_CHARS - 1 ) {
							cmd[cmdlen++] = c;
						}
					}
					cmd[cmdlen] = '\0';
					CG_GetBoundKeysString( cmd, cg.helpmessage + outlen, MAX_HELPMESSAGE_CHARS - outlen );
					outlen += strlen( cg.helpmessage + outlen );
				}
					continue;
				default:
					helpmessage--;
					break;
			}
		}

		cg.helpmessage[outlen++] = c;
	}
	cg.helpmessage[outlen] = '\0';
	Q_FixTruncatedUtf8( cg.helpmessage );

	cg.helpmessage_time = cg.time;
}

/*
* CG_Cmd_DemoGet_f
*/
static bool demo_requested = false;
static void CG_Cmd_DemoGet_f( void ) {
	if( demo_requested ) {
		Com_Printf( "Already requesting a demo\n" );
		return;
	}

	if( Cmd_Argc() != 2 || ( atoi( Cmd_Argv( 1 ) ) <= 0 && Cmd_Argv( 1 )[0] != '.' ) ) {
		Com_Printf( "Usage: demoget <number>\n" );
		Com_Printf( "Downloads a demo from the server\n" );
		Com_Printf( "Use the demolist command to see list of demos on the server\n" );
		return;
	}

	Cbuf_ExecuteText( EXEC_NOW, va( "cmd demoget %s", Cmd_Argv( 1 ) ) );

	demo_requested = true;
}

/*
* CG_SC_DemoGet
*/
static void CG_SC_DemoGet( void ) {
	const char *filename, *extension;

	if( cgs.demoPlaying ) {
		// ignore download commands coming from demo files
		return;
	}

	if( !demo_requested ) {
		Com_Printf( "Warning: demoget when not requested, ignored\n" );
		return;
	}

	demo_requested = false;

	if( Cmd_Argc() < 2 ) {
		Com_Printf( "No such demo found\n" );
		return;
	}

	filename = Cmd_Argv( 1 );
	extension = COM_FileExtension( filename );
	if( !COM_ValidateRelativeFilename( filename ) ||
		!extension || Q_stricmp( extension, cgs.demoExtension ) ) {
		Com_Printf( "Warning: demoget: Invalid filename, ignored\n" );
		return;
	}

	CL_DownloadRequest( filename, false );
}

/*
* CG_SC_MOTD
*/
static void CG_SC_MOTD( void ) {
	char *motd;

	if( cg.motd ) {
		Q_free(   cg.motd );
	}
	cg.motd = NULL;

	motd = Cmd_Argv( 2 );
	if( !motd[0] ) {
		return;
	}

	if( !strcmp( Cmd_Argv( 1 ), "1" ) ) {
		cg.motd = Q_strdup( motd );
		cg.motd_time = cg.time + 50 * strlen( motd );
		if( cg.motd_time < cg.time + 5000 ) {
			cg.motd_time = cg.time + 5000;
		}
	}

	Com_Printf( "\nMessage of the Day:\n%s", motd );
}

/*
* CG_SC_AddAward
*/
static void CG_SC_AddAward( void ) {
	const char *str = Cmd_Argv( 1 );
	if( str && *str ) {
		wsw::ui::UISystem::instance()->addAward( wsw::StringView( str ) );
	}
}

static void CG_SC_ActionRequest() {
	int argNum = 1;
	// Expect a timeout
	if( const auto maybeTimeout = wsw::toNum<unsigned>( wsw::StringView( Cmd_Argv( argNum++ ) ) ) ) {
		// Expect a tag
		if( const wsw::StringView tag( Cmd_Argv( argNum++ ) ); !tag.empty() ) {
			// Expect a title
			if( const wsw::StringView title( Cmd_Argv( argNum++ ) ); !title.empty() ) {
				const wsw::StringView desc( Cmd_Argv( argNum++ ) );
				// Expect a number of commands
				if ( const auto maybeNumCommands = wsw::toNum<unsigned>( wsw::StringView( Cmd_Argv( argNum++ ) ) ) ) {
					// Read (key, command) pairs
					const auto maxArgNum = (int)wsw::min( 9u, *maybeNumCommands ) + argNum;
					wsw::StaticVector<std::pair<wsw::StringView, int>, 9> actions;
					const auto *bindingsSystem = wsw::cl::KeyBindingsSystem::instance();
					while( argNum < maxArgNum ) {
						const wsw::StringView keyView( Cmd_Argv( argNum++ ) );
						const auto maybeKey = bindingsSystem->getKeyForName( keyView );
						if( !maybeKey ) {
							return;
						}
						actions.emplace_back( { wsw::StringView( Cmd_Argv( argNum++ ) ), *maybeKey } );
					}
					wsw::ui::UISystem::instance()->touchActionRequest( tag, *maybeTimeout, title, desc, actions );
				}
			}
		}
	}
}

static void CG_SC_OptionsStatus() {
	if( Cmd_Argc() == 2 ) {
		wsw::ui::UISystem::instance()->handleOptionsStatusCommand( wsw::StringView( Cmd_Argv( 1 ) ) );
	}
}

static void CG_SC_PlaySound() {
	if( Cmd_Argc() < 2 ) {
		return;
	}

	SoundSystem::instance()->startLocalSound( Cmd_Argv( 1 ), 1.0f );
}

void CG_SC_ResetFragsFeed() {
	wsw::ui::UISystem::instance()->resetFragsFeed();
}

static void CG_SC_FragEvent() {
	if( Cmd_Argc() == 4 ) {
		unsigned args[3];
		for( int i = 0; i < 3; ++i ) {
			if( const auto maybeNum = wsw::toNum<unsigned>( wsw::StringView( Cmd_Argv( i + 1 ) ) ) ) {
				args[i] = *maybeNum;
			} else {
				return;
			}
		}
		const auto victim = args[0], attacker = args[1], meansOfDeath = args[2];
		if( ( victim && victim < (unsigned)( MAX_CLIENTS + 1 ) ) && attacker < (unsigned)( MAX_CLIENTS + 1 ) ) {
			if( meansOfDeath >= (unsigned)MOD_GUNBLADE_W && meansOfDeath < (unsigned)MOD_COUNT ) {
				if( const wsw::StringView victimName( cgs.clientInfo[victim - 1].name ); !victimName.empty() ) {
					std::optional<std::pair<wsw::StringView, int>> attackerAndTeam;
					if( attacker ) {
						if( const wsw::StringView view( cgs.clientInfo[attacker - 1].name ); !view.empty() ) {
							const int attackerRealTeam = cg_entities[attacker].current.team;
							attackerAndTeam = std::make_pair( view, attackerRealTeam );
						}
					}
					const int victimTeam = cg_entities[victim].current.team;
					const auto victimAndTeam( std::make_pair( victimName, victimTeam ) );
					wsw::ui::UISystem::instance()->addFragEvent( victimAndTeam, meansOfDeath, attackerAndTeam );
					if( attacker && attacker != victim && ISVIEWERENTITY( attacker ) ) {
						wsw::StaticString<256> message;
						if( cg_entities[attacker].current.team == cg_entities[victim].current.team ) {
							if( GS_TeamBasedGametype() ) {
								message << wsw::StringView( S_COLOR_ORANGE ) <<
									wsw::StringView( "You teamfragged " ) << wsw::StringView( S_COLOR_WHITE );
							}
						}
						if( message.empty() ) {
							message << wsw::StringView( "Cool! You fragged " );
						}
						message << victimName;
						wsw::ui::UISystem::instance()->addStatusMessage( message.asView() );
					}
				}
			}
		}
	}
}

typedef struct
{
	const char *name;
	void ( *func )( void );
} svcmd_t;

static const svcmd_t cg_svcmds[] =
{
	{ "pr", CG_SC_Print },

	// Chat-related commands
	{ "ch", CG_SC_ChatPrint },
	{ "tch", CG_SC_ChatPrint },
	// 'a' stands for "Acknowledge"
	{ "cha", CG_SC_ChatPrint },
	{ "tcha", CG_SC_ChatPrint },
	{ "ign", CG_SC_IgnoreCommand },
	{ "flt", CG_SC_MessageFault },

	{ "tflt", CG_SC_MessageFault },
	{ "cp", CG_SC_CenterPrint },
	{ "cpf", CG_SC_CenterPrintFormat },
	{ "fra", CG_SC_FragEvent },
	{ "mm", CG_SC_MatchMessage },
	{ "mapmsg", CG_SC_HelpMessage },
	{ "demoget", CG_SC_DemoGet },
	{ "motd", CG_SC_MOTD },
	{ "aw", CG_SC_AddAward },
	{ "arq", CG_SC_ActionRequest },
	{ "ply", CG_SC_PlaySound },
	{ "optionsstatus", CG_SC_OptionsStatus },

	{ NULL }
};

/*
* CG_GameCommand
*/
void CG_GameCommand( const char *command ) {
	char *s;
	const svcmd_t *cmd;

	Cmd_TokenizeString( command );

	s = Cmd_Argv( 0 );
	for( cmd = cg_svcmds; cmd->name; cmd++ ) {
		if( !strcmp( s, cmd->name ) ) {
			cmd->func();
			return;
		}
	}

	Com_Printf( "Unknown game command: %s\n", s );
}

/*
==========================================================================

CGAME COMMANDS

==========================================================================
*/

/*
* CG_UseItem
*/
void CG_UseItem( const char *name ) {
	gsitem_t *item;

	if( !cg.frame.valid || cgs.demoPlaying ) {
		return;
	}

	if( !name ) {
		return;
	}

	item = GS_Cmd_UseItem( &cg.frame.playerState, name, 0 );
	if( item ) {
		if( item->type & IT_WEAPON ) {
			CG_Predict_ChangeWeapon( item->tag );
			cg.lastWeapon = cg.predictedPlayerState.stats[STAT_PENDING_WEAPON];
		}

		Cbuf_ExecuteText( EXEC_NOW, va( "cmd use %i", item->tag ) );
	}
}

/*
* CG_Cmd_UseItem_f
*/
static void CG_Cmd_UseItem_f( void ) {
	if( !Cmd_Argc() ) {
		Com_Printf( "Usage: 'use <item name>' or 'use <item index>'\n" );
		return;
	}

	CG_UseItem( Cmd_Args() );
}

/*
* CG_Cmd_NextWeapon_f
*/
static void CG_Cmd_NextWeapon_f( void ) {
	gsitem_t *item;

	if( !cg.frame.valid ) {
		return;
	}

	if( cgs.demoPlaying || cg.predictedPlayerState.pmove.pm_type == PM_CHASECAM ) {
		CG_ChaseStep( 1 );
		return;
	}

	item = GS_Cmd_NextWeapon_f( &cg.frame.playerState, cg.predictedWeaponSwitch );
	if( item ) {
		CG_Predict_ChangeWeapon( item->tag );
		Cbuf_ExecuteText( EXEC_NOW, va( "cmd use %i", item->tag ) );
		cg.lastWeapon = cg.predictedPlayerState.stats[STAT_PENDING_WEAPON];
	}
}

/*
* CG_Cmd_PrevWeapon_f
*/
static void CG_Cmd_PrevWeapon_f( void ) {
	gsitem_t *item;

	if( !cg.frame.valid ) {
		return;
	}

	if( cgs.demoPlaying || cg.predictedPlayerState.pmove.pm_type == PM_CHASECAM ) {
		CG_ChaseStep( -1 );
		return;
	}

	item = GS_Cmd_PrevWeapon_f( &cg.frame.playerState, cg.predictedWeaponSwitch );
	if( item ) {
		CG_Predict_ChangeWeapon( item->tag );
		Cbuf_ExecuteText( EXEC_NOW, va( "cmd use %i", item->tag ) );
		cg.lastWeapon = cg.predictedPlayerState.stats[STAT_PENDING_WEAPON];
	}
}

/*
* CG_Cmd_PrevWeapon_f
*/
static void CG_Cmd_LastWeapon_f( void ) {
	gsitem_t *item;

	if( !cg.frame.valid || cgs.demoPlaying ) {
		return;
	}

	if( cg.lastWeapon != WEAP_NONE && cg.lastWeapon != cg.predictedPlayerState.stats[STAT_PENDING_WEAPON] ) {
		item = GS_Cmd_UseItem( &cg.frame.playerState, va( "%i", cg.lastWeapon ), IT_WEAPON );
		if( item ) {
			if( item->type & IT_WEAPON ) {
				CG_Predict_ChangeWeapon( item->tag );
			}

			Cbuf_ExecuteText( EXEC_NOW, va( "cmd use %i", item->tag ) );
			cg.lastWeapon = cg.predictedPlayerState.stats[STAT_PENDING_WEAPON];
		}
	}
}

/*
* CG_Viewpos_f
*/
static void CG_Viewpos_f( void ) {
	Com_Printf( "\"origin\" \"%i %i %i\"\n", (int)cg.view.origin[0], (int)cg.view.origin[1], (int)cg.view.origin[2] );
	Com_Printf( "\"angles\" \"%i %i %i\"\n", (int)cg.view.angles[0], (int)cg.view.angles[1], (int)cg.view.angles[2] );
}

// ======================================================================

/*
* CG_GametypeMenuCmdAdd_f
*/
static void CG_GametypeMenuCmdAdd_f( void ) {
	cgs.hasGametypeMenu = true;
}

/*
* CG_PlayerNamesCompletionExt_f
*
* Helper function
*/
static char **CG_PlayerNamesCompletionExt_f( const char *partial, bool teamOnly ) {
	int i;
	int team = cg_entities[cgs.playerNum + 1].current.team;
	char **matches = NULL;
	int num_matches = 0;

	if( partial ) {
		size_t partial_len = strlen( partial );

		matches = (char **) Q_malloc( sizeof( char * ) * ( gs.maxclients + 1 ) );
		for( i = 0; i < gs.maxclients; i++ ) {
			cg_clientInfo_t *info = cgs.clientInfo + i;
			if( !info->cleanname[0] ) {
				continue;
			}
			if( teamOnly && ( cg_entities[i + 1].current.team != team ) ) {
				continue;
			}
			if( !Q_strnicmp( info->cleanname, partial, partial_len ) ) {
				matches[num_matches++] = info->cleanname;
			}
		}
		matches[num_matches] = NULL;
	}

	return matches;
}

/*
* CG_PlayerNamesCompletion_f
*/
static char **CG_PlayerNamesCompletion_f( const char *partial ) {
	return CG_PlayerNamesCompletionExt_f( partial, false );
}

/*
* CG_TeamPlayerNamesCompletion_f
*/
static char **CG_TeamPlayerNamesCompletion_f( const char *partial ) {
	return CG_PlayerNamesCompletionExt_f( partial, true );
}

/*
* CG_SayCmdAdd_f
*/
static void CG_SayCmdAdd_f( void ) {
	Cmd_SetCompletionFunc( "say", &CG_PlayerNamesCompletion_f );
}

/*
* CG_SayTeamCmdAdd_f
*/
static void CG_SayTeamCmdAdd_f( void ) {
	Cmd_SetCompletionFunc( "say_team", &CG_TeamPlayerNamesCompletion_f );
}

/*
* CG_StatsCmdAdd_f
*/
static void CG_StatsCmdAdd_f( void ) {
	Cmd_SetCompletionFunc( "stats", &CG_PlayerNamesCompletion_f );
}

/*
* CG_WhoisCmdAdd_f
*/
static void CG_WhoisCmdAdd_f( void ) {
	Cmd_SetCompletionFunc( "whois", &CG_PlayerNamesCompletion_f );
}

// server commands
static svcmd_t cg_consvcmds[] =
{
	{ "gametypemenu", CG_GametypeMenuCmdAdd_f },
	{ "say", CG_SayCmdAdd_f },
	{ "say_team", CG_SayTeamCmdAdd_f },
	{ "stats", CG_StatsCmdAdd_f },
	{ "whois", CG_WhoisCmdAdd_f },

	{ NULL, NULL }
};

// local cgame commands
typedef struct
{
	const char *name;
	void ( *func )( void );
	bool allowdemo;
} cgcmd_t;

static const cgcmd_t cgcmds[] =
{
	{ "+scores", CG_ScoresOn_f, true },
	{ "-scores", CG_ScoresOff_f, true },
	{ "messagemode", CG_MessageMode, false },
	{ "messagemode2", CG_MessageMode2, false },
	{ "demoget", CG_Cmd_DemoGet_f, false },
	{ "demolist", NULL, false },
	{ "use", CG_Cmd_UseItem_f, false },
	{ "weapnext", CG_Cmd_NextWeapon_f, true },
	{ "weapprev", CG_Cmd_PrevWeapon_f, true },
	{ "weaplast", CG_Cmd_LastWeapon_f, true },
	{ "viewpos", CG_Viewpos_f, true },
	{ "players", NULL, false },
	{ "spectators", NULL, false },

	{ NULL, NULL, false }
};

/*
* CG_RegisterCGameCommands
*/
void CG_RegisterCGameCommands( void ) {
	if( !cgs.demoPlaying ) {
		const svcmd_t *svcmd;

		// add game side commands
		for( unsigned i = 0; i < MAX_GAMECOMMANDS; i++ ) {
			const auto maybeName = cgs.configStrings.getGameCommand( i );
			if( !maybeName ) {
				continue;
			}

			const auto name = *maybeName;

			// check for local command overrides
			const cgcmd_t *cmd;
			for( cmd = cgcmds; cmd->name; cmd++ ) {
				if( !Q_stricmp( cmd->name, name.data() ) ) {
					break;
				}
			}
			if( cmd->name ) {
				continue;
			}

			Cmd_AddCommand( name.data(), NULL );

			// check for server commands we might want to do some special things for..
			for( svcmd = cg_consvcmds; svcmd->name; svcmd++ ) {
				if( !Q_stricmp( svcmd->name, name.data() ) ) {
					if( svcmd->func ) {
						svcmd->func();
					}
					break;
				}
			}
		}
	}

	// add local commands
	for( const auto *cmd = cgcmds; cmd->name; cmd++ ) {
		if( cgs.demoPlaying && !cmd->allowdemo ) {
			continue;
		}
		Cmd_AddCommand( cmd->name, cmd->func );
	}
}

/*
* CG_UnregisterCGameCommands
*/
void CG_UnregisterCGameCommands( void ) {
	if( !cgs.demoPlaying ) {
		// remove game commands
		for( unsigned i = 0; i < MAX_GAMECOMMANDS; i++ ) {
			const auto maybeName = cgs.configStrings.getGameCommand( i );
			if( !maybeName ) {
				continue;
			}

			const auto name = *maybeName;
			// check for local command overrides so we don't try
			// to unregister them twice
			const cgcmd_t *cmd;
			for( cmd = cgcmds; cmd->name; cmd++ ) {
				if( !Q_stricmp( cmd->name, name.data() ) ) {
					break;
				}
			}
			if( cmd->name ) {
				continue;
			}

			Cmd_RemoveCommand( name.data() );
		}

		cgs.hasGametypeMenu = false;
	}

	// remove local commands
	for( const auto *cmd = cgcmds; cmd->name; cmd++ ) {
		if( cgs.demoPlaying && !cmd->allowdemo ) {
			continue;
		}
		Cmd_RemoveCommand( cmd->name );
	}
}
