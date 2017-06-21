//********************************** Banshee Engine (www.banshee3d.com) **************************************************//
//**************** Copyright (c) 2016 Marko Pintera (marko.pintera@gmail.com). All rights reserved. **********************//
#include "BsGLRenderAPI.h"
#include "BsRenderAPI.h"
#include "BsGLTextureManager.h"
#include "BsGLIndexBuffer.h"
#include "BsGLUtil.h"
#include "BsGLSLGpuProgram.h"
#include "BsException.h"
#include "BsGLContext.h"
#include "BsGLSupport.h"
#include "BsBlendState.h"
#include "BsRasterizerState.h"
#include "BsDepthStencilState.h"
#include "BsGLRenderTexture.h"
#include "BsGLRenderWindowManager.h"
#include "BsGLSLProgramPipelineManager.h"
#include "BsGLVertexArrayObjectManager.h"
#include "BsRenderStateManager.h"
#include "BsGpuParams.h"
#include "BsGLGpuParamBlockBuffer.h"
#include "BsCoreThread.h"
#include "BsGLQueryManager.h"
#include "BsDebug.h"
#include "BsRenderStats.h"
#include "BsGpuParamDesc.h"
#include "BsGLGpuBuffer.h"
#include "BsGLCommandBuffer.h"
#include "BsGLCommandBufferManager.h"
#include "BsGLTextureView.h"

namespace bs { namespace ct
{
	const char* MODULE_NAME = "BansheeGLRenderAPI.dll";

	void __stdcall openGlErrorCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *message, GLvoid *userParam);

	/************************************************************************/
	/* 								PUBLIC INTERFACE                   		*/
	/************************************************************************/

	GLRenderAPI::GLRenderAPI()
		: mViewportNorm(0.0f, 0.0f, 1.0f, 1.0f)
		, mScissorTop(0), mScissorBottom(720), mScissorLeft(0), mScissorRight(1280)
		, mViewportLeft(0), mViewportTop(0), mViewportWidth(0), mViewportHeight(0)
		, mScissorEnabled(false)
		, mStencilReadMask(0xFFFFFFFF)
		, mStencilWriteMask(0xFFFFFFFF)
		, mStencilRefValue(0)
		, mStencilCompareFront(CMPF_ALWAYS_PASS)
		, mStencilCompareBack(CMPF_ALWAYS_PASS)
		, mNumTextureUnits(0)
		, mTextureInfos(nullptr)
		, mDepthWrite(true)
		, mGLSLProgramFactory(nullptr)
		, mProgramPipelineManager(nullptr)
		, mActivePipeline(nullptr)
		, mCurrentDrawOperation(DOT_TRIANGLE_LIST)
		, mDrawCallInProgress(false)
		, mActiveTextureUnit(-1)
	{
		// Get our GLSupport
		mGLSupport = ct::getGLSupport();

		mColorWrite[0] = mColorWrite[1] = mColorWrite[2] = mColorWrite[3] = true;

		mCurrentContext = 0;
		mMainContext = 0;

		mGLInitialised = false;

		mMinFilter = FO_LINEAR;
		mMipFilter = FO_POINT;

		mProgramPipelineManager = bs_new<GLSLProgramPipelineManager>();
	}

	GLRenderAPI::~GLRenderAPI()
	{

	}

	const StringID& GLRenderAPI::getName() const
	{
		static StringID strName("GLRenderAPI");
		return strName;
	}

	const String& GLRenderAPI::getShadingLanguageName() const
	{
		static String strName("glsl");
		return strName;
	}

	void GLRenderAPI::initialize()
	{
		THROW_IF_NOT_CORE_THREAD;

		mGLSupport->start();
		mVideoModeInfo = mGLSupport->getVideoModeInfo();

		CommandBufferManager::startUp<GLCommandBufferManager>();
		bs::RenderWindowManager::startUp<bs::GLRenderWindowManager>(this);
		RenderWindowManager::startUp<GLRenderWindowManager>(this);

		RenderStateManager::startUp();

		QueryManager::startUp<GLQueryManager>();

		RenderAPI::initialize();
	}

	void GLRenderAPI::initializeWithWindow(const SPtr<RenderWindow>& primaryWindow)
	{
		// Get the context from the window and finish initialization
		SPtr<GLContext> context;
		primaryWindow->getCustomAttribute("GLCONTEXT", &context);

		// Set main and current context
		mMainContext = context;
		mCurrentContext = mMainContext;

		// Set primary context as active
		if (mCurrentContext)
			mCurrentContext->setCurrent();

		checkForErrors();

		// Setup GLSupport
		mGLSupport->initializeExtensions();
		checkForErrors();

		mNumDevices = 1;
		mCurrentCapabilities = bs_newN<RenderAPICapabilities>(mNumDevices);
		initCapabilities(mCurrentCapabilities[0]);		

		initFromCaps(mCurrentCapabilities);
		GLVertexArrayObjectManager::startUp();
		glFrontFace(GL_CW);

		// Ensure cubemaps are filtered across seams
		glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

		GPUInfo gpuInfo;
		gpuInfo.numGPUs = 1;

		const char* vendor = (const char*)glGetString(GL_VENDOR);
		const char* renderer = (const char*)glGetString(GL_RENDERER);
		gpuInfo.names[0] = String(vendor) + " " + String(renderer);

		PlatformUtility::_setGPUInfo(gpuInfo);

		mGLInitialised = true;

		RenderAPI::initializeWithWindow(primaryWindow);
	}

	void GLRenderAPI::destroyCore()
	{
		RenderAPI::destroyCore();

		// Deleting the GLSL program factory
		if (mGLSLProgramFactory)
		{
			// Remove from manager safely
			GpuProgramManager::instance().removeFactory(mGLSLProgramFactory);
			bs_delete(mGLSLProgramFactory);
			mGLSLProgramFactory = nullptr;
		}

		// Deleting the hardware buffer manager.  Has to be done before the mGLSupport->stop().
		HardwareBufferManager::shutDown();
		bs::HardwareBufferManager::shutDown();
		GLRTTManager::shutDown();

		for (UINT32 i = 0; i < MAX_VB_COUNT; i++)
			mBoundVertexBuffers[i] = nullptr;

		mBoundVertexDeclaration = nullptr;
		mBoundIndexBuffer = nullptr;

		mCurrentVertexProgram = nullptr;
		mCurrentFragmentProgram = nullptr;
		mCurrentGeometryProgram = nullptr;
		mCurrentHullProgram = nullptr;
		mCurrentDomainProgram = nullptr;
		mCurrentComputeProgram = nullptr;

		mGLSupport->stop();

		TextureManager::shutDown();
		bs::TextureManager::shutDown();
		QueryManager::shutDown();
		RenderWindowManager::shutDown();
		bs::RenderWindowManager::shutDown();
		RenderStateManager::shutDown();
		GLVertexArrayObjectManager::shutDown(); // Note: Needs to be after QueryManager shutdown as some resources might be waiting for queries to complete
		CommandBufferManager::shutDown();

		mGLInitialised = false;

		if(mProgramPipelineManager != nullptr)
			bs_delete(mProgramPipelineManager);

		if(mGLSupport)
			bs_delete(mGLSupport);

		if (mTextureInfos != nullptr)
			bs_deleteN(mTextureInfos, mNumTextureUnits);
	}

	void GLRenderAPI::setGraphicsPipeline(const SPtr<GraphicsPipelineState>& pipelineState,
		const SPtr<CommandBuffer>& commandBuffer)
	{
		auto executeRef = [&](const SPtr<GraphicsPipelineState>& pipelineState)
		{
			THROW_IF_NOT_CORE_THREAD;

			BlendState* blendState;
			RasterizerState* rasterizerState;
			DepthStencilState* depthStencilState;
			if (pipelineState != nullptr)
			{
				mCurrentVertexProgram = std::static_pointer_cast<GLSLGpuProgram>(pipelineState->getVertexProgram());
				mCurrentFragmentProgram = std::static_pointer_cast<GLSLGpuProgram>(pipelineState->getFragmentProgram());
				mCurrentGeometryProgram = std::static_pointer_cast<GLSLGpuProgram>(pipelineState->getGeometryProgram());
				mCurrentDomainProgram = std::static_pointer_cast<GLSLGpuProgram>(pipelineState->getDomainProgram());
				mCurrentHullProgram = std::static_pointer_cast<GLSLGpuProgram>(pipelineState->getHullProgram());

				blendState = pipelineState->getBlendState().get();
				rasterizerState = pipelineState->getRasterizerState().get();
				depthStencilState = pipelineState->getDepthStencilState().get();

				if (blendState == nullptr)
					blendState = BlendState::getDefault().get();

				if (rasterizerState == nullptr)
					rasterizerState = RasterizerState::getDefault().get();

				if(depthStencilState == nullptr)
					depthStencilState = DepthStencilState::getDefault().get();
			}
			else
			{
				mCurrentVertexProgram = nullptr;
				mCurrentFragmentProgram = nullptr;
				mCurrentGeometryProgram = nullptr;
				mCurrentDomainProgram = nullptr;
				mCurrentHullProgram = nullptr;

				blendState = BlendState::getDefault().get();
				rasterizerState = RasterizerState::getDefault().get();
				depthStencilState = DepthStencilState::getDefault().get();
			}

			// Blend state
			{
				const BlendProperties& stateProps = blendState->getProperties();

				// Alpha to coverage
				setAlphaToCoverage(stateProps.getAlphaToCoverageEnabled());

				// Blend states
				// OpenGL doesn't allow us to specify blend state per render target, so we just use the first one.
				if (stateProps.getBlendEnabled(0))
				{
					setSceneBlending(stateProps.getSrcBlend(0), stateProps.getDstBlend(0), stateProps.getAlphaSrcBlend(0),
						stateProps.getAlphaDstBlend(0), stateProps.getBlendOperation(0), stateProps.getAlphaBlendOperation(0));
				}
				else
				{
					setSceneBlending(BF_ONE, BF_ZERO, BO_ADD);
				}

				// Color write mask
				UINT8 writeMask = stateProps.getRenderTargetWriteMask(0);
				setColorBufferWriteEnabled((writeMask & 0x1) != 0, (writeMask & 0x2) != 0, (writeMask & 0x4) != 0, (writeMask & 0x8) != 0);
			}

			// Rasterizer state
			{
				const RasterizerProperties& stateProps = rasterizerState->getProperties();

				setDepthBias(stateProps.getDepthBias(), stateProps.getSlopeScaledDepthBias());
				setCullingMode(stateProps.getCullMode());
				setPolygonMode(stateProps.getPolygonMode());
				setScissorTestEnable(stateProps.getScissorEnable());
				setMultisamplingEnable(stateProps.getMultisampleEnable());
				setDepthClipEnable(stateProps.getDepthClipEnable());
				setAntialiasedLineEnable(stateProps.getAntialiasedLineEnable());
			}

			// Depth stencil state
			{
				const DepthStencilProperties& stateProps = depthStencilState->getProperties();

				// Set stencil buffer options
				setStencilCheckEnabled(stateProps.getStencilEnable());

				setStencilBufferOperations(stateProps.getStencilFrontFailOp(), stateProps.getStencilFrontZFailOp(), stateProps.getStencilFrontPassOp(), true);
				setStencilBufferFunc(stateProps.getStencilFrontCompFunc(), stateProps.getStencilReadMask(), true);

				setStencilBufferOperations(stateProps.getStencilBackFailOp(), stateProps.getStencilBackZFailOp(), stateProps.getStencilBackPassOp(), false);
				setStencilBufferFunc(stateProps.getStencilBackCompFunc(), stateProps.getStencilReadMask(), false);

				setStencilBufferWriteMask(stateProps.getStencilWriteMask());

				// Set depth buffer options
				setDepthBufferCheckEnabled(stateProps.getDepthReadEnable());
				setDepthBufferWriteEnabled(stateProps.getDepthWriteEnable());
				setDepthBufferFunction(stateProps.getDepthComparisonFunc());
			}
		};

		if (commandBuffer == nullptr)
			executeRef(pipelineState);
		else
		{
			auto execute = [=]() { executeRef(pipelineState); };

			SPtr<GLCommandBuffer> cb = std::static_pointer_cast<GLCommandBuffer>(commandBuffer);
			cb->queueCommand(execute);
		}

		BS_INC_RENDER_STAT(NumPipelineStateChanges);
	}

	void GLRenderAPI::setComputePipeline(const SPtr<ComputePipelineState>& pipelineState,
		const SPtr<CommandBuffer>& commandBuffer)
	{
		auto executeRef = [&](const SPtr<ComputePipelineState>& pipelineState)
		{
			THROW_IF_NOT_CORE_THREAD;

			SPtr<GpuProgram> program;
			if (pipelineState != nullptr)
				program = pipelineState->getProgram();

			if (program != nullptr && program->getProperties().getType() == GPT_COMPUTE_PROGRAM)
				mCurrentComputeProgram = std::static_pointer_cast<GLSLGpuProgram>(program);
			else
				mCurrentComputeProgram = nullptr;
		};

		if (commandBuffer == nullptr)
			executeRef(pipelineState);
		else
		{
			auto execute = [=]() { executeRef(pipelineState); };

			SPtr<GLCommandBuffer> cb = std::static_pointer_cast<GLCommandBuffer>(commandBuffer);
			cb->queueCommand(execute);
		}

		BS_INC_RENDER_STAT(NumPipelineStateChanges);
	}

	void GLRenderAPI::setGpuParams(const SPtr<GpuParams>& gpuParams, const SPtr<CommandBuffer>& commandBuffer)
	{
		auto executeRef = [&](const SPtr<GpuParams>& gpuParams)
		{
			THROW_IF_NOT_CORE_THREAD;

			for(UINT32 i = 0; i < 8; i++)
				glBindImageTexture(i, 0, 0, false, 0, GL_READ_WRITE, GL_R32F);

			bs_frame_mark();
			{
				UINT32 textureUnitCount = 0;
				FrameVector<UINT32> textureUnits(12);
				auto getTexUnit = [&](UINT32 binding)
				{
					for(UINT32 i = 0; i < (UINT32)textureUnits.size(); i++)
					{
						if (textureUnits[i] == binding)
							return i;
					}

					UINT32 unit = textureUnitCount++;
					textureUnits.push_back(binding);

					return unit;
				};

				UINT32 imageUnitCount = 0;
				FrameVector<UINT32> imageUnits(6);
				auto getImageUnit = [&](UINT32 binding)
				{
					for (UINT32 i = 0; i < (UINT32)imageUnits.size(); i++)
					{
						if (imageUnits[i] == binding)
							return i;
					}

					UINT32 unit = imageUnitCount++;
					imageUnits.push_back(binding);

					return unit;
				};

				UINT32 uniformUnitCount = 0;
				FrameVector<UINT32> uniformUnits(6);
				auto getUniformUnit = [&](UINT32 binding)
				{
					for (UINT32 i = 0; i < (UINT32)uniformUnits.size(); i++)
					{
						if (uniformUnits[i] == binding)
							return i;
					}

					UINT32 unit = uniformUnitCount++;
					uniformUnits.push_back(binding);

					return unit;
				};

				UINT32 sharedStorageUnitCount = 0;
				FrameVector<UINT32> sharedStorageUnits(6);
				auto getSharedStorageUnit = [&](UINT32 binding)
				{
					for (UINT32 i = 0; i < (UINT32)sharedStorageUnits.size(); i++)
					{
						if (sharedStorageUnits[i] == binding)
							return i;
					}

					UINT32 unit = sharedStorageUnitCount++;
					sharedStorageUnits.push_back(binding);

					return unit;
				};

				const UINT32 numStages = 6;
				for(UINT32 i = 0; i < numStages; i++)
				{
					textureUnits.clear();
					imageUnits.clear();
					uniformUnits.clear();
					sharedStorageUnits.clear();

					GpuProgramType type = (GpuProgramType)i;

					SPtr<GpuParamDesc> paramDesc = gpuParams->getParamDesc(type);
					if (paramDesc == nullptr)
						continue;

					for (auto& entry : paramDesc->textures)
					{
						UINT32 binding = entry.second.slot;
						SPtr<Texture> texture = gpuParams->getTexture(entry.second.set, binding);
						const TextureSurface& surface = gpuParams->getTextureSurface(entry.second.set, binding);

						UINT32 unit = getTexUnit(binding);
						if (!activateGLTextureUnit(unit))
							continue;

						GLTexture* glTex = static_cast<GLTexture*>(texture.get());

						if (glTex != nullptr)
						{
							SPtr<TextureView> texView = glTex->requestView(surface.mipLevel, surface.numMipLevels, 
								surface.arraySlice, surface.numArraySlices, GVU_DEFAULT);

							GLTextureView* glTexView = static_cast<GLTextureView*>(texView.get());
							GLenum newTextureType = glTexView->getGLTextureTarget();

							if (mTextureInfos[unit].type != newTextureType)
								glBindTexture(mTextureInfos[unit].type, 0);

							glBindTexture(newTextureType, glTexView->getGLID());
							mTextureInfos[unit].type = newTextureType;

							SPtr<GLSLGpuProgram> activeProgram = getActiveProgram(type);
							if (activeProgram != nullptr)
							{
								GLuint glProgram = activeProgram->getGLHandle();

								glProgramUniform1i(glProgram, binding, unit);
							}
						}
						else
							glBindTexture(mTextureInfos[unit].type, 0);
					}

					for(auto& entry : paramDesc->samplers)
					{
						UINT32 binding = entry.second.slot;
						SPtr<SamplerState> samplerState = gpuParams->getSamplerState(entry.second.set, binding);

						if (samplerState == nullptr)
							samplerState = SamplerState::getDefault();

						UINT32 unit = getTexUnit(binding);
						if (!activateGLTextureUnit(unit))
							continue;

						bool isMultisample = mTextureInfos[unit].type == GL_TEXTURE_2D_MULTISAMPLE ||
							mTextureInfos[unit].type == GL_TEXTURE_2D_MULTISAMPLE_ARRAY;

						// No sampler options for multisampled textures
						if (!isMultisample)
						{
							const SamplerProperties& stateProps = samplerState->getProperties();

							setTextureFiltering(unit, FT_MIN, stateProps.getTextureFiltering(FT_MIN));
							setTextureFiltering(unit, FT_MAG, stateProps.getTextureFiltering(FT_MAG));
							setTextureFiltering(unit, FT_MIP, stateProps.getTextureFiltering(FT_MIP));

							setTextureAnisotropy(unit, stateProps.getTextureAnisotropy());
							setTextureCompareMode(unit, stateProps.getComparisonFunction());
							setTextureMipmapBias(unit, stateProps.getTextureMipmapBias());

							const UVWAddressingMode& uvw = stateProps.getTextureAddressingMode();
							setTextureAddressingMode(unit, uvw);

							setTextureBorderColor(unit, stateProps.getBorderColor());
						}
					}

					for(auto& entry : paramDesc->buffers)
					{
						UINT32 binding = entry.second.slot;
						SPtr<GpuBuffer> buffer = gpuParams->getBuffer(entry.second.set, binding);

						GLGpuBuffer* glBuffer = static_cast<GLGpuBuffer*>(buffer.get());

						switch(entry.second.type)
						{
						case GPOT_BYTE_BUFFER: // Texture buffer (read-only, unstructured)
							{
								UINT32 unit = getTexUnit(binding);
								if (!activateGLTextureUnit(unit))
									continue;

								if (glBuffer != nullptr)
								{
									if (mTextureInfos[unit].type != GL_TEXTURE_BUFFER)
										glBindTexture(mTextureInfos[unit].type, 0);

									mTextureInfos[unit].type = GL_TEXTURE_BUFFER;

									glBindTexture(GL_TEXTURE_BUFFER, glBuffer->getGLTextureId());

									SPtr<GLSLGpuProgram> activeProgram = getActiveProgram(type);
									if (activeProgram != nullptr)
									{
										GLuint glProgram = activeProgram->getGLHandle();

										glProgramUniform1i(glProgram, binding, unit);
									}
								}
								else
									glBindTexture(mTextureInfos[unit].type, 0);
							}
							break;
						case GPOT_RWBYTE_BUFFER: // Storage buffer (read/write, unstructured)
							{
								UINT32 unit = getImageUnit(binding);
								if (glBuffer != nullptr)
								{
									glBindImageTexture(unit, glBuffer->getGLTextureId(), 0, false,
													   0, GL_READ_WRITE, glBuffer->getGLFormat());

									SPtr<GLSLGpuProgram> activeProgram = getActiveProgram(type);
									if (activeProgram != nullptr)
									{
										GLuint glProgram = activeProgram->getGLHandle();

										glProgramUniform1i(glProgram, binding, unit);
									}
								}
								else
									glBindImageTexture(unit, 0, 0, false, 0, GL_READ_WRITE, GL_R32F);
							}
							break;
						case GPOT_RWSTRUCTURED_BUFFER: // Shared storage block (read/write, structured)
							{
								UINT32 unit = getSharedStorageUnit(binding);
								if (glBuffer != nullptr)
								{
									glBindBufferBase(GL_SHADER_STORAGE_BUFFER, unit, glBuffer->getGLBufferId());
									
									SPtr<GLSLGpuProgram> activeProgram = getActiveProgram(type);
									if (activeProgram != nullptr)
									{
										GLuint glProgram = activeProgram->getGLHandle();

										glShaderStorageBlockBinding(glProgram, binding, unit);
									}
								}
								else
									glBindBufferBase(GL_SHADER_STORAGE_BUFFER, unit, 0);
							}
							break;
						}
					}

					for(auto& entry : paramDesc->loadStoreTextures)
					{
						UINT32 binding = entry.second.slot;

						SPtr<Texture> texture = gpuParams->getLoadStoreTexture(entry.second.set, binding);
						const TextureSurface& surface = gpuParams->getLoadStoreSurface(entry.second.set, binding);

						UINT32 unit = getImageUnit(binding);
						if (texture != nullptr)
						{
							GLTexture* tex = static_cast<GLTexture*>(texture.get());
							glBindImageTexture(unit, tex->getGLID(), surface.mipLevel, surface.numArraySlices > 1,
								surface.arraySlice, GL_READ_WRITE, tex->getGLFormat());

							SPtr<GLSLGpuProgram> activeProgram = getActiveProgram(type);
							if (activeProgram != nullptr)
							{
								GLuint glProgram = activeProgram->getGLHandle();

								glProgramUniform1i(glProgram, binding, unit);
							}
						}
						else
							glBindImageTexture(unit, 0, 0, false, 0, GL_READ_WRITE, GL_R32F);
					}

					for (auto& entry : paramDesc->paramBlocks)
					{
						UINT32 binding = entry.second.slot;
						SPtr<GpuParamBlockBuffer> buffer = gpuParams->getParamBlockBuffer(entry.second.set, binding);
						
						if (buffer == nullptr)
							continue;

						buffer->flushToGPU();

						SPtr<GLSLGpuProgram> activeProgram = getActiveProgram(type);
						GLuint glProgram = activeProgram->getGLHandle();

						// 0 means uniforms are not in block, in which case we handle it specially
						if (binding == 0)
						{
							UINT8* uniformBufferData = (UINT8*)bs_stack_alloc(buffer->getSize());
							buffer->read(0, uniformBufferData, buffer->getSize());

							for (auto iter = paramDesc->params.begin(); iter != paramDesc->params.end(); ++iter)
							{
								const GpuParamDataDesc& param = iter->second;

								if (param.paramBlockSlot != 0) // 0 means uniforms are not in a block
									continue;

								const UINT8* ptrData = uniformBufferData + param.cpuMemOffset * sizeof(UINT32);

								// Note: We don't transpose matrices here even though we don't use column major format
								// because they are assumed to be pre-transposed in the GpuParams buffer
								switch (param.type)
								{
								case GPDT_FLOAT1:
									glProgramUniform1fv(glProgram, param.gpuMemOffset, param.arraySize, (GLfloat*)ptrData);
									break;
								case GPDT_FLOAT2:
									glProgramUniform2fv(glProgram, param.gpuMemOffset, param.arraySize, (GLfloat*)ptrData);
									break;
								case GPDT_FLOAT3:
									glProgramUniform3fv(glProgram, param.gpuMemOffset, param.arraySize, (GLfloat*)ptrData);
									break;
								case GPDT_FLOAT4:
									glProgramUniform4fv(glProgram, param.gpuMemOffset, param.arraySize, (GLfloat*)ptrData);
									break;
								case GPDT_MATRIX_2X2:
									glProgramUniformMatrix2fv(glProgram, param.gpuMemOffset, param.arraySize,
										GL_FALSE, (GLfloat*)ptrData);
									break;
								case GPDT_MATRIX_2X3:
									glProgramUniformMatrix3x2fv(glProgram, param.gpuMemOffset, param.arraySize,
										GL_FALSE, (GLfloat*)ptrData);
									break;
								case GPDT_MATRIX_2X4:
									glProgramUniformMatrix4x2fv(glProgram, param.gpuMemOffset, param.arraySize,
										GL_FALSE, (GLfloat*)ptrData);
									break;
								case GPDT_MATRIX_3X2:
									glProgramUniformMatrix2x3fv(glProgram, param.gpuMemOffset, param.arraySize,
										GL_FALSE, (GLfloat*)ptrData);
									break;
								case GPDT_MATRIX_3X3:
									glProgramUniformMatrix3fv(glProgram, param.gpuMemOffset, param.arraySize,
										GL_FALSE, (GLfloat*)ptrData);
									break;
								case GPDT_MATRIX_3X4:
									glProgramUniformMatrix4x3fv(glProgram, param.gpuMemOffset, param.arraySize,
										GL_FALSE, (GLfloat*)ptrData);
									break;
								case GPDT_MATRIX_4X2:
									glProgramUniformMatrix2x4fv(glProgram, param.gpuMemOffset, param.arraySize,
										GL_FALSE, (GLfloat*)ptrData);
									break;
								case GPDT_MATRIX_4X3:
									glProgramUniformMatrix3x4fv(glProgram, param.gpuMemOffset, param.arraySize,
										GL_FALSE, (GLfloat*)ptrData);
									break;
								case GPDT_MATRIX_4X4:
									glProgramUniformMatrix4fv(glProgram, param.gpuMemOffset, param.arraySize,
										GL_FALSE, (GLfloat*)ptrData);
									break;
								case GPDT_INT1:
									glProgramUniform1iv(glProgram, param.gpuMemOffset, param.arraySize, (GLint*)ptrData);
									break;
								case GPDT_INT2:
									glProgramUniform2iv(glProgram, param.gpuMemOffset, param.arraySize, (GLint*)ptrData);
									break;
								case GPDT_INT3:
									glProgramUniform3iv(glProgram, param.gpuMemOffset, param.arraySize, (GLint*)ptrData);
									break;
								case GPDT_INT4:
									glProgramUniform4iv(glProgram, param.gpuMemOffset, param.arraySize, (GLint*)ptrData);
									break;
								case GPDT_BOOL:
									glProgramUniform1uiv(glProgram, param.gpuMemOffset, param.arraySize, (GLuint*)ptrData);
									break;
								default:
								case GPDT_UNKNOWN:
									break;
								}
							}

							if (uniformBufferData != nullptr)
							{
								bs_stack_free(uniformBufferData);
							}
						}
						else
						{
							const GLGpuParamBlockBuffer* glParamBlockBuffer = static_cast<const GLGpuParamBlockBuffer*>(buffer.get());

							UINT32 unit = getUniformUnit(binding - 1);
							glUniformBlockBinding(glProgram, binding - 1, unit);
							glBindBufferBase(GL_UNIFORM_BUFFER, unit, glParamBlockBuffer->getGLHandle());
						}
					}
				}
			}
			bs_frame_clear();

			activateGLTextureUnit(0);
		};

		if (commandBuffer == nullptr)
			executeRef(gpuParams);
		else
		{
			auto execute = [=]() { executeRef(gpuParams); };

			SPtr<GLCommandBuffer> cb = std::static_pointer_cast<GLCommandBuffer>(commandBuffer);
			cb->queueCommand(execute);
		}

		BS_INC_RENDER_STAT(NumGpuParamBinds);
	}

	void GLRenderAPI::setStencilRef(UINT32 stencilRefValue, const SPtr<CommandBuffer>& commandBuffer)
	{
		auto executeRef = [&](UINT32 stencilRefValue)
		{
			THROW_IF_NOT_CORE_THREAD;

			setStencilRefValue(stencilRefValue);
		};

		if (commandBuffer == nullptr)
			executeRef(stencilRefValue);
		else
		{
			auto execute = [=]() { executeRef(stencilRefValue); };

			SPtr<GLCommandBuffer> cb = std::static_pointer_cast<GLCommandBuffer>(commandBuffer);
			cb->queueCommand(execute);
		}
	}

	void GLRenderAPI::setViewport(const Rect2& area,
		const SPtr<CommandBuffer>& commandBuffer)
	{
		auto executeRef = [&](const Rect2& area)
		{
			THROW_IF_NOT_CORE_THREAD;

			mViewportNorm = area;
			applyViewport();
		};

		if (commandBuffer == nullptr)
			executeRef(area);
		else
		{
			auto execute = [=]() { executeRef(area); };

			SPtr<GLCommandBuffer> cb = std::static_pointer_cast<GLCommandBuffer>(commandBuffer);
			cb->queueCommand(execute);
		}
	}

	void GLRenderAPI::setRenderTarget(const SPtr<RenderTarget>& target, UINT32 readOnlyFlags, 
		RenderSurfaceMask loadMask, const SPtr<CommandBuffer>& commandBuffer)
	{
		auto executeRef = [&](const SPtr<RenderTarget>& target, UINT32 readOnlyFlags)
		{
			THROW_IF_NOT_CORE_THREAD;

			// Switch context if different from current one
			if (target != nullptr)
			{
				SPtr<GLContext> newContext;
				target->getCustomAttribute("GLCONTEXT", &newContext);
				if (newContext && mCurrentContext != newContext)
				{
					switchContext(newContext);
				}
			}

			// This must happen after context switch to ensure previous context is still alive
			mActiveRenderTarget = target;

			GLFrameBufferObject* fbo = nullptr;

			if (target != nullptr)
				target->getCustomAttribute("FBO", &fbo);

			if (fbo != nullptr)
			{
				fbo->bind();

				// Enable / disable sRGB states
				if (target->getProperties().isHwGammaEnabled())
					glEnable(GL_FRAMEBUFFER_SRGB);
				else
					glDisable(GL_FRAMEBUFFER_SRGB);
			}
			else
				glBindFramebuffer(GL_FRAMEBUFFER, 0);

			applyViewport();
		};

		if (commandBuffer == nullptr)
			executeRef(target, readOnlyFlags);
		else
		{
			auto execute = [=]() { executeRef(target, readOnlyFlags); };

			SPtr<GLCommandBuffer> cb = std::static_pointer_cast<GLCommandBuffer>(commandBuffer);
			cb->queueCommand(execute);
		}

		BS_INC_RENDER_STAT(NumRenderTargetChanges);
	}

	void GLRenderAPI::setVertexBuffers(UINT32 index, SPtr<VertexBuffer>* buffers, UINT32 numBuffers, 
		const SPtr<CommandBuffer>& commandBuffer)
	{
#if BS_DEBUG_MODE
		UINT32 lastIdx = index + numBuffers;
		if(lastIdx > MAX_VB_COUNT)
		{
			LOGERR("Provided vertex buffer slot range is invalid: " + toString(index) + " to " + 
				toString(index + numBuffers) + ".");
			return;
		}
#endif

		auto executeRef = [&](UINT32 index, SPtr<VertexBuffer>* buffers, UINT32 numBuffers)
		{
			THROW_IF_NOT_CORE_THREAD;

			std::array<SPtr<VertexBuffer>, MAX_VB_COUNT> boundBuffers;
			for (UINT32 i = 0; i < numBuffers; i++)
				boundBuffers[index + i] = buffers[i];

			for (UINT32 i = 0; i < numBuffers; i++)
				mBoundVertexBuffers[index + i] = boundBuffers[index + i];
		};

		if (commandBuffer == nullptr)
			executeRef(index, buffers, numBuffers);
		else
		{
			auto execute = [=]() { executeRef(index, buffers, numBuffers); };

			SPtr<GLCommandBuffer> cb = std::static_pointer_cast<GLCommandBuffer>(commandBuffer);
			cb->queueCommand(execute);
		}
	}

	void GLRenderAPI::setVertexDeclaration(const SPtr<VertexDeclaration>& vertexDeclaration,
		const SPtr<CommandBuffer>& commandBuffer)
	{
		auto executeRef = [&](const SPtr<VertexDeclaration>& vertexDeclaration)
		{
			THROW_IF_NOT_CORE_THREAD;

			mBoundVertexDeclaration = vertexDeclaration;
		};

		if (commandBuffer == nullptr)
			executeRef(vertexDeclaration);
		else
		{
			auto execute = [=]() { executeRef(vertexDeclaration); };

			SPtr<GLCommandBuffer> cb = std::static_pointer_cast<GLCommandBuffer>(commandBuffer);
			cb->queueCommand(execute);
		}
	}

	void GLRenderAPI::setDrawOperation(DrawOperationType op, const SPtr<CommandBuffer>& commandBuffer)
	{
		auto executeRef = [&](DrawOperationType op)
		{
			THROW_IF_NOT_CORE_THREAD;

			mCurrentDrawOperation = op;
		};

		if (commandBuffer == nullptr)
			executeRef(op);
		else
		{
			auto execute = [=]() { executeRef(op); };

			SPtr<GLCommandBuffer> cb = std::static_pointer_cast<GLCommandBuffer>(commandBuffer);
			cb->queueCommand(execute);

			cb->mCurrentDrawOperation = op;
		}
	}

	void GLRenderAPI::setIndexBuffer(const SPtr<IndexBuffer>& buffer, const SPtr<CommandBuffer>& commandBuffer)
	{
		auto executeRef = [&](const SPtr<IndexBuffer>& buffer)
		{
			THROW_IF_NOT_CORE_THREAD;

			mBoundIndexBuffer = buffer;
		};

		if (commandBuffer == nullptr)
			executeRef(buffer);
		else
		{
			auto execute = [=]() { executeRef(buffer); };

			SPtr<GLCommandBuffer> cb = std::static_pointer_cast<GLCommandBuffer>(commandBuffer);
			cb->queueCommand(execute);
		}
	}

	void GLRenderAPI::draw(UINT32 vertexOffset, UINT32 vertexCount, UINT32 instanceCount, 
		const SPtr<CommandBuffer>& commandBuffer)
	{
		auto executeRef = [&](UINT32 vertexOffset, UINT32 vertexCount, UINT32 instanceCount)
		{
			THROW_IF_NOT_CORE_THREAD;

			// Find the correct type to render
			GLint primType = getGLDrawMode();
			beginDraw();

			if (instanceCount <= 1)
				glDrawArrays(primType, vertexOffset, vertexCount);
			else
				glDrawArraysInstanced(primType, vertexOffset, vertexCount, instanceCount);

			endDraw();
		};

		UINT32 primCount;
		if (commandBuffer == nullptr)
		{
			executeRef(vertexOffset, vertexCount, instanceCount);

			primCount = vertexCountToPrimCount(mCurrentDrawOperation, vertexCount);
		}
		else
		{
			auto execute = [=]() { executeRef(vertexOffset, vertexCount, instanceCount); };

			SPtr<GLCommandBuffer> cb = std::static_pointer_cast<GLCommandBuffer>(commandBuffer);
			cb->queueCommand(execute);

			primCount = vertexCountToPrimCount(cb->mCurrentDrawOperation, vertexCount);
		}

		BS_INC_RENDER_STAT(NumDrawCalls);
		BS_ADD_RENDER_STAT(NumVertices, vertexCount);
		BS_ADD_RENDER_STAT(NumPrimitives, primCount);
	}

	void GLRenderAPI::drawIndexed(UINT32 startIndex, UINT32 indexCount, UINT32 vertexOffset, UINT32 vertexCount,
		UINT32 instanceCount, const SPtr<CommandBuffer>& commandBuffer)
	{
		auto executeRef = [&](UINT32 startIndex, UINT32 indexCount, UINT32 vertexOffset, UINT32 vertexCount,
			UINT32 instanceCount)
		{
			THROW_IF_NOT_CORE_THREAD;

			if (mBoundIndexBuffer == nullptr)
			{
				LOGWRN("Cannot draw indexed because index buffer is not set.");
				return;
			}

			// Find the correct type to render
			GLint primType = getGLDrawMode();

			beginDraw();

			SPtr<GLIndexBuffer> indexBuffer = std::static_pointer_cast<GLIndexBuffer>(mBoundIndexBuffer);
			const IndexBufferProperties& ibProps = indexBuffer->getProperties();
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer->getGLBufferId());

			GLenum indexType = (ibProps.getType() == IT_16BIT) ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;

			if (instanceCount <= 1)
			{
				glDrawElementsBaseVertex(primType, indexCount, indexType,
					(GLvoid*)(UINT64)(ibProps.getIndexSize() * startIndex), vertexOffset);
			}
			else
			{
				glDrawElementsInstancedBaseVertex(primType, indexCount, indexType,
					(GLvoid*)(UINT64)(ibProps.getIndexSize() * startIndex), instanceCount, vertexOffset);
			}

			endDraw();
		};

		UINT32 primCount;
		if (commandBuffer == nullptr)
		{
			executeRef(startIndex, indexCount, vertexOffset, vertexCount, instanceCount);

			primCount = vertexCountToPrimCount(mCurrentDrawOperation, vertexCount);
		}
		else
		{
			auto execute = [=]() { executeRef(startIndex, indexCount, vertexOffset, vertexCount, instanceCount); };

			SPtr<GLCommandBuffer> cb = std::static_pointer_cast<GLCommandBuffer>(commandBuffer);
			cb->queueCommand(execute);

			primCount = vertexCountToPrimCount(cb->mCurrentDrawOperation, vertexCount);
		}

		BS_INC_RENDER_STAT(NumDrawCalls);
		BS_ADD_RENDER_STAT(NumVertices, vertexCount);
		BS_ADD_RENDER_STAT(NumPrimitives, primCount);

		BS_INC_RENDER_STAT(NumIndexBufferBinds);
	}

	void GLRenderAPI::dispatchCompute(UINT32 numGroupsX, UINT32 numGroupsY, UINT32 numGroupsZ, 
		const SPtr<CommandBuffer>& commandBuffer)
	{
		auto executeRef = [&](UINT32 numGroupsX, UINT32 numGroupsY, UINT32 numGroupsZ)
		{
			THROW_IF_NOT_CORE_THREAD;

			if (mCurrentComputeProgram == nullptr)
			{
				LOGWRN("Cannot dispatch compute without a set compute program.");
				return;
			}

			glUseProgram(mCurrentComputeProgram->getGLHandle());
			glDispatchCompute(numGroupsX, numGroupsY, numGroupsZ);
		};

		if (commandBuffer == nullptr)
			executeRef(numGroupsX, numGroupsY, numGroupsZ);
		else
		{
			auto execute = [=]() { executeRef(numGroupsX, numGroupsY, numGroupsZ); };

			SPtr<GLCommandBuffer> cb = std::static_pointer_cast<GLCommandBuffer>(commandBuffer);
			cb->queueCommand(execute);
		}

		BS_INC_RENDER_STAT(NumComputeCalls);
	}

	void GLRenderAPI::setScissorRect(UINT32 left, UINT32 top, UINT32 right, UINT32 bottom, 
		const SPtr<CommandBuffer>& commandBuffer)
	{
		auto executeRef = [&](UINT32 left, UINT32 top, UINT32 right, UINT32 bottom)
		{
			THROW_IF_NOT_CORE_THREAD;

			mScissorTop = top;
			mScissorBottom = bottom;
			mScissorLeft = left;
			mScissorRight = right;
		};

		if (commandBuffer == nullptr)
			executeRef(left, top, right, bottom);
		else
		{
			auto execute = [=]() { executeRef(left, top, right, bottom); };

			SPtr<GLCommandBuffer> cb = std::static_pointer_cast<GLCommandBuffer>(commandBuffer);
			cb->queueCommand(execute);
		}
	}

	void GLRenderAPI::clearRenderTarget(UINT32 buffers, const Color& color, float depth, UINT16 stencil, UINT8 targetMask,
		const SPtr<CommandBuffer>& commandBuffer)
	{
		auto executeRef = [&](UINT32 buffers, const Color& color, float depth, UINT16 stencil, UINT8 targetMask)
		{
			if (mActiveRenderTarget == nullptr)
				return;

			const RenderTargetProperties& rtProps = mActiveRenderTarget->getProperties();
			Rect2I clearRect(0, 0, rtProps.getWidth(), rtProps.getHeight());

			clearArea(buffers, color, depth, stencil, clearRect, targetMask);
		};

		if (commandBuffer == nullptr)
			executeRef(buffers, color, depth, stencil, targetMask);
		else
		{
			auto execute = [=]() { executeRef(buffers, color, depth, stencil, targetMask); };

			SPtr<GLCommandBuffer> cb = std::static_pointer_cast<GLCommandBuffer>(commandBuffer);
			cb->queueCommand(execute);
		}
	}

	void GLRenderAPI::clearViewport(UINT32 buffers, const Color& color, float depth, UINT16 stencil, UINT8 targetMask,
		const SPtr<CommandBuffer>& commandBuffer)
	{
		auto executeRef = [&](UINT32 buffers, const Color& color, float depth, UINT16 stencil, UINT8 targetMask)
		{
			Rect2I clearRect(mViewportLeft, mViewportTop, mViewportWidth, mViewportHeight);

			clearArea(buffers, color, depth, stencil, clearRect, targetMask);
		};

		if (commandBuffer == nullptr)
			executeRef(buffers, color, depth, stencil, targetMask);
		else
		{
			auto execute = [=]() { executeRef(buffers, color, depth, stencil, targetMask); };

			SPtr<GLCommandBuffer> cb = std::static_pointer_cast<GLCommandBuffer>(commandBuffer);
			cb->queueCommand(execute);
		}
	}

	void GLRenderAPI::swapBuffers(const SPtr<RenderTarget>& target, UINT32 syncMask)
	{
		THROW_IF_NOT_CORE_THREAD;
		target->swapBuffers();
	
		BS_INC_RENDER_STAT(NumPresents);
	}

	void GLRenderAPI::addCommands(const SPtr<CommandBuffer>& commandBuffer, const SPtr<CommandBuffer>& secondary)
	{
		SPtr<GLCommandBuffer> cb = std::static_pointer_cast<GLCommandBuffer>(commandBuffer);
		SPtr<GLCommandBuffer> secondaryCb = std::static_pointer_cast<GLCommandBuffer>(secondary);

		cb->appendSecondary(secondaryCb);
	}

	void GLRenderAPI::submitCommandBuffer(const SPtr<CommandBuffer>& commandBuffer, UINT32 syncMask)
	{
		SPtr<GLCommandBuffer> cb = std::static_pointer_cast<GLCommandBuffer>(commandBuffer);
		if (cb == nullptr)
			return;

		cb->executeCommands();
		cb->clear();
	}

	void GLRenderAPI::clearArea(UINT32 buffers, const Color& color, float depth, UINT16 stencil, const Rect2I& clearRect, 
		UINT8 targetMask)
	{
		THROW_IF_NOT_CORE_THREAD;

		if(mActiveRenderTarget == nullptr)
			return;

		bool colorMask = !mColorWrite[0] || !mColorWrite[1] 
		|| !mColorWrite[2] || !mColorWrite[3]; 

		// Disable scissor test as we want to clear the entire render surface
		GLboolean scissorTestEnabled = glIsEnabled(GL_SCISSOR_TEST);
		UINT32 oldScissorTop = mScissorTop;
		UINT32 oldScissorBottom = mScissorBottom;
		UINT32 oldScissorLeft = mScissorLeft;
		UINT32 oldScissorRight = mScissorRight;

		if (scissorTestEnabled)
		{
			glDisable(GL_SCISSOR_TEST);
		}

		const RenderTargetProperties& rtProps = mActiveRenderTarget->getProperties();

		bool clearEntireTarget = clearRect.width == 0 || clearRect.height == 0;
		clearEntireTarget |= (clearRect.x == 0 && clearRect.y == 0 && clearRect.width == rtProps.getWidth() && clearRect.height == rtProps.getHeight());

		if (!clearEntireTarget)
		{
			setScissorRect(clearRect.x, clearRect.y, clearRect.x + clearRect.width, clearRect.y + clearRect.height);
			setScissorTestEnable(true);
		}

		if (buffers & FBT_COLOR)
		{
			// Enable buffer for writing if it isn't
			if (colorMask)
				glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		}
		if (buffers & FBT_DEPTH)
		{
			// Enable buffer for writing if it isn't
			if (!mDepthWrite)
				glDepthMask(GL_TRUE);
		}
		if (buffers & FBT_STENCIL)
		{
			// Enable buffer for writing if it isn't
			glStencilMask(0xFFFFFFFF);
		}

		if (targetMask == 0xFF)
		{
			GLbitfield flags = 0;
			if (buffers & FBT_COLOR)
			{
				flags |= GL_COLOR_BUFFER_BIT;

				glClearColor(color.r, color.g, color.b, color.a);
			}

			if (buffers & FBT_DEPTH)
			{
				flags |= GL_DEPTH_BUFFER_BIT;

				glClearDepth(depth);
			}

			if (buffers & FBT_STENCIL)
			{
				flags |= GL_STENCIL_BUFFER_BIT;

				glClearStencil(stencil);
			}

			// Clear buffers
			glClear(flags);
		}
		else
		{
			GLFrameBufferObject* fbo = nullptr;
			mActiveRenderTarget->getCustomAttribute("FBO", &fbo);

			if (buffers & FBT_COLOR)
			{
				for (UINT32 i = 0; i < BS_MAX_MULTIPLE_RENDER_TARGETS; i++)
				{
					if (fbo->hasColorBuffer(i) && ((1 << i) & targetMask) != 0)
						glClearBufferfv(GL_COLOR, i, (GLfloat*)&color);
				}
			}

			if (buffers & FBT_DEPTH)
			{
				if (buffers & FBT_STENCIL)
				{
					glClearBufferfi(GL_DEPTH_STENCIL, 0, depth, stencil);
				}
				else
				{
					glClearBufferfv(GL_DEPTH, 0, &depth);
				}
			}
			else if (buffers & FBT_STENCIL)
			{
				INT32 stencilClear = (INT32)stencil;
				glClearBufferiv(GL_STENCIL, 0, &stencilClear);
			}
		}

		if (!clearEntireTarget)
		{
			setScissorTestEnable(false);
		}

		// Restore scissor test
		if (scissorTestEnabled)
		{
			glEnable(GL_SCISSOR_TEST);

			mScissorTop = oldScissorTop;
			mScissorBottom = oldScissorBottom;
			mScissorLeft = oldScissorLeft;
			mScissorRight = oldScissorRight;
		}

		// Reset buffer write state
		if (!mDepthWrite && (buffers & FBT_DEPTH))
		{
			glDepthMask(GL_FALSE);
		}
		if (colorMask && (buffers & FBT_COLOR))
		{
			glColorMask(mColorWrite[0], mColorWrite[1], mColorWrite[2], mColorWrite[3]);
		}
		if (buffers & FBT_STENCIL)
		{
			glStencilMask(mStencilWriteMask);
		}

		BS_INC_RENDER_STAT(NumClears);
	}

	/************************************************************************/
	/* 								PRIVATE		                     		*/
	/************************************************************************/

	void GLRenderAPI::setTextureAddressingMode(UINT16 unit, const UVWAddressingMode& uvw)
	{
		glTexParameteri(mTextureInfos[unit].type, GL_TEXTURE_WRAP_S, getTextureAddressingMode(uvw.u));
		glTexParameteri(mTextureInfos[unit].type, GL_TEXTURE_WRAP_T, getTextureAddressingMode(uvw.v));
		glTexParameteri(mTextureInfos[unit].type, GL_TEXTURE_WRAP_R, getTextureAddressingMode(uvw.w));
	}

	void GLRenderAPI::setTextureBorderColor(UINT16 unit, const Color& color)
	{
		GLfloat border[4] = { color.r, color.g, color.b, color.a };
		glTexParameterfv(mTextureInfos[unit].type, GL_TEXTURE_BORDER_COLOR, border);
	}

	void GLRenderAPI::setTextureMipmapBias(UINT16 unit, float bias)
	{
		glTexParameterf(mTextureInfos[unit].type, GL_TEXTURE_LOD_BIAS, bias);
	}

	void GLRenderAPI::setSceneBlending(BlendFactor sourceFactor, BlendFactor destFactor, BlendOperation op)
	{
		GLint sourceBlend = getBlendMode(sourceFactor);
		GLint destBlend = getBlendMode(destFactor);
		if(sourceFactor == BF_ONE && destFactor == BF_ZERO)
		{
			glDisable(GL_BLEND);
		}
		else
		{
			glEnable(GL_BLEND);
			glBlendFunc(sourceBlend, destBlend);
		}

		GLint func = GL_FUNC_ADD;
		switch(op)
		{
		case BO_ADD:
			func = GL_FUNC_ADD;
			break;
		case BO_SUBTRACT:
			func = GL_FUNC_SUBTRACT;
			break;
		case BO_REVERSE_SUBTRACT:
			func = GL_FUNC_REVERSE_SUBTRACT;
			break;
		case BO_MIN:
			func = GL_MIN;
			break;
		case BO_MAX:
			func = GL_MAX;
			break;
		}

		if(GLEW_VERSION_1_4 || GLEW_ARB_imaging)
		{
			glBlendEquation(func);
		}
		else if(GLEW_EXT_blend_minmax && (func == GL_MIN || func == GL_MAX))
		{
			glBlendEquationEXT(func);
		}
	}


	void GLRenderAPI::setSceneBlending(BlendFactor sourceFactor, BlendFactor destFactor, 
		BlendFactor sourceFactorAlpha, BlendFactor destFactorAlpha, BlendOperation op, BlendOperation alphaOp)
	{
		GLint sourceBlend = getBlendMode(sourceFactor);
		GLint destBlend = getBlendMode(destFactor);
		GLint sourceBlendAlpha = getBlendMode(sourceFactorAlpha);
		GLint destBlendAlpha = getBlendMode(destFactorAlpha);

		if(sourceFactor == BF_ONE && destFactor == BF_ZERO && 
			sourceFactorAlpha == BF_ONE && destFactorAlpha == BF_ZERO)
		{
			glDisable(GL_BLEND);
		}
		else
		{
			glEnable(GL_BLEND);
			glBlendFuncSeparate(sourceBlend, destBlend, sourceBlendAlpha, destBlendAlpha);
		}

		GLint func = GL_FUNC_ADD, alphaFunc = GL_FUNC_ADD;

		switch(op)
		{
		case BO_ADD:
			func = GL_FUNC_ADD;
			break;
		case BO_SUBTRACT:
			func = GL_FUNC_SUBTRACT;
			break;
		case BO_REVERSE_SUBTRACT:
			func = GL_FUNC_REVERSE_SUBTRACT;
			break;
		case BO_MIN:
			func = GL_MIN;
			break;
		case BO_MAX:
			func = GL_MAX;
			break;
		}

		switch(alphaOp)
		{
		case BO_ADD:
			alphaFunc = GL_FUNC_ADD;
			break;
		case BO_SUBTRACT:
			alphaFunc = GL_FUNC_SUBTRACT;
			break;
		case BO_REVERSE_SUBTRACT:
			alphaFunc = GL_FUNC_REVERSE_SUBTRACT;
			break;
		case BO_MIN:
			alphaFunc = GL_MIN;
			break;
		case BO_MAX:
			alphaFunc = GL_MAX;
			break;
		}

		if(GLEW_VERSION_2_0) {
			glBlendEquationSeparate(func, alphaFunc);
		}
		else if(GLEW_EXT_blend_equation_separate) {
			glBlendEquationSeparateEXT(func, alphaFunc);
		}
	}

	void GLRenderAPI::setAlphaTest(CompareFunction func, unsigned char value)
	{
		if(func == CMPF_ALWAYS_PASS)
		{
			glDisable(GL_ALPHA_TEST);
		}
		else
		{
			glEnable(GL_ALPHA_TEST);
			glAlphaFunc(convertCompareFunction(func), value / 255.0f);
		}
	}

	void GLRenderAPI::setAlphaToCoverage(bool enable)
	{
		static bool lasta2c = false;

		if (enable != lasta2c)
		{
			if (enable)
				glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
			else
				glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);

			lasta2c = enable;
		}
	}

	void GLRenderAPI::setScissorTestEnable(bool enable)
	{
		if (mActiveRenderTarget == nullptr)
			return;

		const RenderTargetProperties& rtProps = mActiveRenderTarget->getProperties();

		// Calculate the "lower-left" corner of the viewport
		GLsizei x = 0, y = 0, w = 0, h = 0;

		if (enable)
		{
			glEnable(GL_SCISSOR_TEST);

			x = mScissorLeft;
			y = mScissorTop;
			w = mScissorRight - mScissorLeft;
			h = mScissorBottom - mScissorTop;

			glScissor(x, y, w, h);
		}
		else
		{
			glDisable(GL_SCISSOR_TEST);

			// GL requires you to reset the scissor when disabling
			w = mViewportWidth;
			h = mViewportHeight;
			x = mViewportLeft;
			y = mViewportTop; 

			glScissor(x, y, w, h);
		}

		mScissorEnabled = enable;
	}

	void GLRenderAPI::setMultisamplingEnable(bool enable)
	{
		if (enable)
			glEnable(GL_MULTISAMPLE);
		else
			glDisable(GL_MULTISAMPLE);
	}

	void GLRenderAPI::setDepthClipEnable(bool enable)
	{
		if (enable)
			glEnable(GL_DEPTH_CLAMP);
		else
			glDisable(GL_DEPTH_CLAMP);
	}

	void GLRenderAPI::setAntialiasedLineEnable(bool enable)
	{
		if (enable)
			glEnable(GL_LINE_SMOOTH);
		else
			glDisable(GL_LINE_SMOOTH);
	}


	void GLRenderAPI::setCullingMode(CullingMode mode)
	{
		GLenum cullMode;

		switch( mode )
		{
		case CULL_NONE:
			glDisable(GL_CULL_FACE);
			return;
		default:
		case CULL_CLOCKWISE:
			cullMode = GL_FRONT;
			break;
		case CULL_COUNTERCLOCKWISE:
			cullMode = GL_BACK;
			break;
		}

		glEnable(GL_CULL_FACE);
		glCullFace(cullMode);
	}

	void GLRenderAPI::setDepthBufferCheckEnabled(bool enabled)
	{
		if (enabled)
		{
			glClearDepth(1.0f);
			glEnable(GL_DEPTH_TEST);
		}
		else
		{
			glDisable(GL_DEPTH_TEST);
		}
	}

	void GLRenderAPI::setDepthBufferWriteEnabled(bool enabled)
	{
		GLboolean flag = enabled ? GL_TRUE : GL_FALSE;
		glDepthMask(flag);  

		mDepthWrite = enabled;
	}

	void GLRenderAPI::setDepthBufferFunction(CompareFunction func)
	{
		glDepthFunc(convertCompareFunction(func));
	}

	void GLRenderAPI::setDepthBias(float constantBias, float slopeScaleBias)
	{
		if (constantBias != 0 || slopeScaleBias != 0)
		{
			glEnable(GL_POLYGON_OFFSET_FILL);
			glEnable(GL_POLYGON_OFFSET_POINT);
			glEnable(GL_POLYGON_OFFSET_LINE);

			float scaledConstantBias = -constantBias * float((1 << 24) - 1); // Note: Assumes 24-bit depth buffer
			glPolygonOffset(slopeScaleBias, scaledConstantBias);
		}
		else
		{
			glDisable(GL_POLYGON_OFFSET_FILL);
			glDisable(GL_POLYGON_OFFSET_POINT);
			glDisable(GL_POLYGON_OFFSET_LINE);
		}
	}

	void GLRenderAPI::setColorBufferWriteEnabled(bool red, bool green, bool blue, bool alpha)
	{
		glColorMask(red, green, blue, alpha);
		// record this
		mColorWrite[0] = red;
		mColorWrite[1] = blue;
		mColorWrite[2] = green;
		mColorWrite[3] = alpha;
	}

	void GLRenderAPI::setPolygonMode(PolygonMode level)
	{
		GLenum glmode;
		switch(level)
		{
		case PM_WIREFRAME:
			glmode = GL_LINE;
			break;
		default:
		case PM_SOLID:
			glmode = GL_FILL;
			break;
		}
		glPolygonMode(GL_FRONT_AND_BACK, glmode);
	}

	void GLRenderAPI::setStencilCheckEnabled(bool enabled)
	{
		if (enabled)
			glEnable(GL_STENCIL_TEST);
		else
			glDisable(GL_STENCIL_TEST);
	}

	void GLRenderAPI::setStencilBufferOperations(StencilOperation stencilFailOp,
		StencilOperation depthFailOp, StencilOperation passOp, bool front)
	{
		if (front)
		{
			glStencilOpSeparate(GL_FRONT, 
				convertStencilOp(stencilFailOp),
				convertStencilOp(depthFailOp), 
				convertStencilOp(passOp));
		}
		else
		{
			glStencilOpSeparate(GL_BACK, 
				convertStencilOp(stencilFailOp, true), 
				convertStencilOp(depthFailOp, true), 
				convertStencilOp(passOp, true));
		}
	}

	void GLRenderAPI::setStencilBufferFunc(CompareFunction func, UINT32 mask, bool front)
	{
		mStencilReadMask = mask;

		if(front)
		{
			mStencilCompareFront = func;
			glStencilFuncSeparate(GL_FRONT, convertCompareFunction(mStencilCompareFront), mStencilRefValue, mStencilReadMask);
		}
		else
		{
			mStencilCompareBack = func;
			glStencilFuncSeparate(GL_BACK, convertCompareFunction(mStencilCompareBack), mStencilRefValue, mStencilReadMask);
		}
	}

	void GLRenderAPI::setStencilBufferWriteMask(UINT32 mask)
	{
		mStencilWriteMask = mask;
		glStencilMask(mask);
	}

	void GLRenderAPI::setStencilRefValue(UINT32 refValue)
	{
		THROW_IF_NOT_CORE_THREAD;

		mStencilRefValue = refValue;

		glStencilFuncSeparate(GL_FRONT, convertCompareFunction(mStencilCompareFront), mStencilRefValue, mStencilReadMask);
		glStencilFuncSeparate(GL_BACK, convertCompareFunction(mStencilCompareBack), mStencilRefValue, mStencilReadMask);
	}

	void GLRenderAPI::setTextureFiltering(UINT16 unit, FilterType ftype, FilterOptions fo)
	{
		switch(ftype)
		{
		case FT_MIN:
			mMinFilter = fo;
			// Combine with existing mip filter
			glTexParameteri(mTextureInfos[unit].type, GL_TEXTURE_MIN_FILTER, getCombinedMinMipFilter());
			break;
		case FT_MAG:
			switch (fo)
			{
			case FO_ANISOTROPIC: // GL treats linear and aniso the same
			case FO_LINEAR:
				glTexParameteri(mTextureInfos[unit].type, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				break;
			case FO_POINT:
			case FO_NONE:
				glTexParameteri(mTextureInfos[unit].type, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
				break;
			default:
				break;
			}
			break;
		case FT_MIP:
			mMipFilter = fo;
			// Combine with existing min filter
			glTexParameteri(mTextureInfos[unit].type, GL_TEXTURE_MIN_FILTER, getCombinedMinMipFilter());
			break;
		}
	}

	void GLRenderAPI::setTextureAnisotropy(UINT16 unit, UINT32 maxAnisotropy)
	{
		GLfloat maxSupportAnisotropy = 0;
		glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxSupportAnisotropy);
		if (maxAnisotropy > maxSupportAnisotropy)
			maxAnisotropy = maxSupportAnisotropy ? 
			static_cast<UINT32>(maxSupportAnisotropy) : 1;

		if(maxAnisotropy < 1)
			maxAnisotropy = 1;

		if (getCurrentAnisotropy(unit) != maxAnisotropy)
			glTexParameterf(mTextureInfos[unit].type, GL_TEXTURE_MAX_ANISOTROPY_EXT, (float)maxAnisotropy);
	}

	void GLRenderAPI::setTextureCompareMode(UINT16 unit, CompareFunction compare)
	{
		if (compare == CMPF_ALWAYS_PASS)
			glTexParameteri(mTextureInfos[unit].type, GL_TEXTURE_COMPARE_MODE, GL_NONE);
		else
		{
			glTexParameteri(mTextureInfos[unit].type, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
			glTexParameteri(mTextureInfos[unit].type, GL_TEXTURE_COMPARE_FUNC, convertCompareFunction(compare));
		}
	}

	bool GLRenderAPI::activateGLTextureUnit(UINT16 unit)
	{
		if (mActiveTextureUnit != unit)
		{
			if (unit < getCapabilities(0).getNumCombinedTextureUnits())
			{
				glActiveTexture(GL_TEXTURE0 + unit);
				mActiveTextureUnit = unit;
				return true;
			}
			else if (!unit)
			{
				// always ok to use the first unit
				return true;
			}
			else
			{
				LOGWRN("Provided texture unit index is higher than OpenGL supports. Provided: " + toString(unit) + 
					". Supported range: 0 .. " + toString(getCapabilities(0).getNumCombinedTextureUnits() - 1));
				return false;
			}
		}
		else
		{
			return true;
		}
	}

	void GLRenderAPI::beginDraw()
	{
		if(mDrawCallInProgress)
			BS_EXCEPT(InternalErrorException, "Calling beginDraw without finishing previous draw call. Please call endDraw().");

		mDrawCallInProgress = true;

		if(mCurrentVertexProgram == nullptr)
		{
			LOGWRN("Cannot render without a set vertex shader.");
			return;
		}

		if(mBoundVertexDeclaration == nullptr)
		{
			LOGWRN("Cannot render without a set vertex declaration.");
			return;
		}

		const GLSLProgramPipeline* pipeline = mProgramPipelineManager->getPipeline(mCurrentVertexProgram.get(), 
			mCurrentFragmentProgram.get(), mCurrentGeometryProgram.get(), mCurrentHullProgram.get(), mCurrentDomainProgram.get());

		glUseProgram(0);
		if(mActivePipeline != pipeline)
		{
			glBindProgramPipeline(pipeline->glHandle);
			mActivePipeline = pipeline;
		}

		const GLVertexArrayObject& vao = GLVertexArrayObjectManager::instance().getVAO(mCurrentVertexProgram, 
			mBoundVertexDeclaration, mBoundVertexBuffers);
		glBindVertexArray(vao.getGLHandle()); 

		BS_INC_RENDER_STAT(NumVertexBufferBinds);
	}

	void GLRenderAPI::endDraw()
	{
		if(!mDrawCallInProgress)
			return;

		mDrawCallInProgress = false;
	}

	GLfloat GLRenderAPI::getCurrentAnisotropy(UINT16 unit)
	{
		GLfloat curAniso = 0;
		glGetTexParameterfv(mTextureInfos[unit].type, GL_TEXTURE_MAX_ANISOTROPY_EXT, &curAniso);

		return curAniso ? curAniso : 1;
	}

	GLint GLRenderAPI::convertStencilOp(StencilOperation op, bool invert) const
	{
		switch (op)
		{
		case SOP_KEEP:
			return GL_KEEP;
		case SOP_ZERO:
			return GL_ZERO;
		case SOP_REPLACE:
			return GL_REPLACE;
		case SOP_INCREMENT:
			return invert ? GL_DECR : GL_INCR;
		case SOP_DECREMENT:
			return invert ? GL_INCR : GL_DECR;
		case SOP_INCREMENT_WRAP:
			return invert ? GL_DECR_WRAP_EXT : GL_INCR_WRAP_EXT;
		case SOP_DECREMENT_WRAP:
			return invert ? GL_INCR_WRAP_EXT : GL_DECR_WRAP_EXT;
		case SOP_INVERT:
			return GL_INVERT;
		};
		// to keep compiler happy
		return SOP_KEEP;
	}

	GLint GLRenderAPI::convertCompareFunction(CompareFunction func) const
	{
		switch (func)
		{
		case CMPF_ALWAYS_FAIL:
			return GL_NEVER;
		case CMPF_ALWAYS_PASS:
			return GL_ALWAYS;
		case CMPF_LESS:
			return GL_LESS;
		case CMPF_LESS_EQUAL:
			return GL_LEQUAL;
		case CMPF_EQUAL:
			return GL_EQUAL;
		case CMPF_NOT_EQUAL:
			return GL_NOTEQUAL;
		case CMPF_GREATER_EQUAL:
			return GL_GEQUAL;
		case CMPF_GREATER:
			return GL_GREATER;
		};

		return GL_ALWAYS;
	}

	GLuint GLRenderAPI::getCombinedMinMipFilter() const
	{
		switch (mMinFilter)
		{
		case FO_ANISOTROPIC:
		case FO_LINEAR:
			switch (mMipFilter)
			{
			case FO_ANISOTROPIC:
			case FO_LINEAR:
				// Linear min, linear mip
				return GL_LINEAR_MIPMAP_LINEAR;
			case FO_POINT:
				// Linear min, point mip
				return GL_LINEAR_MIPMAP_NEAREST;
			case FO_NONE:
				// Linear min, no mip
				return GL_LINEAR;
			default:
				break;
			}
			break;
		case FO_POINT:
		case FO_NONE:
			switch (mMipFilter)
			{
			case FO_ANISOTROPIC:
			case FO_LINEAR:
				// Nearest min, linear mip
				return GL_NEAREST_MIPMAP_LINEAR;
			case FO_POINT:
				// Nearest min, point mip
				return GL_NEAREST_MIPMAP_NEAREST;
			case FO_NONE:
				// Nearest min, no mip
				return GL_NEAREST;
			default:
				break;
			}
			break;
		default:
			break;
		}

		// Should never get here
		return 0;
	}

	GLint GLRenderAPI::getBlendMode(BlendFactor blendMode) const
	{
		switch (blendMode)
		{
		case BF_ONE:
			return GL_ONE;
		case BF_ZERO:
			return GL_ZERO;
		case BF_DEST_COLOR:
			return GL_DST_COLOR;
		case BF_SOURCE_COLOR:
			return GL_SRC_COLOR;
		case BF_INV_DEST_COLOR:
			return GL_ONE_MINUS_DST_COLOR;
		case BF_INV_SOURCE_COLOR:
			return GL_ONE_MINUS_SRC_COLOR;
		case BF_DEST_ALPHA:
			return GL_DST_ALPHA;
		case BF_SOURCE_ALPHA:
			return GL_SRC_ALPHA;
		case BF_INV_DEST_ALPHA:
			return GL_ONE_MINUS_DST_ALPHA;
		case BF_INV_SOURCE_ALPHA:
			return GL_ONE_MINUS_SRC_ALPHA;
		};

		return GL_ONE;
	}

	GLint GLRenderAPI::getTextureAddressingMode(TextureAddressingMode tam) const
	{
		switch (tam)
		{
		default:
		case TAM_WRAP:
			return GL_REPEAT;
		case TAM_MIRROR:
			return GL_MIRRORED_REPEAT;
		case TAM_CLAMP:
			return GL_CLAMP_TO_EDGE;
		case TAM_BORDER:
			return GL_CLAMP_TO_BORDER;
		}
	}

	GLint GLRenderAPI::getGLDrawMode() const
	{
		GLint primType;

		// Use adjacency if there is a geometry program and it requested adjacency info
		bool useAdjacency = (mCurrentGeometryProgram != nullptr && mCurrentGeometryProgram->isAdjacencyInfoRequired());
		switch (mCurrentDrawOperation)
		{
		case DOT_POINT_LIST:
			primType = GL_POINTS;
			break;
		case DOT_LINE_LIST:
			primType = useAdjacency ? GL_LINES_ADJACENCY_EXT : GL_LINES;
			break;
		case DOT_LINE_STRIP:
			primType = useAdjacency ? GL_LINE_STRIP_ADJACENCY_EXT : GL_LINE_STRIP;
			break;
		default:
		case DOT_TRIANGLE_LIST:
			primType = useAdjacency ? GL_TRIANGLES_ADJACENCY_EXT : GL_TRIANGLES;
			break;
		case DOT_TRIANGLE_STRIP:
			primType = useAdjacency ? GL_TRIANGLE_STRIP_ADJACENCY_EXT : GL_TRIANGLE_STRIP;
			break;
		case DOT_TRIANGLE_FAN:
			primType = GL_TRIANGLE_FAN;
			break;
		}

		return primType;
	}

	SPtr<GLSLGpuProgram> GLRenderAPI::getActiveProgram(GpuProgramType gptype) const
	{
		switch (gptype)
		{
		case GPT_VERTEX_PROGRAM:
			return mCurrentVertexProgram;
		case GPT_FRAGMENT_PROGRAM:
			return mCurrentFragmentProgram;
		case GPT_GEOMETRY_PROGRAM:
			return mCurrentGeometryProgram;
		case GPT_DOMAIN_PROGRAM:
			return mCurrentDomainProgram;
		case GPT_HULL_PROGRAM:
			return mCurrentHullProgram;
		case GPT_COMPUTE_PROGRAM:
			return mCurrentComputeProgram;
		default:
			BS_EXCEPT(InvalidParametersException, "Insupported gpu program type: " + toString(gptype));
		}

		return nullptr;
	}

	void GLRenderAPI::initFromCaps(RenderAPICapabilities* caps)
	{
		if(caps->getRenderAPIName() != getName())
		{
			BS_EXCEPT(InvalidParametersException, 
				"Trying to initialize GLRenderAPI from RenderSystemCapabilities that do not support OpenGL");
		}

#if BS_DEBUG_MODE
		if (mGLSupport->checkExtension("GL_ARB_debug_output"))
		{
			glDebugMessageCallback(&openGlErrorCallback, 0);
			glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
		}
#endif

		bs::HardwareBufferManager::startUp();
		HardwareBufferManager::startUp<GLHardwareBufferManager>();

		// GPU Program Manager setup
		if(caps->isShaderProfileSupported("glsl"))
		{
			mGLSLProgramFactory = bs_new<GLSLProgramFactory>();
			GpuProgramManager::instance().addFactory(mGLSLProgramFactory);
		}

		GLRTTManager::startUp<GLRTTManager>();

		UINT32 curTexUnitOffset = 0;
		for (UINT32 i = 0; i < 6; i++)
			curTexUnitOffset += caps->getNumTextureUnits((GpuProgramType)i);

		UINT32 totalNumTexUnits = curTexUnitOffset;
		UINT16 numCombinedTexUnits = caps->getNumCombinedTextureUnits();

		if (totalNumTexUnits > numCombinedTexUnits)
			BS_EXCEPT(InternalErrorException, "Number of combined texture units less than the number of individual units!?");

		mNumTextureUnits = numCombinedTexUnits;
		mTextureInfos = bs_newN<TextureInfo>(mNumTextureUnits);
		for (UINT16 i = 0; i < mNumTextureUnits; i++)
			mTextureInfos[i].type = GL_TEXTURE_2D;
		
		bs::TextureManager::startUp<bs::GLTextureManager>(std::ref(*mGLSupport));
		TextureManager::startUp<GLTextureManager>(std::ref(*mGLSupport));
	}

	void GLRenderAPI::switchContext(const SPtr<GLContext>& context)
	{
		// Unbind GPU programs and rebind to new context later, because
		// scene manager treat render system as ONE 'context' ONLY, and it
		// cached the GPU programs using state.
		setGraphicsPipeline(nullptr);

		// It's ready for switching
		if (mCurrentContext)
			mCurrentContext->endCurrent();

		mCurrentContext = context;
		mCurrentContext->setCurrent();

		// Must reset depth/colour write mask to according with user desired, otherwise,
		// clearFrameBuffer would be wrong because the value we are recorded may be
		// difference with the really state stored in GL context.
		glDepthMask(mDepthWrite);
		glColorMask(mColorWrite[0], mColorWrite[1], mColorWrite[2], mColorWrite[3]);
		glStencilMask(mStencilWriteMask);
	}

	void GLRenderAPI::initCapabilities(RenderAPICapabilities& caps) const
	{
		Vector<String> tokens = StringUtil::split(mGLSupport->getGLVersion(), ".");

		DriverVersion driverVersion;
		if (!tokens.empty())
		{
			driverVersion.major = parseINT32(tokens[0]);
			if (tokens.size() > 1)
				driverVersion.minor = parseINT32(tokens[1]);
			if (tokens.size() > 2)
				driverVersion.release = parseINT32(tokens[2]);
		}
		driverVersion.build = 0;

		caps.setDriverVersion(driverVersion);
		const char* deviceName = (const char*)glGetString(GL_RENDERER);
		const char* vendorName = (const char*)glGetString(GL_VENDOR);
		caps.setDeviceName(deviceName);
		caps.setRenderAPIName(getName());

		// determine vendor
		if (strstr(vendorName, "NVIDIA"))
			caps.setVendor(GPU_NVIDIA);
		else if (strstr(vendorName, "ATI"))
			caps.setVendor(GPU_AMD);
		else if (strstr(vendorName, "AMD"))
			caps.setVendor(GPU_AMD);
		else if (strstr(vendorName, "Intel"))
			caps.setVendor(GPU_INTEL);
		else
			caps.setVendor(GPU_UNKNOWN);

		caps.addShaderProfile("glsl");
		caps.setCapability(RSC_TEXTURE_COMPRESSION_BC);

		// Check if geometry shaders are supported
		if (GLEW_VERSION_2_0 &&
			getGLSupport()->checkExtension("GL_EXT_geometry_shader4"))
		{
			caps.setCapability(RSC_GEOMETRY_PROGRAM);

			GLint maxOutputVertices;
			glGetIntegerv(GL_MAX_GEOMETRY_OUTPUT_VERTICES_EXT,&maxOutputVertices);
			caps.setGeometryProgramNumOutputVertices(maxOutputVertices);
		}

		// Max number of fragment shader textures
		GLint units;
		glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &units);
		caps.setNumTextureUnits(GPT_FRAGMENT_PROGRAM, static_cast<UINT16>(units));

		// Max number of vertex shader textures
		GLint vUnits;
		glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, &vUnits);
		caps.setNumTextureUnits(GPT_VERTEX_PROGRAM, static_cast<UINT16>(vUnits));

		GLint numUniformBlocks;
		glGetIntegerv(GL_MAX_VERTEX_UNIFORM_BLOCKS, &numUniformBlocks);
		caps.setNumGpuParamBlockBuffers(GPT_VERTEX_PROGRAM, numUniformBlocks);

		glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_BLOCKS, &numUniformBlocks);
		caps.setNumGpuParamBlockBuffers(GPT_FRAGMENT_PROGRAM, numUniformBlocks);

		if (mGLSupport->checkExtension("GL_ARB_geometry_shader4"))
		{
			GLint geomUnits;
			glGetIntegerv(GL_MAX_GEOMETRY_TEXTURE_IMAGE_UNITS, &geomUnits);
			caps.setNumTextureUnits(GPT_GEOMETRY_PROGRAM, static_cast<UINT16>(geomUnits));

			glGetIntegerv(GL_MAX_GEOMETRY_UNIFORM_BLOCKS, &numUniformBlocks);
			caps.setNumGpuParamBlockBuffers(GPT_GEOMETRY_PROGRAM, numUniformBlocks);
		}

		if (mGLSupport->checkExtension("GL_ARB_tessellation_shader"))
		{
			caps.setCapability(RSC_TESSELLATION_PROGRAM);

			glGetIntegerv(GL_MAX_TESS_CONTROL_UNIFORM_BLOCKS, &numUniformBlocks);
			caps.setNumGpuParamBlockBuffers(GPT_HULL_PROGRAM, numUniformBlocks);

			glGetIntegerv(GL_MAX_TESS_EVALUATION_UNIFORM_BLOCKS, &numUniformBlocks);
			caps.setNumGpuParamBlockBuffers(GPT_DOMAIN_PROGRAM, numUniformBlocks);
		}

		if (mGLSupport->checkExtension("GL_ARB_compute_shader")) 
		{
			caps.setCapability(RSC_COMPUTE_PROGRAM);

			GLint computeUnits;
			glGetIntegerv(GL_MAX_COMPUTE_TEXTURE_IMAGE_UNITS, &computeUnits);
			caps.setNumTextureUnits(GPT_COMPUTE_PROGRAM, static_cast<UINT16>(computeUnits));

			glGetIntegerv(GL_MAX_COMPUTE_UNIFORM_BLOCKS, &numUniformBlocks);
			caps.setNumGpuParamBlockBuffers(GPT_COMPUTE_PROGRAM, numUniformBlocks);

			// Max number of load-store textures
			GLint lsfUnits;
			glGetIntegerv(GL_MAX_FRAGMENT_IMAGE_UNIFORMS, &lsfUnits);
			caps.setNumLoadStoreTextureUnits(GPT_FRAGMENT_PROGRAM, static_cast<UINT16>(lsfUnits));

			GLint lscUnits;
			glGetIntegerv(GL_MAX_COMPUTE_IMAGE_UNIFORMS, &lscUnits);
			caps.setNumLoadStoreTextureUnits(GPT_COMPUTE_PROGRAM, static_cast<UINT16>(lscUnits));

			GLint combinedLoadStoreTextureUnits;
			glGetIntegerv(GL_MAX_IMAGE_UNITS, &combinedLoadStoreTextureUnits);
			caps.setNumCombinedLoadStoreTextureUnits(static_cast<UINT16>(combinedLoadStoreTextureUnits));
		}

		GLint combinedTexUnits;
		glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &combinedTexUnits);
		caps.setNumCombinedTextureUnits(static_cast<UINT16>(combinedTexUnits));

		GLint combinedUniformBlockUnits;
		glGetIntegerv(GL_MAX_COMBINED_UNIFORM_BLOCKS, &combinedUniformBlockUnits);
		caps.setNumCombinedGpuParamBlockBuffers(static_cast<UINT16>(combinedUniformBlockUnits));

		caps.setNumMultiRenderTargets(8);
	}

	bool GLRenderAPI::checkForErrors() const
	{
		GLenum glErr = glGetError();
		bool errorsFound = false;
		String msg;
		while (glErr != GL_NO_ERROR)
		{
			const char* glerrStr = (const char*)gluErrorString(glErr);
			if (glerrStr)
			{
				msg += String(glerrStr);
			}
			glErr = glGetError();
			errorsFound = true;
		}

		if (errorsFound)
			LOGWRN("OpenGL error: " + msg);

		return errorsFound;
	}

	void GLRenderAPI::makeGLMatrix(GLfloat gl_matrix[16], const Matrix4& m)
	{
		UINT32 x = 0;
		for (UINT32 i = 0; i < 4; i++)
		{
			for (UINT32 j = 0; j < 4; j++)
			{
				gl_matrix[x] = m[j][i];
				x++;
			}
		}
	}

	void GLRenderAPI::applyViewport()
	{
		if (mActiveRenderTarget == nullptr)
			return;

		const RenderTargetProperties& rtProps = mActiveRenderTarget->getProperties();

		// Calculate the "lower-left" corner of the viewport
		mViewportLeft = (UINT32)(rtProps.getWidth() * mViewportNorm.x);
		mViewportTop = (UINT32)(rtProps.getHeight() * mViewportNorm.y);
		mViewportWidth = (UINT32)(rtProps.getWidth() * mViewportNorm.width);
		mViewportHeight = (UINT32)(rtProps.getHeight() * mViewportNorm.height);

		glViewport(mViewportLeft, mViewportTop, mViewportWidth, mViewportHeight);

		// Configure the viewport clipping
		if (!mScissorEnabled)
		{
			glEnable(GL_SCISSOR_TEST);
			glScissor(mViewportLeft, mViewportTop, mViewportWidth, mViewportHeight);
		}
	}

	/************************************************************************/
	/* 								UTILITY		                     		*/
	/************************************************************************/

	void GLRenderAPI::convertProjectionMatrix(const Matrix4& matrix, Matrix4& dest)
	{
		dest = matrix;
	}

	const RenderAPIInfo& GLRenderAPI::getAPIInfo() const
	{
		static RenderAPIInfo info(0.0f, 0.0f, -1.0f, 1.0f, VET_COLOR_ABGR,
								  RenderAPIFeatureFlag::UVYAxisUp |
								  RenderAPIFeatureFlag::ColumnMajorMatrices |
								  RenderAPIFeatureFlag::MSAAImageStores);
								  
		return info;
	}

	GpuParamBlockDesc GLRenderAPI::generateParamBlockDesc(const String& name, Vector<GpuParamDataDesc>& params)
	{
		GpuParamBlockDesc block;
		block.blockSize = 0;
		block.isShareable = true;
		block.name = name;
		block.slot = 0;
		block.set = 0;

		for (auto& param : params)
		{
			const GpuParamDataTypeInfo& typeInfo = bs::GpuParams::PARAM_SIZES.lookup[param.type];
			UINT32 size = typeInfo.size / 4;
			UINT32 alignment = typeInfo.alignment / 4;

			// Fix alignment if needed
			UINT32 alignOffset = block.blockSize % alignment;
			if (alignOffset != 0)
			{
				UINT32 padding = (alignment - alignOffset);
				block.blockSize += padding;
			}

			if (param.arraySize > 1)
			{
				// Array elements are always padded and aligned to vec4
				alignOffset = size % typeInfo.baseTypeSize;
				if (alignOffset != 0)
				{
					UINT32 padding = (typeInfo.baseTypeSize - alignOffset);
					size += padding;
				}

				alignOffset = block.blockSize % typeInfo.baseTypeSize;
				if (alignOffset != 0)
				{
					UINT32 padding = (typeInfo.baseTypeSize - alignOffset);
					block.blockSize += padding;
				}

				param.elementSize = size;
				param.arrayElementStride = size;
				param.cpuMemOffset = block.blockSize;
				param.gpuMemOffset = 0;
				
				block.blockSize += size * param.arraySize;
			}
			else
			{
				param.elementSize = size;
				param.arrayElementStride = size;
				param.cpuMemOffset = block.blockSize;
				param.gpuMemOffset = 0;

				block.blockSize += size;
			}

			param.paramBlockSlot = 0;
			param.paramBlockSet = 0;
		}

		// Constant buffer size must always be a multiple of 16
		if (block.blockSize % 4 != 0)
			block.blockSize += (4 - (block.blockSize % 4));

		return block;
	}

	void __stdcall openGlErrorCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *message, GLvoid *userParam)
	{
		if (type != GL_DEBUG_TYPE_PERFORMANCE && type != GL_DEBUG_TYPE_OTHER)
		{
			BS_EXCEPT(RenderingAPIException, "OpenGL error: " + String(message));
		}
	}
}}