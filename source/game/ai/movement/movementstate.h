#ifndef WSW_8d257c89_c2f5_4704_bbac_c272921e7fda_H
#define WSW_8d257c89_c2f5_4704_bbac_c272921e7fda_H

#include "botinput.h"
#include "../baseai.h"

class PredictionContext;

class alignas ( 2 )AiCampingSpot {
	// Fields of this class are packed to allow cheap copying of class instances in bot movement prediction code
	friend class Bot;
	friend class CampingSpotState;

	int16_t origin[3];
	int16_t lookAtPoint[3];
	uint16_t radius;
	uint8_t alertness;
	AiCampingSpot() : radius( 32 ), alertness( 255 ), hasLookAtPoint( false ) {}

public:
	bool hasLookAtPoint;

	float Radius() const { return radius; }
	float Alertness() const { return alertness / 256.0f; }
	Vec3 Origin() const { return GetUnpacked4uVec( origin ); }
	Vec3 LookAtPoint() const { return GetUnpacked4uVec( lookAtPoint ); }

	// Warning! This does not set hasLookAtPoint, only used to store a vector in (initially unsused) lookAtPoint field
	// This behaviour is used when lookAtPoint is controlled manually by an external code.
	void SetLookAtPoint( const Vec3 &lookAtPoint_ ) { SetPacked4uVec( lookAtPoint_, lookAtPoint ); }

	AiCampingSpot( const Vec3 &origin_, float radius_, float alertness_ = 0.75f )
		: radius( (uint16_t)( radius_ ) ), alertness( (uint8_t)( alertness_ * 255 ) ), hasLookAtPoint( false )
	{
		SetPacked4uVec( origin_, origin );
	}

	AiCampingSpot( const vec3_t &origin_, float radius_, float alertness_ = 0.75f )
		: radius( (uint16_t)radius_ ), alertness( (uint8_t)( alertness_ * 255 ) ), hasLookAtPoint( false )
	{
		SetPacked4uVec( origin_, origin );
	}

	AiCampingSpot( const vec3_t &origin_, const vec3_t &lookAtPoint_, float radius_, float alertness_ = 0.75f )
		: radius( (uint16_t)radius_ ), alertness( (uint8_t)( alertness_ * 255 ) ), hasLookAtPoint( true )
	{
		SetPacked4uVec( origin_, origin );
		SetPacked4uVec( lookAtPoint_, lookAtPoint );
	}

	AiCampingSpot( const Vec3 &origin_, const Vec3 &lookAtPoint_, float radius_, float alertness_ = 0.75f )
		: radius( (uint16_t)radius_ ), alertness( (uint8_t)( alertness_ * 255 ) ), hasLookAtPoint( true )
	{
		SetPacked4uVec( origin_, origin );
		SetPacked4uVec( lookAtPoint_, lookAtPoint );
	}
};

class alignas ( 2 )AiPendingLookAtPoint {
	// Fields of this class are packed to allow cheap copying of class instances in bot movement prediction code
	friend struct PendingLookAtPointState;

	int16_t origin[3];
	// Floating point values greater than 1.0f are allowed (unless they are significantly greater than 1.0f);
	uint16_t turnSpeedMultiplier;

	AiPendingLookAtPoint() {
		// Shut an analyzer up
		turnSpeedMultiplier = 16;
	}

public:
	Vec3 Origin() const { return GetUnpacked4uVec( origin ); }
	float TurnSpeedMultiplier() const { return turnSpeedMultiplier / 16.0f; };

	AiPendingLookAtPoint( const vec3_t origin_, float turnSpeedMultiplier_ )
		: turnSpeedMultiplier( (uint16_t)( wsw::min( 255.0f, turnSpeedMultiplier_ * 16.0f ) ) )
	{
		SetPacked4uVec( origin_, origin );
	}

	AiPendingLookAtPoint( const Vec3 &origin_, float turnSpeedMultiplier_ )
		: turnSpeedMultiplier(  (uint16_t)( wsw::min( 255.0f, turnSpeedMultiplier_ * 16.0f ) ) )
	{
		SetPacked4uVec( origin_, origin );
	}
};

struct AerialMovementState {
protected:
	bool ShouldDeactivate( const edict_t *self, const class PredictionContext *context = nullptr ) const;
};

struct alignas ( 2 )JumppadMovementState : protected AerialMovementState {
	// Fields of this class are packed to allow cheap copying of class instances in bot movement prediction code

private:
	static_assert( MAX_EDICTS < ( 1 << 16 ), "Cannot store jumppad entity number in 16 bits" );
	uint16_t jumppadEntNum;

public:
	// Should be set by Bot::TouchedJumppad() callback (its get called in ClientThink())
	// It gets processed by movement code in next frame
	bool hasTouchedJumppad;
	// If this flag is set, bot is in "jumppad" movement state
	bool hasEnteredJumppad;

	JumppadMovementState()
		: jumppadEntNum( 0 ), hasTouchedJumppad( false ), hasEnteredJumppad( false ) {}

	// Useless but kept for structural type conformance with other movement states
	void Frame( unsigned frameTime ) {}

	bool IsActive() const {
		return ( hasTouchedJumppad || hasEnteredJumppad );
	}

	void Deactivate() {
		hasTouchedJumppad = false;
		hasEnteredJumppad = false;
	}

	void Activate( const edict_t *triggerEnt ) {
		hasTouchedJumppad = true;
		// Keep hasEnteredJumppad as-is (a jumppad might be touched again few millis later)
		jumppadEntNum = ( decltype( jumppadEntNum ) )( ENTNUM( const_cast<edict_t *>( triggerEnt ) ) );
	}

	void TryDeactivate( const edict_t *self, const PredictionContext *context = nullptr ) {
		if( ShouldDeactivate( self, context ) ) {
			Deactivate();
		}
	}

	const edict_t *JumppadEntity() const { return game.edicts + jumppadEntNum; }
	Vec3 JumpTarget() const { return Vec3( JumppadEntity()->target_ent->s.origin ); }
};

class alignas ( 2 )WeaponJumpMovementState : protected AerialMovementState {
	int16_t jumpTarget[3];
	int16_t fireTarget[3];
	int16_t originAtStart[3];
	// Sometimes bots cannot manage to look at the ground and get blocked.
	// This is a timer that allows resetting of this movement state in this case.
	uint16_t millisToTriggerJumpLeft;
public:
	int8_t weapon;
	bool hasPendingWeaponJump : 1;
	bool hasTriggeredWeaponJump : 1;
	bool hasCorrectedWeaponJump : 1;

	WeaponJumpMovementState()
		: millisToTriggerJumpLeft( 0 )
		, weapon( 0 )
		, hasPendingWeaponJump( false )
		, hasTriggeredWeaponJump( false )
		, hasCorrectedWeaponJump( false ) {}

	void Frame( unsigned frameTime ) {
		if( millisToTriggerJumpLeft >= frameTime ) {
			millisToTriggerJumpLeft -= frameTime;
		} else {
			millisToTriggerJumpLeft = 0;
		}
	}

	Vec3 JumpTarget() const { return GetUnpacked4uVec( jumpTarget ); }
	Vec3 FireTarget() const { return GetUnpacked4uVec( fireTarget ); }
	Vec3 OriginAtStart() const { return GetUnpacked4uVec( originAtStart ); }

	bool IsActive() const {
		return ( hasPendingWeaponJump || hasTriggeredWeaponJump || hasCorrectedWeaponJump );
	}

	void TryDeactivate( const edict_t *self, const class PredictionContext *context = nullptr );

	void Deactivate() {
		hasPendingWeaponJump = false;
		hasTriggeredWeaponJump = false;
		hasCorrectedWeaponJump = false;
	}

	void Activate( const Vec3 &jumpTarget_, const Vec3 &fireTarget_, const Vec3 &originAtStart_, int weapon_ ) {
		SetPacked4uVec( jumpTarget_, jumpTarget );
		SetPacked4uVec( fireTarget_, fireTarget );
		SetPacked4uVec( originAtStart_, originAtStart );
		millisToTriggerJumpLeft = 384u;
		hasPendingWeaponJump = true;
		hasTriggeredWeaponJump = false;
		hasCorrectedWeaponJump = false;
		assert( weapon_ > 0 && weapon_ < 32 );
		this->weapon = (int8_t)weapon_;
	}
};

struct alignas ( 2 )PendingLookAtPointState {
	AiPendingLookAtPoint pendingLookAtPoint;

private:
	unsigned char timeLeft;

public:
	PendingLookAtPointState() : timeLeft( 0 ) {}

	void Frame( unsigned frameTime ) {
		timeLeft = ( decltype( timeLeft ) ) wsw::max( 0, ( (int)timeLeft * 4 - (int)frameTime ) / 4 );
	}

	bool IsActive() const { return timeLeft > 0; }

	// Timeout period is limited by 1000 millis
	void Activate( const AiPendingLookAtPoint &pendingLookAtPoint_, unsigned timeoutPeriod = 500U ) {
		this->pendingLookAtPoint = pendingLookAtPoint_;
		this->timeLeft = ( decltype( this->timeLeft ) )( wsw::min( 1000U, timeoutPeriod ) / 4 );
	}

	void Deactivate() { timeLeft = 0; }

	void TryDeactivate( const edict_t *self, const class PredictionContext *context = nullptr ) {
		if( !IsActive() ) {
			Deactivate();
		}
	}
};

class alignas ( 2 )CampingSpotState {
	mutable AiCampingSpot campingSpot;
	// When to change chosen strafe dir
	mutable uint16_t moveDirsTimeLeft;
	// When to change randomly chosen look-at-point (if the point is not initially specified)
	mutable uint16_t lookAtPointTimeLeft;
	int8_t forwardMove : 4;
	int8_t rightMove : 4;
	bool isTriggered;

	unsigned StrafeDirTimeout() const {
		return (unsigned)( 400 + 100 * random() + 300 * ( 1.0f - campingSpot.Alertness() ) );
	}
	unsigned LookAtPointTimeout() const {
		return (unsigned)( 800 + 200 * random() + 2000 * ( 1.0f - campingSpot.Alertness() ) );
	}

public:
	CampingSpotState()
		: moveDirsTimeLeft( 0 )
		, lookAtPointTimeLeft( 0 )
		, forwardMove( 0 )
		, rightMove( 0 )
		, isTriggered( false ) {}

	void Frame( unsigned frameTime ) {
		moveDirsTimeLeft = ( uint16_t ) wsw::max( 0, (int)moveDirsTimeLeft - (int)frameTime );
		lookAtPointTimeLeft = ( uint16_t ) wsw::max( 0, (int)lookAtPointTimeLeft - (int)frameTime );
	}

	bool IsActive() const { return isTriggered; }

	void Activate( const AiCampingSpot &campingSpot_ ) {
		// Reset dir timers if and only if an actual origin has been significantly changed.
		// Otherwise this leads to "jitter" movement on the same point
		// when prediction errors prevent using a predicted action
		if( this->Origin().SquareDistance2DTo( campingSpot_.Origin() ) > 16 * 16 ) {
			moveDirsTimeLeft = 0;
			lookAtPointTimeLeft = 0;
		}
		this->campingSpot = campingSpot_;
		this->isTriggered = true;
	}

	void Deactivate() { isTriggered = false; }

	void TryDeactivate( const edict_t *self, const class PredictionContext *context = nullptr );

	Vec3 Origin() const { return campingSpot.Origin(); }
	float Radius() const { return campingSpot.Radius(); }

	AiPendingLookAtPoint GetOrUpdateRandomLookAtPoint() const;

	float Alertness() const { return campingSpot.Alertness(); }

	int ForwardMove() const { return forwardMove; }
	int RightMove() const { return rightMove; }

	bool AreKeyMoveDirsValid() { return moveDirsTimeLeft > 0; }

	void SetKeyMoveDirs( int forwardMove_, int rightMove_ ) {
		this->forwardMove = ( decltype( this->forwardMove ) )forwardMove_;
		this->rightMove = ( decltype( this->rightMove ) )rightMove_;
	}
};

class alignas ( 2 )KeyMoveDirsState {
public:
	uint16_t timeLeft { 0 };
	int8_t forwardMove { 0 };
	int8_t rightMove { 0 };
public:
	static constexpr uint16_t TIMEOUT_PERIOD = 500;

	void Frame( unsigned frameTime ) {
		timeLeft = ( decltype( timeLeft ) ) wsw::max( 0, ( (int)timeLeft - (int)frameTime ) );
	}

	bool IsActive() const { return timeLeft != 0; }

	void TryDeactivate( const edict_t *self, const class PredictionContext *context = nullptr ) {}

	void Deactivate() { timeLeft = 0; }

	void Activate( int forwardMove_, int rightMove_, unsigned timeLeft_ = TIMEOUT_PERIOD ) {
		this->forwardMove = ( decltype( this->forwardMove ) )forwardMove_;
		this->rightMove = ( decltype( this->rightMove ) )rightMove_;
		this->timeLeft = ( decltype( this->timeLeft ) )timeLeft_;
	}

	int ForwardMove() const { return forwardMove; }
	int RightMove() const { return rightMove; }
};

class alignas ( 2 )FlyUntilLandingMovementState : protected AerialMovementState {
	int16_t target[3];
	uint16_t landingDistanceThreshold;
	bool isTriggered : 1;
	// If not set, uses target Z level as landing threshold
	bool usesDistanceThreshold : 1;
	bool isLanding : 1;

public:
	FlyUntilLandingMovementState()
		: landingDistanceThreshold( 0 ),
		  isTriggered( false ),
		  usesDistanceThreshold( false ),
		  isLanding( false ) {}

	void Frame( unsigned frameTime ) {}

	bool CheckForLanding( const class PredictionContext *context );

	void Activate( const vec3_t target_, float landingDistanceThreshold_ ) {
		SetPacked4uVec( target_, this->target );
		landingDistanceThreshold = ( decltype( landingDistanceThreshold ) )( landingDistanceThreshold_ );
		isTriggered = true;
		usesDistanceThreshold = true;
		isLanding = false;
	}

	void Activate( const Vec3 &target_, float landingDistanceThreshold_ ) {
		Activate( target_.Data(), landingDistanceThreshold_ );
	}

	void Activate( float startLandingAtZ ) {
		this->target[2] = (short)startLandingAtZ;
		isTriggered = true;
		usesDistanceThreshold = false;
		isLanding = false;
	}

	bool IsActive() const { return isTriggered; }

	void Deactivate() { isTriggered = false; }

	void TryDeactivate( const edict_t *self, const class PredictionContext *context = nullptr ) {
		if( ShouldDeactivate( self, context ) ) {
			Deactivate();
		}
	}

	Vec3 Target() const { return GetUnpacked4uVec( target ); }
};

class Bot;

struct alignas ( 4 )MovementState {
	// We want to pack members tightly to reduce copying cost of this struct during the planning process
	static_assert( alignof( AiEntityPhysicsState ) == 4, "Members order by alignment is broken" );
	AiEntityPhysicsState entityPhysicsState;
	static_assert( alignof( CampingSpotState ) == 2, "Members order by alignment is broken" );
	CampingSpotState campingSpotState;
	static_assert( alignof( JumppadMovementState ) == 2, "Members order by alignment is broken" );
	JumppadMovementState jumppadMovementState;
	static_assert( alignof( WeaponJumpMovementState ) == 2, "Members order by alignment is broken" );
	WeaponJumpMovementState weaponJumpMovementState;
	static_assert( alignof( PendingLookAtPointState ) == 2, "Members order by alignment is broken" );
	PendingLookAtPointState pendingLookAtPointState;
	static_assert( alignof( FlyUntilLandingMovementState ) == 2, "Members order by alignment is broken" );
	FlyUntilLandingMovementState flyUntilLandingMovementState;
	static_assert( alignof( KeyMoveDirsState ) == 2, "Members order by alignment is broken" );
	KeyMoveDirsState keyMoveDirsState;

	// A current input rotation kind that is used in this state.
	// This value is saved to prevent choice jitter trying to apply an input rotation.
	// (The current input rotation kind has a bit less restrictive application conditions).
	InputRotation inputRotation { InputRotation::NONE };

	void Frame( unsigned frameTime ) {
		jumppadMovementState.Frame( frameTime );
		weaponJumpMovementState.Frame( frameTime );
		pendingLookAtPointState.Frame( frameTime );
		campingSpotState.Frame( frameTime );
		keyMoveDirsState.Frame( frameTime );
		flyUntilLandingMovementState.Frame( frameTime );
	}

	void TryDeactivateContainedStates( const edict_t *self, PredictionContext *context ) {
		jumppadMovementState.TryDeactivate( self, context );
		weaponJumpMovementState.TryDeactivate( self, context );
		pendingLookAtPointState.TryDeactivate( self, context );
		campingSpotState.TryDeactivate( self, context );
		keyMoveDirsState.TryDeactivate( self, context );
		flyUntilLandingMovementState.TryDeactivate( self, context );
	}

	void Reset() {
		jumppadMovementState.Deactivate();
		weaponJumpMovementState.Deactivate();
		pendingLookAtPointState.Deactivate();
		campingSpotState.Deactivate();
		keyMoveDirsState.Deactivate();
		flyUntilLandingMovementState.Deactivate();
	}

	unsigned GetContainedStatesMask() const {
		unsigned result = 0;
		result |= ( (unsigned)( jumppadMovementState.IsActive() ) ) << 0;
		result |= ( (unsigned)( weaponJumpMovementState.IsActive() ) ) << 1;
		result |= ( (unsigned)( pendingLookAtPointState.IsActive() ) ) << 2;
		result |= ( (unsigned)( campingSpotState.IsActive() ) ) << 3;
		// Skip keyMoveDirsState.
		// It either should not affect movement at all if regular movement is chosen,
		// or should be handled solely by the combat movement code.
		result |= ( (unsigned)( flyUntilLandingMovementState.IsActive() ) ) << 4;
		return result;
	}

	bool TestActualStatesForExpectedMask( unsigned expectedStatesMask, const Bot *bot ) const;
};

#endif
