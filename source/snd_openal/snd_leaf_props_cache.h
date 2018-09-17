#ifndef QFUSION_SND_LEAF_PROPS_CACHE_H
#define QFUSION_SND_LEAF_PROPS_CACHE_H

#include "snd_cached_computation.h"

class LeafPropsSampler;
class LeafPropsReader;

struct alignas( 4 )LeafProps {
	uint8_t roomSizeFactor;
	uint8_t skyFactor;
	uint8_t waterFactor;
	uint8_t metalFactor;

	static float PackValue( float value ) { return (uint8_t)( value * 255 ); }
	static float UnpackValue( uint8_t packed ) { return packed / 255.0f; }

#define MK_ACCESSORS( accessorName, fieldName )                                             \
	float accessorName() const { return UnpackValue( fieldName ); }                         \
	void Set##accessorName( float fieldName##_ ) { fieldName = PackValue( fieldName##_ ); }

	MK_ACCESSORS( RoomSizeFactor, roomSizeFactor );
	MK_ACCESSORS( SkyFactor, skyFactor );
	MK_ACCESSORS( WaterFactor, waterFactor );
	MK_ACCESSORS( MetalFactor, metalFactor );

#undef MK_ACCESSORS
};

class LeafPropsCache: public CachedComputation {
	template <typename> friend class SingletonHolder;

	LeafProps *leafProps { nullptr };

	LeafProps ComputeLeafProps( int leafNum, LeafPropsSampler *sampler, bool fastAndCoarse );

	bool TryReadFromFile( LeafPropsReader *reader, int actualLeafsNum );

	void ResetExistingState( const char *actualMap, int actualNumLeafs ) override;
	bool TryReadFromFile( const char *actualMap, const char *actualChecksum, int actualNumLeafs, int fsFlags ) override;
	void ComputeNewState( const char *actualMap, int actualNumLeafs, bool fastAndCoarse ) override;
	bool SaveToCache( const char *actualMap, const char *actualChecksum, int actualNumLeafs ) override;

	LeafPropsCache(): CachedComputation( "LeafPropsCache" ) {}
public:
	static LeafPropsCache *Instance();
	static void Init();
	static void Shutdown();

	~LeafPropsCache() override {
		if( leafProps ) {
			S_Free( leafProps );
		}
	}

	const LeafProps &GetPropsForLeaf( int leafNum ) const {
		return leafProps[leafNum];
	}
};

#endif
