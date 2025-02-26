#ifndef WSW_0646db93_a5f6_4b71_9267_800aa42abe4b_H
#define WSW_0646db93_a5f6_4b71_9267_800aa42abe4b_H

#include <cstdint>
#include <optional>
#include <span>

struct MessageFault;
struct ReplicatedScoreboardData;

struct AccuracyRows {
	using Span = std::span<const uint8_t, 10>;
	const Span weak;
	const Span strong;
};

namespace wsw { class StringView; }

namespace wsw::cl { struct ChatMessage; }

namespace wsw::ui {

class UISystem {
public:
	virtual ~UISystem() = default;

	static void init( int width, int height );
	static void shutdown();

	[[nodiscard]]
	static auto instance() -> UISystem *;

	virtual void refresh() = 0;

	virtual void drawSelfInMainContext() = 0;

	virtual void beginRegistration() = 0;
	virtual void endRegistration() = 0;

	[[nodiscard]]
	virtual bool requestsKeyboardFocus() const = 0;
	[[nodiscard]]
	virtual bool handleKeyEvent( int quakeKey, bool keyDown ) = 0;
	[[nodiscard]]
	virtual bool handleCharEvent( int ch ) = 0;
	[[nodiscard]]
	virtual bool handleMouseMove( int frameTime, int dx, int dy ) = 0;

	virtual void toggleInGameMenu() = 0;

	virtual void addToChat( const wsw::cl::ChatMessage &message ) = 0;
	virtual void addToTeamChat( const wsw::cl::ChatMessage &message ) = 0;

	virtual void handleMessageFault( const MessageFault &messageFault ) = 0;

	virtual void handleConfigString( unsigned configStringNum, const wsw::StringView &string ) = 0;

	virtual void updateScoreboard( const ReplicatedScoreboardData &scoreboardData, const AccuracyRows &accuracyRows ) = 0;

	virtual void setScoreboardShown( bool shown ) = 0;
	[[nodiscard]]
	virtual bool isShowingScoreboard() const = 0;

	virtual void toggleChatPopup() = 0;
	virtual void toggleTeamChatPopup() = 0;

	// This is a workaround for the current lack of ranges support
	template <typename ActionsRange>
	void touchActionRequest( const wsw::StringView &tag, unsigned timeout,
						     const wsw::StringView &title, const wsw::StringView &desc,
						     const ActionsRange &actions ) {
		touchActionRequest( tag, timeout, title, desc, std::begin( actions ), std::end( actions ) );
	}

	virtual void touchActionRequest( const wsw::StringView &tag, unsigned timeout,
								     const wsw::StringView &title, const wsw::StringView &desc,
								  	 const std::pair<wsw::StringView, int> *actionsBegin,
								  	 const std::pair<wsw::StringView, int> *actionsEnd ) = 0;

	virtual void handleOptionsStatusCommand( const wsw::StringView &status ) = 0;

	virtual void resetFragsFeed() = 0;
	virtual void addFragEvent( const std::pair<wsw::StringView, int> &victimAndTeam,
							   unsigned meansOfDeath,
							   const std::optional<std::pair<wsw::StringView, int>> &attackerAndTeam ) = 0;

	virtual void addToMessageFeed( const wsw::StringView &message ) = 0;

	virtual void addAward( const wsw::StringView &award ) = 0;

	virtual void addStatusMessage( const wsw::StringView &message ) = 0;

	enum class ConnectionFailKind {
		DontReconnect = 1,
		TryReconnecting,
		PasswordRequired
	};

	virtual void notifyOfFailedConnection( const wsw::StringView &message, ConnectionFailKind kind ) = 0;

	[[nodiscard]]
	virtual bool isShown() const = 0;
};

}

#endif
