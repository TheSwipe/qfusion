/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2002-2013 Victor Luchits

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

#ifndef WSW_63ccf348_3b16_4f9c_9a49_cd5849918618_H
#define WSW_63ccf348_3b16_4f9c_9a49_cd5849918618_H

#include <memory>
#include <span>

struct alignas( 32 )Frustum {
	alignas( 32 ) float planeX[8];
	alignas( 32 ) float planeY[8];
	alignas( 32 ) float planeZ[8];
	alignas( 32 ) float planeD[8];
	alignas( 32 ) uint32_t xBlendMasks[8];
	alignas( 32 ) uint32_t yBlendMasks[8];
	alignas( 32 ) uint32_t zBlendMasks[8];

	void setPlaneComponentsAtIndex( unsigned index, const float *n, float d );

	// Call after setting up 4th+ planes
	void fillComponentTails( unsigned numPlanesSoFar );

	void setupFor4Planes( const float *viewOrigin, const mat3_t viewAxis, float fovX, float fovY );
};

namespace wsw::ref {

class alignas( 32 ) Frontend {
private:
	shader_t *m_coronaShader;
	shader_t *m_particleShader;

	static constexpr unsigned kMaxLightsInScene = 1024;
	static constexpr unsigned kMaxProgramLightsInView = 32;

	int m_coronaDrawSurfaces[kMaxLightsInScene];
	unsigned m_numVisibleProgramLights { 0 };
	uint16_t m_programLightIndices[kMaxProgramLightsInView];
	struct { float mins[8], maxs[8]; } m_lightBoundingDops[kMaxProgramLightsInView];

	std::unique_ptr<ParticleDrawSurface[]> m_particleDrawSurfaces {
		std::make_unique<ParticleDrawSurface[]>( Scene::kMaxParticlesInAggregate * Scene::kMaxParticleAggregates )
	};

	refinst_t m_state;
	// TODO: Put in the state
	Frustum m_frustum;

	unsigned m_occludersSelectionFrame { 0 };
	unsigned m_occlusionCullingFrame { 0 };

	wsw::StaticVector<DrawSceneRequest, 1> m_drawSceneRequestHolder;

	struct DebugLine {
		float p1[3];
		float p2[3];
		int color;
	};
	wsw::Vector<DebugLine> m_debugLines;

	wsw::Vector<sortedDrawSurf_t> m_meshDrawList;

	template <typename T>
	struct BufferHolder {
		std::unique_ptr<T[]> data;
		unsigned capacity { 0 };

		void reserve( size_t newSize ) {
			if( newSize > capacity ) [[unlikely]] {
				data = std::make_unique<T[]>( newSize );
				capacity = newSize;
			}
		}

		void reserveZeroed( size_t newSize ) {
			if( newSize > capacity ) [[unlikely]] {
				data = std::make_unique<T[]>( newSize );
				capacity = newSize;
			}
			std::memset( data.get(), 0, sizeof( T ) * newSize );
		}
	};

	struct SortedOccluder {
		unsigned surfNum;
		float score;
		[[nodiscard]]
		bool operator<( const SortedOccluder &that ) const { return score > that.score; }
	};

	BufferHolder<unsigned> m_visibleLeavesBuffer;
	BufferHolder<unsigned> m_occluderPassFullyVisibleLeavesBuffer;
	BufferHolder<unsigned> m_occluderPassPartiallyVisibleLeavesBuffer;

	BufferHolder<SortedOccluder> m_visibleOccludersBuffer;

	Frustum m_occluderFrusta[64];

	struct MergedSurfSpan {
		int firstSurface;
		int lastSurface;
	};

	BufferHolder<MergedSurfSpan> m_drawSurfSurfSpans;

	struct VisTestedModel {
		// TODO: Pass lod number?
		const model_t *selectedLod;
		vec3_t absMins, absMaxs;
		unsigned indexInEntitiesGroup;
	};

	BufferHolder<VisTestedModel> m_visTestedModelsBuffer;

	BufferHolder<uint32_t> m_leafLightBitsOfSurfacesHolder;

	[[nodiscard]]
	auto getFogForBounds( const float *mins, const float *maxs ) -> mfog_t *;
	[[nodiscard]]
	auto getFogForSphere( const vec3_t centre, const float radius ) -> mfog_t *;
	[[nodiscard]]
	bool isPointCompletelyFogged( const mfog_t *fog, const float *origin, float radius );

	void bindFrameBuffer( int );

	[[nodiscard]]
	auto getDefaultFarClip() const -> float;

	void renderViewFromThisCamera( Scene *scene, const refdef_t *fd );

	[[nodiscard]]
	auto tryAddingPortalSurface( const entity_t *ent, const shader_t *shader, void *drawSurf ) -> portalSurface_t *;

	[[nodiscard]]
	auto tryUpdatingPortalSurfaceAndDistance( drawSurfaceBSP_t *drawSurf, const msurface_t *surf, const float *origin ) -> std::optional<float>;

	void updatePortalSurface( portalSurface_t *portalSurface, const mesh_t *mesh,
							  const float *mins, const float *maxs, const shader_t *shader, void *drawSurf );

	void collectVisiblePolys( Scene *scene, std::span<const Frustum> frusta );

	[[nodiscard]]
	auto cullWorldSurfaces() -> std::tuple<std::span<const Frustum>, std::span<const unsigned>, std::span<const unsigned>>;

	void addVisibleWorldSurfacesToSortList( Scene *scene );

	void collectVisibleEntities( Scene *scene, std::span<const Frustum> frusta );

	[[nodiscard]]
	auto collectVisibleLights( Scene *scene, std::span<const Frustum> frusta ) -> std::span<const uint16_t>;

	[[nodiscard]]
	auto cullNullModelEntities( std::span<const entity_t> nullModelEntities,
								const Frustum *__restrict primaryFrustum,
								std::span<const Frustum> occluderFrusta,
								uint16_t *tmpIndices )
								-> std::span<const uint16_t>;

	[[nodiscard]]
	auto cullAliasModelEntities( std::span<const entity_t> aliasModelEntities,
								 const Frustum *__restrict primaryFrustum,
								 std::span<const Frustum> occluderFrusta,
								 VisTestedModel *tmpBuffer )
								 -> std::span<VisTestedModel>;

	[[nodiscard]]
	auto cullSkeletalModelEntities( std::span<const entity_t> skeletalModelEntities,
									const Frustum *__restrict primaryFrustum,
									std::span<const Frustum> occluderFrusta,
									VisTestedModel *tmpBuffer )
									-> std::span<VisTestedModel>;

	[[nodiscard]]
	auto cullBrushModelEntities( std::span<const entity_t> brushModelEntities,
								 const Frustum *__restrict primaryFrustum,
								 std::span<const Frustum> occluderFrusta,
								 uint16_t *tmpIndices )
								 -> std::span<const uint16_t>;

	[[nodiscard]]
	auto cullSpriteEntities( std::span<const entity_t> spriteEntities,
							 const Frustum *__restrict primaryFrustum,
							 std::span<const Frustum> occluderFrusta,
							 uint16_t *tmpIndices )
							 -> std::span<const uint16_t>;

	[[nodiscard]]
	auto cullLights( std::span<const Scene::DynamicLight> lights,
					 const Frustum *__restrict primaryFrustum,
					 std::span<const Frustum> occluderFrusta,
					 uint16_t *tmpCoronaLightIndices,
					 uint16_t *tmpProgramLightIndices )
					 -> std::pair<std::span<const uint16_t>, std::span<const uint16_t>>;

	void collectVisibleParticles( Scene *scene, std::span<const Frustum> frusta );

	[[nodiscard]]
	auto cullParticleAggregates( std::span<const Scene::ParticlesAggregate> aggregates,
								 const Frustum *__restrict primaryFrustum,
								 std::span<const Frustum> occluderFrusta,
								 uint16_t *tmpIndices ) -> std::span<const uint16_t>;

	void collectVisibleExternalMeshes( Scene *scene, std::span<const Frustum> frusta );

	[[nodiscard]]
	auto cullExternalMeshes( std::span<const Scene::ExternalCompoundMesh> meshes,
							 const Frustum *__restrict primaryFrustum,
							 std::span<const Frustum> occluderFrusta,
							 uint16_t *tmpIndices ) -> std::span<const uint16_t>;

	// TODO: Check why spans can't be supplied
	[[nodiscard]]
	auto cullQuadPolys( QuadPoly **polys, unsigned numPolys,
						const Frustum *__restrict primaryFrustum,
						std::span<const Frustum> occluderFrusta,
						uint16_t *tmpIndices ) -> std::span<const uint16_t>;

	// TODO: Check why spans can't be supplied
	[[nodiscard]]
	auto cullComplexPolys( ComplexPoly **polys, unsigned numPolys,
						   const Frustum *__restrict primaryFrustum,
						   std::span<const Frustum> occluderFrusta,
						   uint16_t *tmpIndices ) -> std::span<const uint16_t>;

	void addAliasModelEntitiesToSortList( const entity_t *aliasModelEntities, std::span<VisTestedModel> indices );
	void addSkeletalModelEntitiesToSortList( const entity_t *skeletalModelEntities, std::span<VisTestedModel> indices );

	void addNullModelEntitiesToSortList( const entity_t *nullModelEntities, std::span<const uint16_t> indices );
	void addBrushModelEntitiesToSortList( const entity_t *brushModelEntities, std::span<const uint16_t> indices,
										  std::span<const Scene::DynamicLight> lights );
	void addSpriteEntitiesToSortList( const entity_t *spriteEntities, std::span<const uint16_t> indices );

	void addParticlesToSortList( const entity_t *particleEntity, const Scene::ParticlesAggregate *particles,
								 std::span<const uint16_t> aggregateIndices );

	void addExternalMeshesToSortList( const entity_t *meshEntity, const Scene::ExternalCompoundMesh *meshes,
									  std::span<const uint16_t> indicesOfMeshes );

	void addCoronaLightsToSortList( const entity_t *polyEntity, const Scene::DynamicLight *lights,
									std::span<const uint16_t> indices );

	void addDebugLine( const float *p1, const float *p2, int color = COLOR_RGB( 255, 255, 255 ) );

	void submitDebugStuffToBackend( Scene *scene );

	[[nodiscard]]
	auto collectVisibleWorldLeaves() -> std::span<const unsigned>;
	[[nodiscard]]
	auto collectVisibleOccluders( std::span<const unsigned> visibleLeaves ) -> std::span<const SortedOccluder>;
	[[nodiscard]]
	auto buildFrustaOfOccluders( std::span<const SortedOccluder> sortedOccluders ) -> std::span<const Frustum>;

	void cullSurfacesInVisLeavesByOccluders( std::span<const unsigned> indicesOfLeaves,
											 std::span<const Frustum> occluderFrusta,
											 MergedSurfSpan *mergedSurfSpans );

	void markSurfacesOfLeavesAsVisible( std::span<const unsigned> indicesOfLeaves, MergedSurfSpan *mergedSurfSpans );

	[[nodiscard]]
	auto cullLeavesByOccluders( std::span<const unsigned> indicesOfLeaves,
								std::span<const Frustum> occluderFrusta )
								-> std::pair<std::span<const unsigned>, std::span<const unsigned>>;

	void markLightsOfSurfaces( const Scene *scene,
							   std::span<std::span<const unsigned>> spansOfLeaves,
							   std::span<const uint16_t> visibleLightIndices );

	void markLightsOfLeaves( const Scene *scene,
							 std::span<const unsigned> indicesOfLeaves,
							 std::span<const uint16_t> visibleLightIndices,
							 unsigned *lightBitsOfSurfaces );

	void setupViewMatrices();
	void clearActiveFrameBuffer();

	void addMergedBspSurfToSortList( const entity_t *entity, drawSurfaceBSP_t *drawSurf,
									 msurface_t *firstVisSurf, msurface_t *lastVisSurf,
									 const float *maybeOrigin, std::span<const Scene::DynamicLight> lights );

	void *addEntryToSortList( const entity_t *e, const mfog_t *fog, const shader_t *shader,
							  float dist, unsigned order, const portalSurface_t *portalSurf,
							  const void *drawSurf, unsigned surfType );

	void submitSortedSurfacesToBackend( Scene *scene );
public:
	Frontend();

	static void init();
	static void shutdown();

	[[nodiscard]]
	static auto instance() -> Frontend *;

	[[nodiscard]]
	auto createDrawSceneRequest( const refdef_t &refdef ) -> DrawSceneRequest *;
	void submitDrawSceneRequest( DrawSceneRequest *request );

	void initVolatileAssets();

	void destroyVolatileAssets();

	void renderScene( Scene *scene, const refdef_t *rd );

	void set2DMode( bool enable );

	void dynLightDirForOrigin( const float *origin, float radius, vec3_t dir, vec3_t diffuseLocal, vec3_t ambientLocal );
};

}

#endif