//********************************** Banshee Engine (www.banshee3d.com) **************************************************//
//**************** Copyright (c) 2016 Marko Pintera (marko.pintera@gmail.com). All rights reserved. **********************//
#include "BsLightProbeVolume.h"
#include "BsLightProbeVolumeRTTI.h"
#include "BsFrameAlloc.h"
#include "BsRenderer.h"
#include "BsLight.h"
#include "BsGpuBuffer.h"
#include "BsTexture.h"
#include "BsIBLUtility.h"

namespace bs
{
	LightProbeVolumeBase::LightProbeVolumeBase()
		: mPosition(BsZero), mRotation(BsIdentity), mIsActive(true)
	{ }

	LightProbeVolume::LightProbeVolume()
	{ }

	LightProbeVolume::LightProbeVolume(const AABox& volume, const Vector3& density)
	{
		// TODO - Generates probes in the grid volume
	}

	LightProbeVolume::~LightProbeVolume()
	{
		if (mRendererTask)
			mRendererTask->cancel();
	}

	UINT32 LightProbeVolume::addProbe(const Vector3& position)
	{
		UINT32 handle = mNextProbeId++;
		mProbes[handle] = ProbeInfo(LightProbeFlags::Clean, position);

		_markCoreDirty();
		return handle;
	}

	void LightProbeVolume::removeProbe(UINT32 handle)
	{
		auto iterFind = mProbes.find(handle);
		if (iterFind != mProbes.end() && mProbes.size() > 4)
		{
			iterFind->second.flags = LightProbeFlags::Removed;
			_markCoreDirty();
		}
	}

	void LightProbeVolume::setProbePosition(UINT32 handle, const Vector3& position)
	{
		auto iterFind = mProbes.find(handle);
		if (iterFind != mProbes.end())
		{
			iterFind->second.position = position;
			_markCoreDirty();
		}
	}

	Vector3 LightProbeVolume::getProbePosition(UINT32 handle) const
	{
		auto iterFind = mProbes.find(handle);
		if (iterFind != mProbes.end())
			return iterFind->second.position;
		
		return Vector3::ZERO;
	}

	void LightProbeVolume::renderProbe(UINT32 handle)
	{
		auto iterFind = mProbes.find(handle);
		if (iterFind != mProbes.end())
		{
			if (iterFind->second.flags == LightProbeFlags::Clean)
			{
				iterFind->second.flags = LightProbeFlags::Dirty;

				_markCoreDirty();
				runRenderProbeTask();
			}
		}
	}

	void LightProbeVolume::renderProbes()
	{
		bool anyModified = false;
		for(auto& entry : mProbes)
		{
			if (entry.second.flags == LightProbeFlags::Clean)
			{
				entry.second.flags = LightProbeFlags::Dirty;
				anyModified = true;
			}
		}

		if (anyModified)
		{
			_markCoreDirty();
			runRenderProbeTask();
		}
	}

	void LightProbeVolume::runRenderProbeTask()
	{
		// If a task is already running cancel it
		// Note: If the task is just about to start processing, cancelling it will skip the update this frame
		// (which might be fine if we just changed positions of dirty probes it was about to update, but it might also
		// waste a frame if those positions needed to be updated anyway). For now I'm ignoring it as it seems like a rare
		// enough situation, plus it's one that will only happen during development time.
		if (mRendererTask)
			mRendererTask->cancel();

		auto renderComplete = [this]()
		{
			mRendererTask = nullptr;
		};

		SPtr<ct::LightProbeVolume> coreProbeVolume = getCore();
		auto renderProbes = [coreProbeVolume]()
		{
			return coreProbeVolume->renderProbes(3);
		};

		mRendererTask = ct::RendererTask::create("RenderLightProbes", renderProbes);

		mRendererTask->onComplete.connect(renderComplete);
		ct::gRenderer()->addTask(mRendererTask);
	}

	void LightProbeVolume::updateCoefficients()
	{
		// Ensure all light probe coefficients are generated
		if (mRendererTask)
			mRendererTask->wait();

		ct::LightProbeVolume* coreVolume = getCore().get();

		Vector<LightProbeCoefficientInfo> coeffInfo;
		auto getSaveData = [coreVolume, &coeffInfo]()
		{
			coreVolume->getProbeCoefficients(coeffInfo);
		};

		gCoreThread().queueCommand(getSaveData);
		gCoreThread().submit(true);

		for(auto& entry : coeffInfo)
		{
			auto iterFind = mProbes.find(entry.handle);
			if (iterFind == mProbes.end())
				continue;

			iterFind->second.coefficients = entry.coefficients;
		}
	}

	SPtr<ct::LightProbeVolume> LightProbeVolume::getCore() const
	{
		return std::static_pointer_cast<ct::LightProbeVolume>(mCoreSpecific);
	}

	SPtr<LightProbeVolume> LightProbeVolume::create(const AABox& volume, const Vector3& density)
	{
		LightProbeVolume* probeVolume = new (bs_alloc<LightProbeVolume>()) LightProbeVolume(volume, density);
		SPtr<LightProbeVolume> probeVolumePtr = bs_core_ptr<LightProbeVolume>(probeVolume);
		probeVolumePtr->_setThisPtr(probeVolumePtr);
		probeVolumePtr->initialize();

		return probeVolumePtr;
	}

	SPtr<LightProbeVolume> LightProbeVolume::createEmpty()
	{
		LightProbeVolume* probeVolume = new (bs_alloc<LightProbeVolume>()) LightProbeVolume();
		SPtr<LightProbeVolume> probleVolumePtr = bs_core_ptr<LightProbeVolume>(probeVolume);
		probleVolumePtr->_setThisPtr(probleVolumePtr);

		return probleVolumePtr;
	}

	SPtr<ct::CoreObject> LightProbeVolume::createCore() const
	{
		ct::LightProbeVolume* handler = new (bs_alloc<ct::LightProbeVolume>()) ct::LightProbeVolume(mProbes);
		SPtr<ct::LightProbeVolume> handlerPtr = bs_shared_ptr<ct::LightProbeVolume>(handler);
		handlerPtr->_setThisPtr(handlerPtr);

		return handlerPtr;
	}

	CoreSyncData LightProbeVolume::syncToCore(FrameAlloc* allocator)
	{
		UINT32 size = 0;
		UINT8* buffer = nullptr;

		bs_frame_mark();
		{
			FrameVector<std::pair<UINT32, ProbeInfo>> dirtyProbes;
			FrameVector<UINT32> removedProbes;
			for (auto& probe : mProbes)
			{
				if (probe.second.flags == LightProbeFlags::Dirty)
				{
					dirtyProbes.push_back(std::make_pair(probe.first, probe.second));
					probe.second.flags = LightProbeFlags::Clean;
				}
				else if (probe.second.flags == LightProbeFlags::Removed)
				{
					removedProbes.push_back(probe.first);
					probe.second.flags = LightProbeFlags::Empty;
				}
			}

			for (auto& probe : removedProbes)
				mProbes.erase(probe);

			UINT32 numDirtyProbes = (UINT32)dirtyProbes.size();
			UINT32 numRemovedProbes = (UINT32)removedProbes.size();

			size += rttiGetElemSize(mPosition);
			size += rttiGetElemSize(mRotation);
			size += rttiGetElemSize(mIsActive);
			size += rttiGetElemSize(numDirtyProbes);
			size += rttiGetElemSize(numRemovedProbes);
			size += (sizeof(UINT32) + sizeof(Vector3) + sizeof(LightProbeFlags)) * numDirtyProbes;
			size += sizeof(UINT32) * numRemovedProbes;

			buffer = allocator->alloc(size);

			char* dataPtr = (char*)buffer;
			dataPtr = rttiWriteElem(mPosition, dataPtr);
			dataPtr = rttiWriteElem(mRotation, dataPtr);
			dataPtr = rttiWriteElem(mIsActive, dataPtr);
			dataPtr = rttiWriteElem(numDirtyProbes, dataPtr);
			dataPtr = rttiWriteElem(numRemovedProbes, dataPtr);

			for (auto& entry : dirtyProbes)
			{
				dataPtr = rttiWriteElem(entry.first, dataPtr);
				dataPtr = rttiWriteElem(entry.second.position, dataPtr);
				dataPtr = rttiWriteElem(entry.second.flags, dataPtr);
			}

			for(auto& entry : removedProbes)
				dataPtr = rttiWriteElem(entry, dataPtr);
		}
		bs_frame_clear();

		return CoreSyncData(buffer, size);
	}

	void LightProbeVolume::_markCoreDirty()
	{
		markCoreDirty();
	}

	RTTITypeBase* LightProbeVolume::getRTTIStatic()
	{
		return LightProbeVolumeRTTI::instance();
	}

	RTTITypeBase* LightProbeVolume::getRTTI() const
	{
		return LightProbeVolume::getRTTIStatic();
	}

	namespace ct
	{
	LightProbeVolume::LightProbeVolume(const UnorderedMap<UINT32, bs::LightProbeVolume::ProbeInfo>& probes)
	{
		mInitCoefficients.resize(probes.size());

		UINT32 probeIdx = 0;
		for(auto& entry : probes)
		{
			mProbeMap[entry.first] = probeIdx;
			mProbePositions[probeIdx] = entry.second.position;
			
			LightProbeInfo probeInfo;
			probeInfo.flags = LightProbeFlags::Dirty;
			probeInfo.bufferIdx = probeIdx;
			probeInfo.handle = entry.first;

			mProbeInfos[probeIdx] = probeInfo;
			mInitCoefficients[probeIdx] = entry.second.coefficients;

			probeIdx++;
		}
	}

	LightProbeVolume::~LightProbeVolume()
	{
		gRenderer()->notifyLightProbeVolumeRemoved(this);
	}

	void LightProbeVolume::initialize()
	{
		// Set SH coefficients loaded from the file
		UINT32 numCoefficients = (UINT32)mInitCoefficients.size();
		assert(mInitCoefficients.size() == mProbeMap.size());

		resizeCoefficientBuffer(std::max(32U, numCoefficients));
		mCoefficients->writeData(0, sizeof(LightProbeSHCoefficients) * numCoefficients, mInitCoefficients.data());
		mInitCoefficients.clear();

		gRenderer()->notifyLightProbeVolumeAdded(this);
		CoreObject::initialize();
	}

	bool LightProbeVolume::renderProbes(UINT32 maxProbes)
	{
		// Probe map only contains active probes
		UINT32 numUsedProbes = (UINT32)mProbeMap.size();
		if(numUsedProbes > mCoeffBufferSize)
			resizeCoefficientBuffer(std::max(32U, numUsedProbes * 2));

		UINT32 numProbeUpdates = 0;
		for (; mFirstDirtyProbe < (UINT32)mProbeInfos.size(); ++mFirstDirtyProbe)
		{
			LightProbeInfo& probeInfo = mProbeInfos[mFirstDirtyProbe];

			if(probeInfo.flags == LightProbeFlags::Dirty)
			{
				TEXTURE_DESC cubemapDesc;
				cubemapDesc.type = TEX_TYPE_CUBE_MAP;
				cubemapDesc.format = PF_FLOAT16_RGB;
				cubemapDesc.width = IBLUtility::IRRADIANCE_CUBEMAP_SIZE;
				cubemapDesc.height = IBLUtility::IRRADIANCE_CUBEMAP_SIZE;
				cubemapDesc.usage = TU_STATIC | TU_RENDERTARGET;

				SPtr<Texture> cubemap = Texture::create(cubemapDesc);

				gRenderer()->captureSceneCubeMap(cubemap, mProbePositions[mFirstDirtyProbe], true);
				gIBLUtility().filterCubemapForIrradiance(cubemap, mCoefficients, probeInfo.bufferIdx);

				probeInfo.flags = LightProbeFlags::Clean;
				numProbeUpdates++;
			}

			if (maxProbes != 0 && numProbeUpdates >= maxProbes)
				break;
		}

		gRenderer()->notifyLightProbeVolumeUpdated(this, true);

		return mFirstDirtyProbe == (UINT32)mProbeInfos.size();
	}

	void LightProbeVolume::syncToCore(const CoreSyncData& data)
	{
		char* dataPtr = (char*)data.getBuffer();

		bool oldIsActive = mIsActive;

		dataPtr = rttiReadElem(mPosition, dataPtr);
		dataPtr = rttiReadElem(mRotation, dataPtr);
		dataPtr = rttiReadElem(mIsActive, dataPtr);

		UINT32 numDirtyProbes, numRemovedProbes;
		dataPtr = rttiReadElem(numDirtyProbes, dataPtr);
		dataPtr = rttiReadElem(numRemovedProbes, dataPtr);

		for (UINT32 i = 0; i < numDirtyProbes; ++i)
		{
			UINT32 handle;
			dataPtr = rttiReadElem(handle, dataPtr);

			Vector3 position;
			dataPtr = rttiReadElem(position, dataPtr);

			LightProbeFlags flags;
			dataPtr = rttiReadElem(flags, dataPtr);

			auto iterFind = mProbeMap.find(handle);
			if(iterFind != mProbeMap.end())
			{
				// Update existing probe information
				UINT32 compactIdx = iterFind->second;
				
				mProbeInfos[compactIdx].flags = LightProbeFlags::Dirty;
				mProbePositions[compactIdx] = position;

				mFirstDirtyProbe = std::min(compactIdx, mFirstDirtyProbe);
			}
			else // Add a new probe
			{
				// Empty slots always start at a specific index because we always move them to the back of the array
				UINT32 emptyProbeStartIdx = (UINT32)mProbeMap.size();
				UINT32 numProbes = (UINT32)mProbeInfos.size();

				// Find an empty slot to place the probe information at
				UINT32 compactIdx = -1;
				for(UINT32 j = emptyProbeStartIdx; j < numProbes; ++j)
				{
					if(mProbeInfos[j].flags == LightProbeFlags::Empty)
					{
						compactIdx = j;
						break;
					}
				}

				// Found an empty slot
				if (compactIdx == -1)
				{
					compactIdx = (UINT32)mProbeInfos.size();

					LightProbeInfo info;
					info.flags = LightProbeFlags::Dirty;
					info.bufferIdx = compactIdx;
					info.handle = handle;

					mProbeInfos.push_back(info);
					mProbePositions.push_back(position);
				}
				else // No empty slot, add a new one
				{
					LightProbeInfo& info = mProbeInfos[compactIdx];
					info.flags = LightProbeFlags::Dirty;
					info.handle = handle;

					mProbePositions[compactIdx] = position;
				}

				mProbeMap[handle] = compactIdx;
				mFirstDirtyProbe = std::min(compactIdx, mFirstDirtyProbe);
			}
		}

		// Mark slots for removed probes as empty, and move them back to the end of the array
		for (UINT32 i = 0; i < numRemovedProbes; ++i)
		{
			UINT32 idx;
			dataPtr = rttiReadElem(idx, dataPtr);

			auto iterFind = mProbeMap.find(idx);
			if(iterFind != mProbeMap.end())
			{
				UINT32 compactIdx = iterFind->second;
				
				LightProbeInfo& info = mProbeInfos[compactIdx];
				info.flags = LightProbeFlags::Empty;

				// Move the empty info to the back of the array so all non-empty probes are contiguous
				// Search from back to current index, and find first non-empty probe to switch switch
				UINT32 lastSearchIdx = (UINT32)mProbeInfos.size() - 1;
				while (lastSearchIdx >= (INT32)compactIdx)
				{
					LightProbeFlags flags = mProbeInfos[lastSearchIdx].flags;
					if (flags != LightProbeFlags::Empty)
					{
						std::swap(mProbeInfos[i], mProbeInfos[lastSearchIdx]);
						std::swap(mProbePositions[i], mProbePositions[lastSearchIdx]);

						mProbeMap[mProbeInfos[lastSearchIdx].handle] = i;
						break;
					}

					lastSearchIdx--;
				}
				
				mProbeMap.erase(iterFind);
			}
		}

		if (oldIsActive != mIsActive)
		{
			if (mIsActive)
				gRenderer()->notifyLightProbeVolumeAdded(this);
			else
				gRenderer()->notifyLightProbeVolumeRemoved(this);
		}
		else
		{
			if(mIsActive)
				gRenderer()->notifyLightProbeVolumeUpdated(this, false);
		}
	}

	void LightProbeVolume::getProbeCoefficients(Vector<LightProbeCoefficientInfo>& output) const
	{
		UINT32 numActiveProbes = (UINT32)mProbeMap.size();
		if (numActiveProbes == 0)
			return;

		output.resize(numActiveProbes);

		LightProbeSHCoefficients* coefficients = bs_stack_alloc<LightProbeSHCoefficients>(numActiveProbes);
		mCoefficients->readData(0, sizeof(LightProbeSHCoefficients) * numActiveProbes, coefficients);

		for(UINT32 i = 0; i < numActiveProbes; ++i)
		{
			output[i].coefficients = coefficients[mProbeInfos[i].bufferIdx];
			output[i].handle = mProbeInfos[i].handle;
		}

		bs_stack_free(coefficients);
	}

	void LightProbeVolume::resizeCoefficientBuffer(UINT32 count)
	{
		GPU_BUFFER_DESC desc;
		desc.type = GBT_STRUCTURED;
		desc.elementSize = sizeof(LightProbeSHCoefficients);
		desc.elementCount = count;
		desc.usage = GBU_STATIC;
		desc.format = BF_UNKNOWN;

		SPtr<GpuBuffer> newBuffer = GpuBuffer::create(desc);
		if (mCoefficients)
			newBuffer->copyData(*mCoefficients, 0, 0, mCoefficients->getSize(), true);

		mCoefficients = newBuffer;
		mCoeffBufferSize = count;
	}
}}
