#include "snd_effect_sampler.h"
#include "snd_leaf_props_cache.h"
#include "snd_effects_allocator.h"
#include "snd_propagation.h"
#include "efxpresetsregistry.h"

#include "../qcommon/wswstaticvector.h"
#include "../qcommon/wswstringsplitter.h"

#include <limits>
#include <random>

static UnderwaterFlangerEffectSampler underwaterFlangerEffectSampler;

static ReverbEffectSampler reverbEffectSampler;

Effect *EffectSamplers::TryApply( const ListenerProps &listenerProps, src_t *src, const src_t *tryReusePropsSrc ) {
	Effect *effect;
	if( ( effect = ::underwaterFlangerEffectSampler.TryApply( listenerProps, src, tryReusePropsSrc ) ) ) {
		return effect;
	}
	if( ( effect = ::reverbEffectSampler.TryApply( listenerProps, src, tryReusePropsSrc ) ) ) {
		return effect;
	}

	Com_Error( ERR_FATAL, "EffectSamplers::TryApply(): Can't find an applicable effect sampler\n" );
}

// We want sampling results to be reproducible especially for leaf sampling and thus use this local implementation
static std::minstd_rand0 samplingRandom;

float EffectSamplers::SamplingRandom() {
	typedef decltype( samplingRandom ) R;
	return ( samplingRandom() - R::min() ) / (float)( R::max() - R::min() );
}

Effect *UnderwaterFlangerEffectSampler::TryApply( const ListenerProps &listenerProps, src_t *src, const src_t * ) {
	if( !listenerProps.isInLiquid && !src->envUpdateState.isInLiquid ) {
		return nullptr;
	}

	float directObstruction = 0.9f;
	if( src->envUpdateState.isInLiquid && listenerProps.isInLiquid ) {
		directObstruction = ComputeDirectObstruction( listenerProps, src );
	}

	auto *effect = EffectsAllocator::Instance()->NewFlangerEffect( src );
	effect->directObstruction = directObstruction;
	effect->hasMediumTransition = src->envUpdateState.isInLiquid ^ listenerProps.isInLiquid;
	return effect;
}

static bool ENV_TryReuseSourceReverbProps( src_t *src, const src_t *tryReusePropsSrc, EaxReverbEffect *newEffect ) {
	if( !tryReusePropsSrc ) {
		return false;
	}

	auto *reuseEffect = Effect::Cast<const EaxReverbEffect *>( tryReusePropsSrc->envUpdateState.effect );
	if( !reuseEffect ) {
		return false;
	}

	// We are already sure that both sources are in the same contents kind (non-liquid).
	// Check distance between sources.
	const float squareDistance = DistanceSquared( tryReusePropsSrc->origin, src->origin );
	// If they are way too far for reusing
	if( squareDistance > 96 * 96 ) {
		return false;
	}

	// If they are very close, feel free to just copy props
	if( squareDistance < 4.0f * 4.0f ) {
		newEffect->CopyReverbProps( reuseEffect );
		return true;
	}

	// Do a coarse raycast test between these two sources
	vec3_t start, end, dir;
	VectorSubtract( tryReusePropsSrc->origin, src->origin, dir );
	const float invDistance = 1.0f / sqrtf( squareDistance );
	VectorScale( dir, invDistance, dir );
	// Offset start and end by a dir unit.
	// Ensure start and end are in "air" and not on a brush plane
	VectorAdd( src->origin, dir, start );
	VectorSubtract( tryReusePropsSrc->origin, dir, end );

	trace_t trace;
	S_Trace( &trace, start, end, vec3_origin, vec3_origin, MASK_SOLID );
	if( trace.fraction != 1.0f ) {
		return false;
	}

	newEffect->CopyReverbProps( reuseEffect );
	return true;
}

void ObstructedEffectSampler::SetupDirectObstructionSamplingProps( src_t *src, unsigned minSamples, unsigned maxSamples ) {
	float quality = s_environment_sampling_quality->value;
	samplingProps_t *props = &src->envUpdateState.directObstructionSamplingProps;

	// If the quality is valid and has not been modified since the pattern has been set
	if( props->quality == quality ) {
		return;
	}

	unsigned numSamples = GetNumSamplesForCurrentQuality( minSamples, maxSamples );

	props->quality = quality;
	props->numSamples = numSamples;
	props->valueIndex = (uint16_t)( EffectSamplers::SamplingRandom() * std::numeric_limits<uint16_t>::max() );
}

struct DirectObstructionOffsetsHolder {
	enum { NUM_VALUES = 256 };
	vec3_t offsets[NUM_VALUES];
	enum { MAX_OFFSET = 20 };

	DirectObstructionOffsetsHolder() {
		for( auto *v: offsets ) {
			for( int i = 0; i < 3; ++i ) {
				v[i] = -MAX_OFFSET + 2 * MAX_OFFSET * EffectSamplers::SamplingRandom();
			}
		}
	}
};

static DirectObstructionOffsetsHolder directObstructionOffsetsHolder;

float ObstructedEffectSampler::ComputeDirectObstruction( const ListenerProps &listenerProps, src_t *src ) {
	trace_t trace;
	envUpdateState_t *updateState;
	float *originOffset;
	vec3_t testedListenerOrigin;
	vec3_t testedSourceOrigin;
	float squareDistance;
	unsigned numTestedRays, numPassedRays;
	unsigned valueIndex;

	updateState = &src->envUpdateState;

	VectorCopy( listenerProps.origin, testedListenerOrigin );
	// TODO: We assume standard view height
	testedListenerOrigin[2] += 18.0f;

	squareDistance = DistanceSquared( testedListenerOrigin, src->origin );
	// Shortcut for sounds relative to the player
	if( squareDistance < 32.0f * 32.0f ) {
		return 0.0f;
	}

	if( !S_LeafsInPVS( listenerProps.GetLeafNum(), S_PointLeafNum( src->origin ) ) ) {
		return 1.0f;
	}

	vec3_t hintBounds[2];
	ClearBounds( hintBounds[0], hintBounds[1] );
	AddPointToBounds( testedListenerOrigin, hintBounds[0], hintBounds[1] );
	AddPointToBounds( src->origin, hintBounds[0], hintBounds[1] );
	// Account for obstruction sampling offsets
	// as we are going to compute the top node hint once
	for( int i = 0; i < 3; ++i ) {
		hintBounds[0][i] -= DirectObstructionOffsetsHolder::MAX_OFFSET;
		hintBounds[1][i] += DirectObstructionOffsetsHolder::MAX_OFFSET;
	}

	const int topNodeHint = S_FindTopNodeForBox( hintBounds[0], hintBounds[1] );
	S_Trace( &trace, testedListenerOrigin, src->origin, vec3_origin, vec3_origin, MASK_SOLID, topNodeHint );
	if( trace.fraction == 1.0f && !trace.startsolid ) {
		// Consider zero obstruction in this case
		return 0.0f;
	}

	SetupDirectObstructionSamplingProps( src, 3, MAX_DIRECT_OBSTRUCTION_SAMPLES );

	numPassedRays = 0;
	numTestedRays = updateState->directObstructionSamplingProps.numSamples;
	valueIndex = updateState->directObstructionSamplingProps.valueIndex;
	for( unsigned i = 0; i < numTestedRays; i++ ) {
		valueIndex = ( valueIndex + 1 ) % DirectObstructionOffsetsHolder::NUM_VALUES;
		originOffset = directObstructionOffsetsHolder.offsets[ valueIndex ];

		VectorAdd( src->origin, originOffset, testedSourceOrigin );
		S_Trace( &trace, testedListenerOrigin, testedSourceOrigin, vec3_origin, vec3_origin, MASK_SOLID, topNodeHint );
		if( trace.fraction == 1.0f && !trace.startsolid ) {
			numPassedRays++;
		}
	}

	return 1.0f - 0.9f * ( numPassedRays / (float)numTestedRays );
}

Effect *ReverbEffectSampler::TryApply( const ListenerProps &listenerProps, src_t *src, const src_t *tryReusePropsSrc ) {
	EaxReverbEffect *effect = EffectsAllocator::Instance()->NewReverbEffect( src );
	effect->directObstruction = ComputeDirectObstruction( listenerProps, src );
	// We try reuse props only for reverberation effects
	// since reverberation effects sampling is extremely expensive.
	// Moreover, direct obstruction reuse is just not valid,
	// since even a small origin difference completely changes it.
	if( ENV_TryReuseSourceReverbProps( src, tryReusePropsSrc, effect ) ) {
		src->envUpdateState.needsInterpolation = false;
	} else {
		ComputeReverberation( listenerProps, src, effect );
	}
	return effect;
}

float ReverbEffectSampler::GetEmissionRadius() const {
	// Do not even bother casting rays 999999 units ahead for very attenuated sources.
	// However, clamp/normalize the hit distance using the same defined threshold
	float attenuation = src->attenuation;

	if( attenuation <= 1.0f ) {
		return 999999.9f;
	}

	clamp_high( attenuation, 10.0f );
	float distance = 4.0f * REVERB_ENV_DISTANCE_THRESHOLD;
	distance -= 3.5f * Q_Sqrt( attenuation / 10.0f ) * REVERB_ENV_DISTANCE_THRESHOLD;
	return distance;
}

void ReverbEffectSampler::ResetMutableState( const ListenerProps &listenerProps_, src_t *src_, EaxReverbEffect *effect_ ) {
	this->listenerProps = &listenerProps_;
	this->src = src_;
	this->effect = effect_;

	GenericRaycastSampler::ResetMutableState( primaryRayDirs, reflectionPoints, primaryHitDistances, src->origin );

	VectorCopy( listenerProps_.origin, testedListenerOrigin );
	testedListenerOrigin[2] += 18.0f;
}

void ReverbEffectSampler::ComputeReverberation( const ListenerProps &listenerProps_,
												src_t *src_,
												EaxReverbEffect *effect_ ) {
	ResetMutableState( listenerProps_, src_, effect_ );

	numPrimaryRays = GetNumSamplesForCurrentQuality( 16, MAX_REVERB_PRIMARY_RAY_SAMPLES );

	SetupPrimaryRayDirs();

	EmitPrimaryRays();

	if( !numPrimaryHits ) {
		// Keep existing values (they are valid by default now)
		return;
	}

	ProcessPrimaryEmissionResults();
	EmitSecondaryRays();
}

void ReverbEffectSampler::SetupPrimaryRayDirs() {
	assert( numPrimaryRays );

	SetupSamplingRayDirs( primaryRayDirs, numPrimaryRays );
}

struct LerpPresetHelper {
	const EFXEAXREVERBPROPERTIES *tinyOpenRoomPreset;
	const EFXEAXREVERBPROPERTIES *tinyClosedRoomPreset;
	const EFXEAXREVERBPROPERTIES *hugeOpenRoomPreset;
	const EFXEAXREVERBPROPERTIES *hugeClosedRoomPreset;
	const float skyFrac;
	const float sizeFrac;

	[[nodiscard]]
	auto calcBiLerpValue( float (EFXEAXREVERBPROPERTIES::*fieldPtr ) ) const -> float {
		const float tinyOpenValue   = tinyOpenRoomPreset->*fieldPtr;
		const float tinyClosedValue = tinyClosedRoomPreset->*fieldPtr;
		const float hugeOpenValue   = hugeOpenRoomPreset->*fieldPtr;
		const float hugeClosedValue = hugeClosedRoomPreset->*fieldPtr;

		const float tinyValue = std::lerp( tinyClosedValue, tinyOpenValue, skyFrac );
		const float hugeValue = std::lerp( hugeClosedValue, hugeOpenValue, skyFrac );
		return std::lerp( tinyValue, hugeValue, sizeFrac );
	}
};

class CachedPresetTracker {
public:
	CachedPresetTracker( const char *varName, const char *defaultValue ) noexcept
		: m_varName( varName ), m_defaultValue( defaultValue ) {}

	[[nodiscard]]
	auto getPreset() const -> const EFXEAXREVERBPROPERTIES * {
		if( !m_var ) {
			m_var = Cvar_Get( m_varName, m_defaultValue, CVAR_CHEAT | CVAR_DEVELOPER );
			m_var->modified = true;
		}
		if( m_var->modified ) {
			m_preset = getByString( wsw::StringView( m_var->string ) );
			if( !m_preset ) {
				Com_Printf( S_COLOR_YELLOW "Failed to find a preset by string \"%s\" for %s. Using the default value \"%s\"\n",
							m_var->string, m_var->name, m_defaultValue );
				Cvar_ForceSet( m_var->name, m_defaultValue );
				m_preset = getByString( wsw::StringView( m_var->string ) );
				assert( m_preset );
			}
			m_var->modified = false;
		}
		return m_preset;
	}
private:
	// This method allows for an overall nicer code, and it's totally fine assuming this is not a hot code path
	[[nodiscard]]
	static auto mixFieldOfParts( const float EFXEAXREVERBPROPERTIES::* field,
								 const wsw::StaticVector<const EFXEAXREVERBPROPERTIES *, 4> &parts ) {
		assert( !parts.empty() );
		float value = 0.0f;
		for( const EFXEAXREVERBPROPERTIES *preset: parts ) {
			value += preset->*field;
		}
		return value / (float)parts.size();
	}

	[[nodiscard]]
	auto getByString( const wsw::StringView &string ) const -> const EFXEAXREVERBPROPERTIES * {
		if( string.indexOf( ' ' ) == std::nullopt ) {
			return EfxPresetsRegistry::s_instance.findByName( string );
		} else {
			wsw::StringSplitter splitter( string );
			wsw::StaticVector<const EFXEAXREVERBPROPERTIES *, 4> parts;
			while( const auto maybeToken = splitter.getNext( ' ' ) ) {
				if( !parts.full() ) {
					if( const auto *preset = EfxPresetsRegistry::s_instance.findByName( *maybeToken )) {
						parts.push_back( preset );
					} else {
						return nullptr;
					}
				} else {
					return nullptr;
				}
			}
			if( !parts.empty() ) {
				// Note: Unused fields are not processed
				m_mix.flDensity             = mixFieldOfParts( &EFXEAXREVERBPROPERTIES::flDensity, parts );
				m_mix.flDiffusion           = mixFieldOfParts( &EFXEAXREVERBPROPERTIES::flDiffusion, parts );
				m_mix.flGain                = mixFieldOfParts( &EFXEAXREVERBPROPERTIES::flGain, parts );
				m_mix.flGainLF              = mixFieldOfParts( &EFXEAXREVERBPROPERTIES::flGainLF, parts );
				m_mix.flGainHF              = mixFieldOfParts( &EFXEAXREVERBPROPERTIES::flGainHF, parts );
				m_mix.flDecayTime           = mixFieldOfParts( &EFXEAXREVERBPROPERTIES::flDecayTime, parts );
				m_mix.flDecayHFRatio        = mixFieldOfParts( &EFXEAXREVERBPROPERTIES::flDecayHFRatio, parts );
				m_mix.flDecayLFRatio        = mixFieldOfParts( &EFXEAXREVERBPROPERTIES::flDecayLFRatio, parts );
				m_mix.flReflectionsGain     = mixFieldOfParts( &EFXEAXREVERBPROPERTIES::flReflectionsGain, parts );
				m_mix.flReflectionsDelay    = mixFieldOfParts( &EFXEAXREVERBPROPERTIES::flReflectionsDelay, parts );
				m_mix.flLateReverbGain      = mixFieldOfParts( &EFXEAXREVERBPROPERTIES::flLateReverbGain, parts );
				m_mix.flLateReverbDelay     = mixFieldOfParts( &EFXEAXREVERBPROPERTIES::flLateReverbDelay, parts );
				m_mix.flEchoTime            = mixFieldOfParts( &EFXEAXREVERBPROPERTIES::flEchoTime, parts );
				m_mix.flEchoDepth           = mixFieldOfParts( &EFXEAXREVERBPROPERTIES::flEchoDepth, parts );
				m_mix.flAirAbsorptionGainHF = mixFieldOfParts( &EFXEAXREVERBPROPERTIES::flAirAbsorptionGainHF, parts );
				m_mix.flLFReference         = mixFieldOfParts( &EFXEAXREVERBPROPERTIES::flLFReference, parts );
				m_mix.flHFReference         = mixFieldOfParts( &EFXEAXREVERBPROPERTIES::flHFReference, parts );
				return &m_mix;
			} else {
				return nullptr;
			}
		}
	}

	const char *const m_varName;
	const char *const m_defaultValue;
	mutable cvar_t *m_var { nullptr };
	mutable const EFXEAXREVERBPROPERTIES *m_preset { nullptr };
	mutable EFXEAXREVERBPROPERTIES m_mix {};
};

static CachedPresetTracker g_tinyOpenRoomPreset { "s_tinyOpenRoomPreset", "quarry" };
static CachedPresetTracker g_tinyClosedRoomPreset { "s_tinyClosedRoomPreset", "hallway" };
static CachedPresetTracker g_hugeOpenRoomPreset { "s_hugeOpenRoomPreset", "outdoors_rollingplains" };
static CachedPresetTracker g_hugeClosedRoomPreset { "s_hugeClosedRoomPreset", "city_library" };

void ReverbEffectSampler::ProcessPrimaryEmissionResults() {
	// Instead of trying to compute these factors every sampling call,
	// reuse pre-computed properties of CM map leafs that briefly resemble rooms/convex volumes.
	assert( src->envUpdateState.leafNum >= 0 );

	const auto *const leafPropsCache = LeafPropsCache::Instance();
	const LeafProps &leafProps = leafPropsCache->GetPropsForLeaf( src->envUpdateState.leafNum );

	const LerpPresetHelper helper {
		.tinyOpenRoomPreset   = g_tinyOpenRoomPreset.getPreset(),
		.tinyClosedRoomPreset = g_tinyClosedRoomPreset.getPreset(),
		.hugeOpenRoomPreset   = g_hugeOpenRoomPreset.getPreset(),
		.hugeClosedRoomPreset = g_hugeClosedRoomPreset.getPreset(),
		.skyFrac              = leafProps.getSkyFactor(),
		.sizeFrac             = leafProps.getRoomSizeFactor(),
	};

	effect->gain   = helper.calcBiLerpValue( &EFXEAXREVERBPROPERTIES::flGain );
	effect->gainHf = helper.calcBiLerpValue( &EFXEAXREVERBPROPERTIES::flGainHF );
	effect->gainLf = helper.calcBiLerpValue( &EFXEAXREVERBPROPERTIES::flGainLF );

	effect->diffusion = helper.calcBiLerpValue( &EFXEAXREVERBPROPERTIES::flDiffusion );
	effect->decayTime = helper.calcBiLerpValue( &EFXEAXREVERBPROPERTIES::flDecayTime );

	effect->decayHfRatio = helper.calcBiLerpValue( &EFXEAXREVERBPROPERTIES::flDecayHFRatio );
	effect->decayLfRatio = helper.calcBiLerpValue( &EFXEAXREVERBPROPERTIES::flDecayLFRatio );

	effect->reflectionsGain  = helper.calcBiLerpValue( &EFXEAXREVERBPROPERTIES::flReflectionsGain );
	effect->reflectionsDelay = helper.calcBiLerpValue( &EFXEAXREVERBPROPERTIES::flReflectionsDelay );

	effect->lateReverbGain  = helper.calcBiLerpValue( &EFXEAXREVERBPROPERTIES::flLateReverbGain );
	effect->lateReverbDelay = helper.calcBiLerpValue( &EFXEAXREVERBPROPERTIES::flLateReverbDelay );

	effect->echoTime = helper.calcBiLerpValue( &EFXEAXREVERBPROPERTIES::flEchoTime );
	effect->echoDepth = helper.calcBiLerpValue( &EFXEAXREVERBPROPERTIES::flEchoDepth );

	effect->lfReference = helper.calcBiLerpValue( &EFXEAXREVERBPROPERTIES::flLFReference );
	effect->hfReference = helper.calcBiLerpValue( &EFXEAXREVERBPROPERTIES::flHFReference );

	effect->airAbsorptionGainHf = helper.calcBiLerpValue( &EFXEAXREVERBPROPERTIES::flAirAbsorptionGainHF );

	// Custom parameters

	effect->density = 1.0f - 0.7f * leafProps.getMetallnessFactor();

	// 0.5 is the value of a neutral surface
	const float smoothness = leafProps.getSmoothnessFactor();
	if( smoothness <= 0.5f ) {
		const float frac = 2.0f * smoothness;
		assert( frac >= 0.0f && frac <= 1.0f );
		effect->hfReference = std::lerp( 1000.0f, 2500.0f, frac );
	} else {
		// The high HF reference is unpleasant for ears
		// Use the quadratic curve, so it kicks in only in special kinds of environment.
		const float frac = wsw::square( 2.0f * ( smoothness - 0.5f ) );
		assert( frac >= 0.0f && frac <= 1.0f );
		effect->hfReference = std::lerp( 2500.0f, 4000.0f, frac );
	}

	// Tune it down
	effect->lateReverbGain = wsw::min( 1.0f, effect->lateReverbGain );
	effect->gain *= 0.7f;
}

void ReverbEffectSampler::EmitSecondaryRays() {
	int listenerLeafNum = listenerProps->GetLeafNum();

	auto *const panningUpdateState = &src->panningUpdateState;
	panningUpdateState->numPrimaryRays = numPrimaryRays;

	trace_t trace;

	unsigned numPassedSecondaryRays = 0;
	panningUpdateState->numPassedSecondaryRays = 0;
	for( unsigned i = 0; i < numPrimaryHits; i++ ) {
		// Cut off by PVS system early, we are not interested in actual ray hit points contrary to the primary emission.
		if( !S_LeafsInPVS( listenerLeafNum, S_PointLeafNum( reflectionPoints[i] ) ) ) {
			continue;
		}

		S_Trace( &trace, reflectionPoints[i], testedListenerOrigin, vec3_origin, vec3_origin, MASK_SOLID );
		if( trace.fraction == 1.0f && !trace.startsolid ) {
			numPassedSecondaryRays++;
			float *savedPoint = panningUpdateState->reflectionPoints[panningUpdateState->numPassedSecondaryRays++];
			VectorCopy( reflectionPoints[i], savedPoint );
		}
	}

	if( numPrimaryHits ) {
		float frac = numPassedSecondaryRays / (float)numPrimaryHits;
		// The secondary rays obstruction is complement to the `frac`
		effect->secondaryRaysObstruction = 1.0f - frac;
	} else {
		// Set minimal feasible values
		effect->secondaryRaysObstruction = 1.0f;
	}
}