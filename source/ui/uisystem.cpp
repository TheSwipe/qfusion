#include "uisystem.h"
#include "../qcommon/links.h"
#include "../qcommon/singletonholder.h"
#include "../qcommon/qcommon.h"
#include "../client/client.h"
#include "../game/ai/static_vector.h"
#include "nativelydrawnitems.h"

#include <QGuiApplication>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QQuickRenderControl>
#include <QQuickWindow>
#include <QOffscreenSurface>
#include <QOpenGLFunctions>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QUrl>

QVariant VID_GetMainContextHandle();

bool GLimp_BeginUIRenderingHacks();
bool GLimp_EndUIRenderingHacks();

/**
 * Just to provide a nice prefix in Qml scope.
 * There could be multiple connections and multiple states.
 * This makes the state meaning clear.
 */
class QuakeClient : public QObject {
	Q_OBJECT

public:
	enum State {
		Disconnected,
		MMValidating,
		Connecting,
		Loading,
		Active
	};
	Q_ENUM( State );
};

class QWswUISystem : public QObject, public UISystem {
	Q_OBJECT

	// The implementation is borrowed from https://github.com/RSATom/QuickLayer

	template <typename> friend class SingletonHolder;
	friend class NativelyDrawnImage;
	friend class NativelyDrawnModel;
public:
	void refresh( unsigned refreshFlags ) override;

	void drawSelfInMainContext() override;

	void beginRegistration() override {};
	void endRegistration() override {};

	void handleKeyEvent( int quakeKey, bool keyDown, Context context ) override;
	void handleCharEvent( int ch ) override;
	void handleMouseMove( int frameTime, int dx, int dy ) override;

	void forceMenuOn() override {};
	void forceMenuOff() override {};

	[[nodiscard]]
	bool hasRespectMenu() const override { return isShowingRespectMenu; };

	void showRespectMenu( bool show ) override {
		if( show == isShowingRespectMenu ) {
			return;
		}
		isShowingRespectMenu = show;
		Q_EMIT isShowingRespectMenuChanged( isShowingRespectMenu );
	};

	void enterUIRenderingMode();
	void leaveUIRenderingMode();

	Q_PROPERTY( QuakeClient::State quakeClientState READ getQuakeClientState NOTIFY quakeClientStateChanged );
	Q_PROPERTY( bool isPlayingADemo READ isPlayingADemo NOTIFY isPlayingADemoChanged );
	Q_PROPERTY( bool isShowingInGameMenu READ isShowingInGameMenuGetter NOTIFY isShowingInGameMenuChanged );
	Q_PROPERTY( bool isShowingRespectMenu READ isShowingRespectMenuGetter NOTIFY isShowingRespectMenuChanged );
	Q_PROPERTY( bool isDebuggingNativelyDrawnItems READ isDebuggingNativelyDrawnItems NOTIFY isDebuggingNativelyDrawnItemsChanged );

	Q_INVOKABLE void registerNativelyDrawnItem( QQuickItem *item );
	Q_INVOKABLE void unregisterNativelyDrawnItem( QQuickItem *item );
signals:
	Q_SIGNAL void quakeClientStateChanged( QuakeClient::State state );
	Q_SIGNAL void isPlayingADemoChanged( bool isPlayingADemo );
	Q_SIGNAL void isShowingInGameMenuChanged( bool isShowingInGameMenu );
	Q_SIGNAL void isShowingRespectMenuChanged( bool isShowingRespectMenu );
	Q_SIGNAL void isDebuggingNativelyDrawnItemsChanged( bool isDebuggingNativelyDrawnItems );
public slots:
	Q_SLOT void onSceneGraphInitialized();
	Q_SLOT void onRenderRequested();
	Q_SLOT void onSceneChanged();

	Q_SLOT void onComponentStatusChanged( QQmlComponent::Status status );
private:
	QGuiApplication *application { nullptr };
	QOpenGLContext *externalContext { nullptr };
	QOpenGLContext *sharedContext { nullptr };
	QQuickRenderControl *control { nullptr };
	QOpenGLFramebufferObject *framebufferObject { nullptr };
	QOffscreenSurface *surface { nullptr };
	QQuickWindow *quickWindow { nullptr };
	QQmlEngine *engine { nullptr };
	QQmlComponent *component { nullptr };
	bool hasPendingSceneChange { false };
	bool hasPendingRedraw { false };
	bool isInUIRenderingMode { false };
	bool isValidAndReady { false };

	// A copy of last frame client properties for state change detection without intrusive changes to client code.
	// Use a separate scope for clarity and for avoiding name conflicts.
	struct {
		bool isPlayingADemo { false };
		QuakeClient::State quakeClientState { QuakeClient::Disconnected };
	} lastFrameState;

	bool isShowingInGameMenu { false };
	bool isShowingRespectMenu { false };

	bool hasStartedBackgroundMapLoading {false };
	bool hasSucceededBackgroundMapLoading {false };

	cvar_t *ui_sensitivity { nullptr };
	cvar_t *ui_mouseAccel { nullptr };

	cvar_t *ui_debugNativelyDrawnItems { nullptr };

	qreal mouseXY[2] { 0.0, 0.0 };

	QString charStrings[128];

	NativelyDrawn *m_nativelyDrawnListHead { nullptr };

	static constexpr const int kMaxNativelyDrawnItems = 64;

	int m_numNativelyDrawnItems { 0 };

	[[nodiscard]]
	auto getQuakeClientState() const { return lastFrameState.quakeClientState; }

	[[nodiscard]]
	bool isPlayingADemo() const { return lastFrameState.isPlayingADemo; }

	[[nodiscard]]
	bool isShowingInGameMenuGetter() const { return isShowingInGameMenu; };

	[[nodiscard]]
	bool isShowingRespectMenuGetter() const { return isShowingRespectMenu; };

	[[nodiscard]]
	bool isDebuggingNativelyDrawnItems() const;

	explicit QWswUISystem( int width, int height );

	void checkPropertyChanges();
	void renderQml();

	[[nodiscard]]
	auto getPressedMouseButtons() const -> Qt::MouseButtons;
	[[nodiscard]]
	auto getPressedKeyboardModifiers() const -> Qt::KeyboardModifiers;

	bool tryHandlingKeyEventAsAMouseEvent( int quakeKey, bool keyDown );

	void drawBackgroundMapIfNeeded();

	[[nodiscard]]
	auto convertQuakeKeyToQtKey( int quakeKey ) const -> std::optional<Qt::Key>;
};

void QWswUISystem::onSceneGraphInitialized() {
	auto attachment = QOpenGLFramebufferObject::CombinedDepthStencil;
	framebufferObject = new QOpenGLFramebufferObject( quickWindow->size(), attachment );
	quickWindow->setRenderTarget( framebufferObject );
}

void QWswUISystem::onRenderRequested() {
	hasPendingRedraw = true;
}

void QWswUISystem::onSceneChanged() {
	hasPendingSceneChange = true;
}

void QWswUISystem::onComponentStatusChanged( QQmlComponent::Status status ) {
	if ( QQmlComponent::Ready != status ) {
		if( status == QQmlComponent::Error ) {
			Com_Printf( S_COLOR_RED "The root Qml component loading has failed: %s\n",
				component->errorString().toUtf8().constData() );
		}
		return;
	}

	QObject *const rootObject = component->create();
	if( !rootObject ) {
		Com_Printf( S_COLOR_RED "Failed to finish the root Qml component creation\n" );
		return;
	}

	auto *const rootItem = qobject_cast<QQuickItem*>( rootObject );
	if( !rootItem ) {
		Com_Printf( S_COLOR_RED "The root Qml component is not a QQuickItem\n" );
		return;
	}

	QQuickItem *const parentItem = quickWindow->contentItem();
	const QSizeF size( quickWindow->width(), quickWindow->height() );
	parentItem->setSize( size );
	rootItem->setParentItem( parentItem );
	rootItem->setSize( size );

	isValidAndReady = true;
}

static SingletonHolder<QWswUISystem> uiSystemInstanceHolder;
// Hacks for allowing retrieval of a maybe-instance
// (we do not want to add these hacks to SingletonHolder)
static bool initialized = false;

void UISystem::init( int width, int height ) {
	::uiSystemInstanceHolder.Init( width, height );
	initialized = true;
}

void UISystem::shutdown() {
	::uiSystemInstanceHolder.Shutdown();
	initialized = false;
}

auto UISystem::instance() -> UISystem * {
	return ::uiSystemInstanceHolder.Instance();
}

auto UISystem::maybeInstance() -> std::optional<UISystem *> {
	if( initialized ) {
		return ::uiSystemInstanceHolder.Instance();
	}
	return std::nullopt;
}

void QWswUISystem::refresh( unsigned refreshFlags ) {
#ifndef _WIN32
	QGuiApplication::processEvents( QEventLoop::AllEvents );
#endif

	checkPropertyChanges();

	if( !isValidAndReady ) {
		return;
	}

	enterUIRenderingMode();
	renderQml();
	leaveUIRenderingMode();
}

QVariant VID_GetMainContextHandle();

static bool isAPrintableChar( int ch ) {
	if( ch < 0 || ch > 127 ) {
		return false;
	}

	// See https://en.cppreference.com/w/cpp/string/byte/isprint
	return std::isprint( (unsigned char)ch );
}

QWswUISystem::QWswUISystem( int initialWidth, int initialHeight ) {
	int fakeArgc = 0;
	char *fakeArgv[] = { nullptr };
	application = new QGuiApplication( fakeArgc, fakeArgv );

	QSurfaceFormat format;
	format.setDepthBufferSize( 24 );
	format.setStencilBufferSize( 8 );
	format.setMajorVersion( 3 );
	format.setMinorVersion( 3 );
	format.setRenderableType( QSurfaceFormat::OpenGL );
	format.setProfile( QSurfaceFormat::CompatibilityProfile );

	externalContext = new QOpenGLContext;
	externalContext->setNativeHandle( VID_GetMainContextHandle() );
	if( !externalContext->create() ) {
		Com_Printf( S_COLOR_RED "Failed to create a Qt wrapper of the main rendering context\n" );
		return;
	}

	sharedContext = new QOpenGLContext;
	sharedContext->setFormat( format );
	sharedContext->setShareContext( externalContext );
	if( !sharedContext->create() ) {
		Com_Printf( S_COLOR_RED "Failed to create a dedicated Qt OpenGL rendering context\n" );
		return;
	}

	control = new QQuickRenderControl();
	quickWindow = new QQuickWindow( control );
	quickWindow->setGeometry( 0, 0, initialWidth, initialHeight );
	quickWindow->setColor( Qt::transparent );

	QObject::connect( quickWindow, &QQuickWindow::sceneGraphInitialized, this, &QWswUISystem::onSceneGraphInitialized );
	QObject::connect( control, &QQuickRenderControl::renderRequested, this, &QWswUISystem::onRenderRequested );
	QObject::connect( control, &QQuickRenderControl::sceneChanged, this, &QWswUISystem::onSceneChanged );

	surface = new QOffscreenSurface;
	surface->setFormat( sharedContext->format() );
	surface->create();
	if ( !surface->isValid() ) {
		Com_Printf( S_COLOR_RED "Failed to create a dedicated Qt OpenGL offscreen surface\n" );
		return;
	}

	enterUIRenderingMode();

	bool hadErrors = true;
	if( sharedContext->makeCurrent( surface ) ) {
		control->initialize( sharedContext );
		quickWindow->resetOpenGLState();
		hadErrors = sharedContext->functions()->glGetError() != GL_NO_ERROR;
	} else {
		Com_Printf( S_COLOR_RED "Failed to make the dedicated Qt OpenGL rendering context current\n" );
	}

	leaveUIRenderingMode();

	if( hadErrors ) {
		Com_Printf( S_COLOR_RED "Failed to initialize the Qt Quick render control from the given GL context\n" );
		return;
	}

	const QString reason( "This type is a native code bridge and cannot be instantiated" );
	qmlRegisterUncreatableType<QWswUISystem>( "net.warsow", 2, 6, "Wsw", reason );
	qmlRegisterUncreatableType<QuakeClient>( "net.warsow", 2, 6, "QuakeClient", reason );
	qmlRegisterType<NativelyDrawnImage>( "net.warsow", 2, 6, "NativelyDrawnImage_Native" );
	qmlRegisterType<NativelyDrawnModel>( "net.warsow", 2, 6, "NativelyDrawnModel_Native" );

	engine = new QQmlEngine;
	engine->rootContext()->setContextProperty( "wsw", this );

	component = new QQmlComponent( engine );

	connect( component, &QQmlComponent::statusChanged, this, &QWswUISystem::onComponentStatusChanged );
	component->loadUrl( QUrl( "qrc:/RootItem.qml" ) );

	ui_sensitivity = Cvar_Get( "ui_sensitivity", "1.0", CVAR_ARCHIVE );
	ui_mouseAccel = Cvar_Get( "ui_mouseAccel", "0.25", CVAR_ARCHIVE );

	ui_debugNativelyDrawnItems = Cvar_Get( "ui_debugNativelyDrawnItems", "0", 0 );

	// Initialize the table of textual strings corresponding to characters
	for( const QString &s: charStrings ) {
		const auto offset = (int)( &s - charStrings );
		if( ::isAPrintableChar( offset ) ) {
			charStrings[offset] = QString::asprintf( "%c", (char)offset );
		}
	}
}

void QWswUISystem::renderQml() {
	assert( isValidAndReady );

	if( !hasPendingSceneChange && !hasPendingRedraw ) {
		return;
	}

	if( hasPendingSceneChange ) {
		control->polishItems();
		control->sync();
	}

	hasPendingSceneChange = hasPendingRedraw = false;

	if( !sharedContext->makeCurrent( surface ) ) {
		// Consider this a fatal error
		Com_Error( ERR_FATAL, "Failed to make the dedicated Qt OpenGL rendering context current\n" );
	}

	control->render();

	quickWindow->resetOpenGLState();

	auto *const f = sharedContext->functions();
	f->glFlush();
	f->glFinish();
}

void QWswUISystem::enterUIRenderingMode() {
	assert( !isInUIRenderingMode );
	isInUIRenderingMode = true;

	if( !GLimp_BeginUIRenderingHacks() ) {
		Com_Error( ERR_FATAL, "Failed to enter the UI rendering mode\n" );
	}
}

void QWswUISystem::leaveUIRenderingMode() {
	assert( isInUIRenderingMode );
	isInUIRenderingMode = false;

	if( !GLimp_EndUIRenderingHacks() ) {
		Com_Error( ERR_FATAL, "Failed to leave the UI rendering mode\n" );
	}
}

void R_Set2DMode( bool );
void R_DrawExternalTextureOverlay( unsigned );
shader_t *R_RegisterPic( const char * );
struct model_s *R_RegisterModel( const char * );
void RF_RegisterWorldModel( const char * );
void RF_ClearScene();
void RF_RenderScene( const refdef_t * );
void RF_DrawStretchPic( int x, int y, int w, int h, float s1, float t1, float s2, float t2,
	                    const vec4_t color, const shader_t *shader );

void QWswUISystem::drawSelfInMainContext() {
	if( !isValidAndReady ) {
		return;
	}

	drawBackgroundMapIfNeeded();

	// Make deeper items get evicted first from a max-heap
	const auto cmp = []( const NativelyDrawn *lhs, const NativelyDrawn *rhs ) {
		return lhs->m_nativeZ > rhs->m_nativeZ;
	};

	StaticVector<NativelyDrawn *, kMaxNativelyDrawnItems> zHeaps[2];
	for( NativelyDrawn *nativelyDrawn = m_nativelyDrawnListHead; nativelyDrawn; nativelyDrawn = nativelyDrawn->next ) {
		auto &heap = zHeaps[nativelyDrawn->m_nativeZ >= 0];
		heap.push_back( nativelyDrawn );
		std::push_heap( heap.begin(), heap.end(), cmp );
	}

	// This is quite inefficient as we switch rendering modes for different kinds of items.
	// Unfortunately this is mandatory for maintaining the desired Z order.
	// Considering the low number of items of this kind the performance impact should be negligible.

	while( !zHeaps[0].empty() ) {
		std::pop_heap( zHeaps[0].begin(), zHeaps[0].end(), cmp );
		zHeaps[0].back()->drawSelfNatively();
		zHeaps[0].pop_back();
	}

	R_Set2DMode( true );
	R_DrawExternalTextureOverlay( framebufferObject->texture() );
	R_Set2DMode( false );

	while( !zHeaps[1].empty() ) {
		std::pop_heap( zHeaps[1].begin(), zHeaps[1].end(), cmp );
		zHeaps[1].back()->drawSelfNatively();
		zHeaps[1].pop_back();
	}

	// TODO: Draw while showing an in-game menu as well (there should be a different condition)
	if( lastFrameState.quakeClientState != QuakeClient::Disconnected ) {
		return;
	}

	R_Set2DMode( true );
	vec4_t color = { 1.0f, 1.0f, 1.0f, 1.0f };
	// TODO: Check why CL_BeginRegistration()/CL_EndRegistration() never gets called
	auto *cursorMaterial = R_RegisterPic( "gfx/ui/cursor.tga" );
	// TODO: Account for screen pixel density
	RF_DrawStretchPic( (int)mouseXY[0], (int)mouseXY[1], 32, 32, 0.0f, 0.0f, 1.0f, 1.0f, color, cursorMaterial );
	R_Set2DMode( false );
}

void QWswUISystem::drawBackgroundMapIfNeeded() {
	if( lastFrameState.quakeClientState != QuakeClient::Disconnected ) {
		hasStartedBackgroundMapLoading = false;
		hasSucceededBackgroundMapLoading = false;
		return;
	}

	constexpr const char *worldModelName = "maps/ui.bsp";
	if( !hasStartedBackgroundMapLoading ) {
		RF_RegisterWorldModel( worldModelName );
		hasStartedBackgroundMapLoading = true;
	} else if( !hasSucceededBackgroundMapLoading ) {
		if( R_RegisterModel( worldModelName ) ) {
			hasSucceededBackgroundMapLoading = true;
		}
	}

	if( !hasSucceededBackgroundMapLoading ) {
		return;
	}

	refdef_t rdf;
	memset( &rdf, 0, sizeof( rdf ) );
	rdf.areabits = nullptr;

	const auto widthAndHeight = std::make_pair( quickWindow->width(), quickWindow->height() );
	std::tie( rdf.x, rdf.y ) = std::make_pair( 0, 0 );
	std::tie( rdf.width, rdf.height ) = widthAndHeight;

	// This is a copy-paste from Warsow 2.1 map_ui.pk3 CSS
	const vec3_t origin { 302.0f, -490.0f, 120.0f };
	const vec3_t angles { 0, -240, 0 };

	VectorCopy( origin, rdf.vieworg );
	AnglesToAxis( angles, rdf.viewaxis );
	rdf.fov_x = 90.0f;
	rdf.fov_y = CalcFov( 90.0f, rdf.width, rdf.height );
	AdjustFov( &rdf.fov_x, &rdf.fov_y, rdf.width, rdf.height, false );
	rdf.time = 0;

	std::tie( rdf.scissor_x, rdf.scissor_y ) = std::make_pair( 0, 0 );
	std::tie( rdf.scissor_width, rdf.scissor_height ) = widthAndHeight;

	RF_ClearScene();
	RF_RenderScene( &rdf );
}

void QWswUISystem::checkPropertyChanges() {
	auto *currClientState = &lastFrameState.quakeClientState;
	const auto formerClientState = *currClientState;

	if( cls.state == CA_UNINITIALIZED || cls.state == CA_DISCONNECTED ) {
		*currClientState = QuakeClient::Disconnected;
	} else if( cls.state == CA_GETTING_TICKET ) {
		*currClientState = QuakeClient::MMValidating;
	} else if( cls.state == CA_LOADING ) {
		*currClientState = QuakeClient::Loading;
	} else if( cls.state == CA_ACTIVE ) {
		*currClientState = QuakeClient::Active;
	} else {
		*currClientState = QuakeClient::Connecting;
	}

	if( *currClientState != formerClientState ) {
		Q_EMIT quakeClientStateChanged( *currClientState );
	}

	auto *isPlayingADemo = &lastFrameState.isPlayingADemo;
	const auto wasPlayingADemo = *isPlayingADemo;

	*isPlayingADemo = cls.demo.playing;
	if( *isPlayingADemo != wasPlayingADemo ) {
		Q_EMIT isPlayingADemoChanged( *isPlayingADemo );
	}

	if( ui_debugNativelyDrawnItems->modified ) {
		Q_EMIT isDebuggingNativelyDrawnItemsChanged( ui_debugNativelyDrawnItems->integer != 0 );
		ui_debugNativelyDrawnItems->modified = false;
	}
}

void QWswUISystem::handleMouseMove( int frameTime, int dx, int dy ) {
	if( !dx && !dy ) {
		return;
	}

	const int bounds[2] = { quickWindow->width(), quickWindow->height() };
	const int deltas[2] = { dx, dy };

	if( ui_sensitivity->modified ) {
		if( ui_sensitivity->value <= 0.0f || ui_sensitivity->value > 10.0f ) {
			Cvar_ForceSet( ui_sensitivity->name, "1.0" );
		}
	}

	if( ui_mouseAccel->modified ) {
		if( ui_mouseAccel->value < 0.0f || ui_mouseAccel->value > 1.0f ) {
			Cvar_ForceSet( ui_mouseAccel->name, "0.25" );
		}
	}

	float sensitivity = ui_sensitivity->value;
	if( frameTime > 0 ) {
		sensitivity += (float)ui_mouseAccel->value * std::sqrt( dx * dx + dy * dy ) / (float)( frameTime );
	}

	for( int i = 0; i < 2; ++i ) {
		if( !deltas[i] ) {
			continue;
		}
		qreal scaledDelta = ( (qreal)deltas[i] * sensitivity );
		// Make sure we won't lose a mouse movement due to fractional part loss
		if( !scaledDelta ) {
			scaledDelta = Q_sign( deltas[i] );
		}
		mouseXY[i] += scaledDelta;
		Q_clamp( mouseXY[i], 0, bounds[i] );
	}

	QPointF point( mouseXY[0], mouseXY[1] );
	QMouseEvent event( QEvent::MouseMove, point, Qt::NoButton, getPressedMouseButtons(), getPressedKeyboardModifiers() );
	QCoreApplication::sendEvent( quickWindow, &event );
}

void QWswUISystem::handleKeyEvent( int quakeKey, bool keyDown, Context context ) {
	// Currently unsupported
	if( context == RespectContext ) {
		return;
	}

	if( tryHandlingKeyEventAsAMouseEvent( quakeKey, keyDown ) ) {
		return;
	}

	const auto maybeQtKey = convertQuakeKeyToQtKey( quakeKey );
	if( !maybeQtKey ) {
		return;
	}

	const auto type = keyDown ? QEvent::KeyPress : QEvent::KeyRelease;
	QKeyEvent keyEvent( type, *maybeQtKey, getPressedKeyboardModifiers() );
	QCoreApplication::sendEvent( quickWindow, &keyEvent );
}

void QWswUISystem::handleCharEvent( int ch ) {
	if( !::isAPrintableChar( ch ) ) {
		return;
	}

	const auto modifiers = getPressedKeyboardModifiers();
	// The plain cast of `ch` to Qt::Key seems to be correct in this case
	// (all printable characters seem to map 1-1 to Qt key codes)
	QKeyEvent pressEvent( QEvent::KeyPress, (Qt::Key)ch, modifiers, charStrings[ch] );
	QCoreApplication::sendEvent( quickWindow, &pressEvent );
	QKeyEvent releaseEvent( QEvent::KeyRelease, (Qt::Key)ch, modifiers );
	QCoreApplication::sendEvent( quickWindow, &releaseEvent );
}

auto QWswUISystem::getPressedMouseButtons() const -> Qt::MouseButtons {
	auto result = Qt::MouseButtons();
	if( Key_IsDown( K_MOUSE1 ) ) {
		result |= Qt::LeftButton;
	}
	if( Key_IsDown( K_MOUSE2 ) ) {
		result |= Qt::RightButton;
	}
	if( Key_IsDown( K_MOUSE3 ) ) {
		result |= Qt::MiddleButton;
	}
	return result;
}

auto QWswUISystem::getPressedKeyboardModifiers() const -> Qt::KeyboardModifiers {
	auto result = Qt::KeyboardModifiers();
	if( Key_IsDown( K_LCTRL ) || Key_IsDown( K_RCTRL ) ) {
		result |= Qt::ControlModifier;
	}
	if( Key_IsDown( K_LALT ) || Key_IsDown( K_RALT ) ) {
		result |= Qt::AltModifier;
	}
	if( Key_IsDown( K_LSHIFT ) || Key_IsDown( K_RSHIFT ) ) {
		result |= Qt::ShiftModifier;
	}
	return result;
}

bool QWswUISystem::tryHandlingKeyEventAsAMouseEvent( int quakeKey, bool keyDown ) {
	Qt::MouseButton button;
	if( quakeKey == K_MOUSE1 ) {
		button = Qt::LeftButton;
	} else if( quakeKey == K_MOUSE2 ) {
		button = Qt::RightButton;
	} else if( quakeKey == K_MOUSE3 ) {
		button = Qt::MiddleButton;
	} else {
		return false;
	}

	QPointF point( mouseXY[0], mouseXY[1] );
	QEvent::Type eventType = keyDown ? QEvent::MouseButtonPress : QEvent::MouseButtonRelease;
	QMouseEvent event( eventType, point, button, getPressedMouseButtons(), getPressedKeyboardModifiers() );
	QCoreApplication::sendEvent( quickWindow, &event );
	return true;
}

auto QWswUISystem::convertQuakeKeyToQtKey( int quakeKey ) const -> std::optional<Qt::Key> {
	if( quakeKey < 0 ) {
		return std::nullopt;
	}

	static_assert( K_BACKSPACE == 127 );
	if( quakeKey < 127 ) {
		if( quakeKey == K_TAB ) {
			return Qt::Key_Tab;
		}
		if( quakeKey == K_ENTER ) {
			return Qt::Key_Enter;
		}
		if( quakeKey == K_ESCAPE ) {
			return Qt::Key_Escape;
		}
		if( quakeKey == K_SPACE ) {
			return Qt::Key_Space;
		}
		if( std::isprint( quakeKey ) ) {
			return (Qt::Key)quakeKey;
		}
		return std::nullopt;
	}

	if( quakeKey >= K_F1 && quakeKey <= K_F15 ) {
		return (Qt::Key)( Qt::Key_F1 + ( quakeKey - K_F1 ) );
	}

	// Some other seemingly unuseful keys are ignored
	switch( quakeKey ) {
		case K_BACKSPACE: return Qt::Key_Backspace;

		case K_UPARROW: return Qt::Key_Up;
		case K_DOWNARROW: return Qt::Key_Down;
		case K_LEFTARROW: return Qt::Key_Left;
		case K_RIGHTARROW: return Qt::Key_Right;

		case K_LALT:
		case K_RALT:
			return Qt::Key_Alt;

		case K_LCTRL:
		case K_RCTRL:
			return Qt::Key_Control;

		case K_LSHIFT:
		case K_RSHIFT:
			return Qt::Key_Shift;

		case K_INS: return Qt::Key_Insert;
		case K_DEL: return Qt::Key_Delete;
		case K_PGDN: return Qt::Key_PageDown;
		case K_PGUP: return Qt::Key_PageUp;
		case K_HOME: return Qt::Key_Home;
		case K_END: return Qt::Key_End;

		default: return std::nullopt;
	}
}

bool QWswUISystem::isDebuggingNativelyDrawnItems() const {
	return ui_debugNativelyDrawnItems->integer != 0;
}

void QWswUISystem::registerNativelyDrawnItem( QQuickItem *item ) {
	auto *nativelyDrawn = dynamic_cast<NativelyDrawn *>( item );
	if( !nativelyDrawn ) {
		Com_Printf( "An item is not an instance of NativelyDrawn\n" );
		return;
	}
	if( m_numNativelyDrawnItems == kMaxNativelyDrawnItems ) {
		Com_Printf( "Too many natively drawn items, skipping this one\n" );
		return;
	}
	::Link( nativelyDrawn, &this->m_nativelyDrawnListHead );
	nativelyDrawn->m_isLinked = true;
	m_numNativelyDrawnItems++;
}

void QWswUISystem::unregisterNativelyDrawnItem( QQuickItem *item ) {
	auto *nativelyDrawn = dynamic_cast<NativelyDrawn *>( item );
	if( !nativelyDrawn ) {
		Com_Printf( "An item is not an instance of NativelyDrawn\n" );
		return;
	}
	if( !nativelyDrawn->m_isLinked ) {
		return;
	}
	::Unlink( nativelyDrawn, &this->m_nativelyDrawnListHead );
	nativelyDrawn->m_isLinked = false;
	m_numNativelyDrawnItems--;
	assert( m_numNativelyDrawnItems >= 0 );
}

#include "uisystem.moc"

