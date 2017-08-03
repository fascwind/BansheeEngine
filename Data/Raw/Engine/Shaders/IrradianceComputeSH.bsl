#include "$ENGINE$\ReflectionCubemapCommon.bslinc"
#include "$ENGINE$\SHCommon.bslinc"

technique IrradianceComputeSH
{
	mixin ReflectionCubemapCommon;
	mixin SHCommon;

	code
	{
		struct SHCoeffsAndWeight
		{
			#if ORDER == 3
			SHVector3RGB coeffs;
			#else // Assuming order 5
			SHVector5RGB coeffs;
			#endif
			float weight;
		};
	
		SamplerState gInputSamp;
		TextureCube gInputTex;
	
		RWStructuredBuffer<SHCoeffsAndWeight> gOutput;
		
		[internal]
		cbuffer Params
		{
			uint gCubeFace;
			uint gFaceSize;
			uint2 gDispatchSize;
		}			
		
		groupshared SHCoeffsAndWeight sCoeffs[TILE_WIDTH * TILE_HEIGHT];

		/** 
		 * Integrates area of a cube face projected onto the surface of the sphere, from [0, 0] to [u, v]. 
		 * u & v expected in [-1, -1] to [1, 1] range.
		 *
		 * See http://www.rorydriscoll.com/2012/01/15/cubemap-texel-solid-angle/ for derivation.
		 */
		float integrateProjectedCubeArea(float u, float v)
		{
			return atan2(u * v, sqrt(u * u + v * v + 1.0f));
		}
		
		/** Calculates solid angle of a texel projected onto a sphere. */
		float texelSolidAngle(float u, float v, float invFaceSize)
		{
			float x0 = u - invFaceSize;
			float x1 = u + invFaceSize;
			float y0 = v - invFaceSize;
			float y1 = v + invFaceSize;

			return   integrateProjectedCubeArea(x1, y1)
				   - integrateProjectedCubeArea(x0, y1)
				   - integrateProjectedCubeArea(x1, y0)
				   + integrateProjectedCubeArea(x0, y0);
		}
		
		[numthreads(TILE_WIDTH, TILE_HEIGHT, 1)]
		void csmain(
			uint groupIdx : SV_GroupIndex,
			uint3 groupId : SV_GroupID,
			uint3 dispatchThreadId : SV_DispatchThreadID)
		{
			SHCoeffsAndWeight data;
			data.weight = 0;
			
			SHZero(data.coeffs.R);
			SHZero(data.coeffs.G);
			SHZero(data.coeffs.B);
			
			float invFaceSize = 1.0f / gFaceSize;
		
			uint2 pixelCoords = dispatchThreadId.xy * PIXELS_PER_THREAD;
			uint2 pixelCoordsEnd = pixelCoords + uint2(PIXELS_PER_THREAD, PIXELS_PER_THREAD);
			for(uint y = pixelCoords.y; y < pixelCoordsEnd.y; y++)
			{
				for(uint x = pixelCoords.x; x < pixelCoordsEnd.x; x++)
				{
					// Ignore pixels out of valid range
					if (x >= gFaceSize || y >= gFaceSize)
						break;
						
					// Map from [0, size-1] to [-1.0 + invSize, 1.0 - invSize].
					// (+0.5 in order to sample center of texel)
					float u = 2.0f * (x + 0.5f) * invFaceSize - 1.0f;
					float v = 2.0f * (y + 0.5f) * invFaceSize - 1.0f;
					
					float3 dir = getDirFromCubeFace(gCubeFace, float2(u, v));
					dir = normalize(dir);
					
					// Need to calculate solid angle (weight) of the texel, as cube face corners have
					// much smaller solid angle, meaning many of them occupy the same area when projected
					// on a sphere. Without weighing that area would look too bright.
					float weight = texelSolidAngle(u, v, invFaceSize);
					
					#if ORDER == 3
					SHVector3 shBasis = SHBasis3(dir);
					#else // Assuming order 5
					SHVector5 shBasis = SHBasis5(dir);
					#endif
					float3 radiance = gInputTex.SampleLevel(gInputSamp, dir, 0).rgb;
					
					SHMultiplyAdd(data.coeffs.R, shBasis, radiance.r * weight);
					SHMultiplyAdd(data.coeffs.G, shBasis, radiance.g * weight);
					SHMultiplyAdd(data.coeffs.B, shBasis, radiance.b * weight);
					
					data.weight += weight;
				}
			}
			
			sCoeffs[groupIdx] = data;
			
			GroupMemoryBarrierWithGroupSync();
			
			int numThreads = TILE_WIDTH * TILE_HEIGHT;
			[unroll]
			for(int tc = numThreads / 2; tc > 0; tc >>= 1)
			{
				if(groupIdx < tc)
				{
					SHAdd(sCoeffs[groupIdx].coeffs.R, sCoeffs[groupIdx + tc].coeffs.R);
					SHAdd(sCoeffs[groupIdx].coeffs.G, sCoeffs[groupIdx + tc].coeffs.G);
					SHAdd(sCoeffs[groupIdx].coeffs.B, sCoeffs[groupIdx + tc].coeffs.B);

					sCoeffs[groupIdx].weight += sCoeffs[groupIdx + tc].weight;
				}
			
				GroupMemoryBarrierWithGroupSync();
			}
			
			if(groupIdx == 0)
			{
				uint faceOffset = gDispatchSize.x * gDispatchSize.y * gCubeFace;
				uint outputIdx = faceOffset + groupId.y * gDispatchSize.x + groupId.x;
				gOutput[outputIdx] = sCoeffs[0];
			}
		}
	};
};